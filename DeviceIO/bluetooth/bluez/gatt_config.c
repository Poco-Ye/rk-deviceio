/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2014  Instituto Nokia de Tecnologia - INdT
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <pthread.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <DeviceIo/RkBtBase.h>

#include "error.h"
#include "gdbus/gdbus.h"
#include "gatt_config.h"

#define GATT_MGR_IFACE				"org.bluez.GattManager1"
#define GATT_SERVICE_IFACE			"org.bluez.GattService1"
#define GATT_CHR_IFACE				"org.bluez.GattCharacteristic1"
#define GATT_DESCRIPTOR_IFACE		"org.bluez.GattDescriptor1"

static char reconnect_path[66];
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
static volatile bool g_dis_adv_close_ble = false;
#define min(x, y) ((x) < (y) ? (x) : (y))

#define AD_FLAGS						0x1
#define AD_COMPLETE_128_SERVICE_UUID	0x7
#define AD_COMPLETE_LOCAL_NAME			0x9

typedef struct {
	uint8_t data[16];
} uuid128_t;

typedef struct {
	uint8_t data[6];
} mac_t;

struct AdvDataContent {
	uint8_t adv_length;
	uint8_t flag_length;
	uint8_t flag;
	uint8_t flag_value;
	uint8_t service_uuid_length;
	uint8_t service_uuid_flag;
	uuid128_t service_uuid_value;
};

struct AdvRespDataContent {
	uint8_t adv_resp_length;
	uint8_t local_name_length;
	uint8_t local_name_flag;
	uint8_t local_name_value[29];
};

#define GATT_MAX_CHR 10
typedef struct BLE_CONTENT_T
{
	uint8_t advData[32];
	uint8_t advDataLen;
	uint8_t respData[32];
	uint8_t respDataLen;
	uint8_t server_uuid[38];
	uint8_t char_uuid[GATT_MAX_CHR][38];
	uint8_t char_cnt;
	int (*cb_ble_recv_fun)(char *uuid, char *data, int len);
	void (*cb_ble_request_data)(char *uuid);
} ble_content_t;

ble_content_t *ble_content_internal;
ble_content_t ble_content_internal_bak;
static int gid = 0;
static int characteristic_id;
static int service_id;

char le_random_addr[6];
char CMD_RA[256] = "hcitool -i hci0 cmd 0x08 0x0005";
#define CMD_PARA "hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 01 00 00 00 00 00 00 00 07 00"

#define SERVICES_UUID            "23 20 56 7c 05 cf 6e b4 c3 41 77 28 51 82 7e 1b"
//#define CMD_PARA                 "hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 02 00 00 00 00 00 00 00 07 00"
//#define CMD_PARA                 "hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 01 00 00 00 00 00 00 00 07 00"
#define CMD_EN                   "hcitool -i hci0 cmd 0x08 0x000a 1"
#define CMD_DISEN                "hcitool -i hci0 cmd 0x08 0x000a 0"

static GDBusProxy *ble_proxy = NULL;

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

extern volatile bool BLE_FLAG;
extern struct adapter *default_ctrl;
extern DBusConnection *dbus_conn;
extern volatile GDBusProxy *ble_dev;

struct characteristic {
	char *service;
	char *uuid;
	char *path;
	uint8_t *value;
	int vlen;
	const char **props;
};

struct characteristic *gchr[GATT_MAX_CHR];
char gservice_path[512];

struct descriptor {
	struct characteristic *chr;
	char *uuid;
	char *path;
	uint8_t *value;
	int vlen;
	const char **props;
};

int gatt_set_on_adv(void);

/*
 * Supported properties are defined at doc/gatt-api.txt. See "Flags"
 * property of the GattCharacteristic1.
 */
static const char *ias_alert_level_props[] = { "read", "write", NULL };
static const char *chr_props[] = { "read", "write", "notify", NULL };
static const char *desc_props[] = { "read", "write", NULL };

static void chr_write(struct characteristic *chr, const uint8_t *value, int len);
static void chr_iface_destroy(gpointer user_data);

static gboolean desc_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct descriptor *desc = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &desc->uuid);

	return TRUE;
}

static gboolean desc_get_characteristic(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct descriptor *desc = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
						&desc->chr->path);

	return TRUE;
}

static bool desc_read(struct descriptor *desc, DBusMessageIter *iter)
{
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_BYTE_AS_STRING, &array);

	if (desc->vlen && desc->value)
		dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE,
						&desc->value, desc->vlen);

	dbus_message_iter_close_container(iter, &array);

	return true;
}

static gboolean desc_get_value(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct descriptor *desc = user_data;

	printf("Descriptor(%s): Get(\"Value\")\n", desc->uuid);

	return desc_read(desc, iter);
}

static void desc_write(struct descriptor *desc, const uint8_t *value, int len)
{
	g_free(desc->value);
	desc->value = g_memdup(value, len);
	desc->vlen = len;

	g_dbus_emit_property_changed(dbus_conn, desc->path,
					GATT_DESCRIPTOR_IFACE, "Value");
}

static int parse_value(DBusMessageIter *iter, const uint8_t **value, int *len)
{
	DBusMessageIter array;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return -EINVAL;

	dbus_message_iter_recurse(iter, &array);
	dbus_message_iter_get_fixed_array(&array, value, len);

	return 0;
}

static void desc_set_value(const GDBusPropertyTable *property,
				DBusMessageIter *iter,
				GDBusPendingPropertySet id, void *user_data)
{
	struct descriptor *desc = user_data;
	const uint8_t *value;
	int len;

	printf("Descriptor(%s): Set(\"Value\", ...)\n", desc->uuid);

	if (parse_value(iter, &value, &len)) {
		printf("Invalid value for Set('Value'...)\n");
		g_dbus_pending_property_error(id,
					ERROR_INTERFACE ".InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	desc_write(desc, value, len);

	g_dbus_pending_property_success(id);
}

static gboolean desc_get_props(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct descriptor *desc = data;
	DBusMessageIter array;
	int i;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_STRING_AS_STRING, &array);

	for (i = 0; desc->props[i]; i++)
		dbus_message_iter_append_basic(&array,
					DBUS_TYPE_STRING, &desc->props[i]);

	dbus_message_iter_close_container(iter, &array);

	return TRUE;
}

static const GDBusPropertyTable desc_properties[] = {
	{ "UUID",		"s", desc_get_uuid },
	{ "Characteristic",	"o", desc_get_characteristic },
	{ "Value",		"ay", desc_get_value, desc_set_value, NULL },
	{ "Flags",		"as", desc_get_props, NULL, NULL },
	{ }
};

static gboolean chr_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct characteristic *chr = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &chr->uuid);

	return TRUE;
}

static gboolean chr_get_service(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct characteristic *chr = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
							&chr->service);

	return TRUE;
}

static bool chr_read(struct characteristic *chr, DBusMessageIter *iter)
{
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_BYTE_AS_STRING, &array);

	dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE,
						&chr->value, chr->vlen);

	dbus_message_iter_close_container(iter, &array);

	return true;
}

static gboolean chr_get_value(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	struct characteristic *chr = user_data;

	printf("Characteristic(%s): Get(\"Value\")\n", chr->uuid);

	return chr_read(chr, iter);
}

static gboolean chr_get_props(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct characteristic *chr = data;
	DBusMessageIter array;
	int i;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_STRING_AS_STRING, &array);

	for (i = 0; chr->props[i]; i++)
		dbus_message_iter_append_basic(&array,
					DBUS_TYPE_STRING, &chr->props[i]);

	dbus_message_iter_close_container(iter, &array);

	return TRUE;
}

static void chr_write(struct characteristic *chr, const uint8_t *value, int len)
{
	g_free(chr->value);
	chr->value = g_memdup(value, len);
	chr->vlen = len;

	g_dbus_emit_property_changed(dbus_conn, chr->path, GATT_CHR_IFACE,
								"Value");
}

static void chr_set_value(const GDBusPropertyTable *property,
				DBusMessageIter *iter,
				GDBusPendingPropertySet id, void *user_data)
{
	struct characteristic *chr = user_data;
	const uint8_t *value;
	int len;

	printf("Characteristic(%s): Set('Value', ...)\n", chr->uuid);

	if (!parse_value(iter, &value, &len)) {
		printf("Invalid value for Set('Value'...)\n");
		g_dbus_pending_property_error(id,
					ERROR_INTERFACE ".InvalidArguments",
					"Invalid arguments in method call");
		return;
	}

	chr_write(chr, value, len);

	g_dbus_pending_property_success(id);
}

static const GDBusPropertyTable chr_properties[] = {
	{ "UUID",	"s", chr_get_uuid },
	{ "Service",	"o", chr_get_service },
	{ "Value",	"ay", chr_get_value, chr_set_value, NULL },
	{ "Flags",	"as", chr_get_props, NULL, NULL },
	{ }
};

static gboolean service_get_primary(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	dbus_bool_t primary = TRUE;

	printf("Get Primary: %s\n", primary ? "True" : "False");

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &primary);

	return TRUE;
}

static gboolean service_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	const char *uuid = user_data;

	printf("Get UUID: %s\n", uuid);

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &uuid);

	return TRUE;
}

static const GDBusPropertyTable service_properties[] = {
	{ "Primary", "b", service_get_primary },
	{ "UUID", "s", service_get_uuid },
	{ }
};

static void chr_iface_destroy(gpointer user_data)
{
	struct characteristic *chr = user_data;
	printf("== chr_iface_destroy ==\n");
	g_free(chr->uuid);
	g_free(chr->service);
	g_free(chr->value);
	g_free(chr->path);
	g_free(chr);
}

static void desc_iface_destroy(gpointer user_data)
{
	struct descriptor *desc = user_data;

	g_free(desc->uuid);
	g_free(desc->value);
	g_free(desc->path);
	g_free(desc);
}

static int parse_options(DBusMessageIter *iter, const char **device)
{
	DBusMessageIter dict;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
		return -EINVAL;

	dbus_message_iter_recurse(iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		const char *key;
		DBusMessageIter value, entry;
		int var;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		var = dbus_message_iter_get_arg_type(&value);
		if (strcasecmp(key, "device") == 0) {
			if (var != DBUS_TYPE_OBJECT_PATH)
				return -EINVAL;
			dbus_message_iter_get_basic(&value, device);
			printf("Device: %s\n", *device);
		}

		dbus_message_iter_next(&dict);
	}

	return 0;
}

static char ret_buff[1024];
static void execute(const char cmdline[], char recv_buff[], int len)
{
	printf("[GATT_CONFIG] execute: %s\n", cmdline);

	FILE *stream = NULL;
	char *tmp_buff = recv_buff;

	memset(recv_buff, 0, len);

	if ((stream = popen(cmdline, "r")) != NULL) {
		while (fgets(tmp_buff, len, stream)) {
			//printf("tmp_buf[%d]: %s\n", strlen(tmp_buff), tmp_buff);
			tmp_buff += strlen(tmp_buff);
			len -= strlen(tmp_buff);
			if (len <= 1)
				break;
		}
		printf("[GATT_CONFIG] execute_r: %s \n", recv_buff);
		pclose(stream);
	}
}

#define BLE_SEND_MAX_LEN (134)
static DBusMessage *chr_read_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chr = user_data;
	DBusMessage *reply;
	DBusMessageIter iter;
	const char *device;
	char str[512];
	printf("=== chr_read_value enter ===\n");

	if (!dbus_message_iter_init(msg, &iter))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, &device))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return g_dbus_create_error(msg, DBUS_ERROR_NO_MEMORY,
							"No Memory");

	dbus_message_iter_init_append(reply, &iter);

	ble_content_internal->cb_ble_request_data(chr->uuid);

	chr_read(chr, &iter);
	memcpy(str, chr->value, chr->vlen);
	str[chr->vlen] = '\0';
	printf("chr_read_value[%d]: %s\n", chr->vlen, str);
	printf("	dump 8 byte: ");
	for (int i = 0; i < min(chr->vlen, 8); i++)
		printf("0x%02x ", (chr->value)[i]);
	printf("\n");

	printf("=== chr_read_value exit ===\n");
	return reply;
}

static DBusMessage *chr_write_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	printf("=== chr_write_value enter ===\n");
	struct characteristic *chr = user_data;
	DBusMessageIter iter;
	const uint8_t *value;
	int len;
	const char *device;

	dbus_message_iter_init(msg, &iter);

	if (parse_value(&iter, &value, &len))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, &device))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	chr_write(chr, value, len);
	if(len == 0 || chr->value == NULL) {
		printf("chr_write_value is null\n");
		return dbus_message_new_method_return(msg);
	}

	if (ble_content_internal->cb_ble_recv_fun) {
		printf("cb_ble_recv_fun enter \n");
		ble_content_internal->cb_ble_recv_fun(chr->uuid, (char *)chr->value, len);
		printf("cb_ble_recv_fun exit \n");
	} else {
		printf("cb_ble_recv_fun is null !!! \n");
	}

	printf("=== chr_write_value exit ===\n");
	return dbus_message_new_method_return(msg);
}

static DBusMessage *chr_start_notify(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	return g_dbus_create_error(msg, DBUS_ERROR_NOT_SUPPORTED,
							"Not Supported");
}

static DBusMessage *chr_stop_notify(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	return g_dbus_create_error(msg, DBUS_ERROR_NOT_SUPPORTED,
							"Not Supported");
}

static const GDBusMethodTable chr_methods[] = {
	{ GDBUS_ASYNC_METHOD("ReadValue", GDBUS_ARGS({ "options", "a{sv}" }),
					GDBUS_ARGS({ "value", "ay" }),
					chr_read_value) },
	{ GDBUS_ASYNC_METHOD("WriteValue", GDBUS_ARGS({ "value", "ay" },
						{ "options", "a{sv}" }),
					NULL, chr_write_value) },
	{ GDBUS_ASYNC_METHOD("StartNotify", NULL, NULL, chr_start_notify) },
	{ GDBUS_METHOD("StopNotify", NULL, NULL, chr_stop_notify) },
	{ }
};

static DBusMessage *desc_read_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct descriptor *desc = user_data;
	DBusMessage *reply;
	DBusMessageIter iter;
	const char *device;

	if (!dbus_message_iter_init(msg, &iter))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, &device))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return g_dbus_create_error(msg, DBUS_ERROR_NO_MEMORY,
							"No Memory");

	dbus_message_iter_init_append(reply, &iter);

	desc_read(desc, &iter);

	return reply;
}

static DBusMessage *desc_write_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct descriptor *desc = user_data;
	DBusMessageIter iter;
	const char *device;
	const uint8_t *value;
	int len;

	if (!dbus_message_iter_init(msg, &iter))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_value(&iter, &value, &len))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	if (parse_options(&iter, &device))
		return g_dbus_create_error(msg, DBUS_ERROR_INVALID_ARGS,
							"Invalid arguments");

	desc_write(desc, value, len);

	return dbus_message_new_method_return(msg);
}

static const GDBusMethodTable desc_methods[] = {
	{ GDBUS_ASYNC_METHOD("ReadValue", GDBUS_ARGS({ "options", "a{sv}" }),
					GDBUS_ARGS({ "value", "ay" }),
					desc_read_value) },
	{ GDBUS_ASYNC_METHOD("WriteValue", GDBUS_ARGS({ "value", "ay" },
						{ "options", "a{sv}" }),
					NULL, desc_write_value) },
	{ }
};

static gboolean unregister_ble(void)
{
	int i;

	for (i = 0; i < ble_content_internal->char_cnt; i++) {
		printf("unregister_blechar_uuid[%d]: %s, gchr[i]->path: %s.\n", i, ble_content_internal->char_uuid[i], gchr[i]->path);
		g_dbus_unregister_interface(dbus_conn, gchr[i]->path, GATT_CHR_IFACE);
	}
	printf("unregister_ble gservice_path: %s.\n", gservice_path);
	g_dbus_unregister_interface(dbus_conn, gservice_path, GATT_SERVICE_IFACE);

	return TRUE;
}

static gboolean register_characteristic(const char *chr_uuid,
						const uint8_t *value, int vlen,
						const char **props,
						const char *desc_uuid,
						const char **desc_props,
						const char *service_path)
{
	struct characteristic *chr;
	struct descriptor *desc;
	static int id = 1;

	chr = g_new0(struct characteristic, 1);
	chr->uuid = g_strdup(chr_uuid);
	chr->value = g_memdup(value, vlen);
	chr->vlen = vlen;
	chr->props = props;
	chr->service = g_strdup(service_path);
	chr->path = g_strdup_printf("%s/characteristic%d", service_path, characteristic_id++);
	printf("register_characteristic chr->uuid: %s, chr->path: %s\n", chr->uuid, chr->path);
	if (!g_dbus_register_interface(dbus_conn, chr->path, GATT_CHR_IFACE,
					chr_methods, NULL, chr_properties,
					chr, chr_iface_destroy)) {
		printf("Couldn't register characteristic interface\n");
		chr_iface_destroy(chr);
		return FALSE;
	}

	gchr[gid++] = chr;

	if (!desc_uuid)
		return TRUE;

	desc = g_new0(struct descriptor, 1);
	desc->uuid = g_strdup(desc_uuid);
	desc->chr = chr;
	desc->props = desc_props;
	desc->path = g_strdup_printf("%s/descriptor%d", chr->path, characteristic_id++);

	if (!g_dbus_register_interface(dbus_conn, desc->path,
					GATT_DESCRIPTOR_IFACE,
					desc_methods, NULL, desc_properties,
					desc, desc_iface_destroy)) {
		printf("Couldn't register descriptor interface\n");
		g_dbus_unregister_interface(dbus_conn, chr->path,
							GATT_CHR_IFACE);

		desc_iface_destroy(desc);
		return FALSE;
	}

	return TRUE;
}

static char *register_service(const char *uuid)
{
	static int id = 1;
	char *path;

	path = g_strdup_printf("/service%d", service_id++);
	if (!g_dbus_register_interface(dbus_conn, path, GATT_SERVICE_IFACE,
				NULL, NULL, service_properties,
				g_strdup(uuid), g_free)) {
		printf("Couldn't register service interface\n");
		g_free(path);
		return NULL;
	}

	return path;
}

static void gatt_create_services(void)
{
	char *service_path;
	uint8_t level = ' ';
	int i;

	printf("server_uuid: %s\n", ble_content_internal->server_uuid);
	service_path = register_service(ble_content_internal->server_uuid);
	if (!service_path)
		return;

	strcpy(gservice_path, service_path);

	for (i = 0; i < ble_content_internal->char_cnt; i++) {
		printf("char_uuid[%d]: %s\n", i, ble_content_internal->char_uuid[i]);
		gboolean mcharacteristic = register_characteristic(ble_content_internal->char_uuid[i],
							&level, sizeof(level),
							chr_props,
							NULL,
							desc_props,
							service_path);
		/* Add Alert Level Characteristic to Immediate Alert Service */
		if (!mcharacteristic) {
			printf("Couldn't register characteristic.\n");
			g_dbus_unregister_interface(dbus_conn, service_path,
								GATT_SERVICE_IFACE);
			g_free(service_path);
			return;
		}
	}

	printf("Registered service: %s\n", service_path);
}

int gatt_write_data(char *uuid, void *data, int len)
{
	int i;
	struct characteristic *chr;

	if (!ble_dev) {
		printf("gatt_write_data: ble not connect!\n");
		return 0;
	}

	printf("gatt_write uuid: %s, len: [%d], data[%p]: %s\n", uuid, len, data, (char *)data);
	printf("	dump 8 byte: ");
	for (i = 0; i < min(len, 8); i++)
		printf("0x%02x ", ((char *)data)[i]);
	printf("\n");

	if (!gchr[0])
		while(1);

	for (i = 0; i < gid; i++) {
		printf("gatt_write[%d] uuid: %s\n", i, gchr[i]->uuid);
		if (strcmp(gchr[i]->uuid, uuid) == 0) {
			chr = gchr[i];
			break;
		}
	}

	if (chr == NULL) {
		printf("gatt_write invaild uuid: %s.\n", uuid);
		return -1;
	}

	chr_write(chr, data, len);
	return 0;
}

void ble_enable_adv(void)
{
	system("hciconfig hci0 piscan");
	system("hciconfig hci0 piscan");
	gatt_set_on_adv();
	execute(CMD_EN, ret_buff, 1024);
}

void ble_disable_adv(void)
{
	printf("=== ble_disable_adv ===\n");
	//g_dis_adv_close_ble = true;
	execute(CMD_DISEN, ret_buff, 1024);
	execute(CMD_DISEN, ret_buff, 1024);
}

int gatt_set_on_adv(void)
{
	char buff[1024] = {0};
	char CMD_ADV_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0008";
	char CMD_ADV_RESP_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0009";
	char temp[32];
	int i;

	if (!ble_content_internal->advDataLen)
		return -1;

	if (!ble_content_internal->respDataLen)
		return -1;

	//LE Set Random Address Command
	execute(CMD_RA, ret_buff, 1024);
	printf("CMD_RA buff: %s", ret_buff);
	sleep(1);
	//LE SET PARAMETERS
	execute(CMD_PARA, ret_buff, 1024);
	printf("CMD_PARA buff: %s", ret_buff);

	// LE Set Advertising Data Command
	memset(temp, 0, 32);
	for(i = 0; i < ble_content_internal->advDataLen; i++) {
		sprintf(temp,"%02x", ble_content_internal->advData[i]);
		strcat(CMD_ADV_DATA, " ");
		strcat(CMD_ADV_DATA, temp);
	}
	printf("CMD_ADV_DATA: %s\n", CMD_ADV_DATA);
	execute(CMD_ADV_DATA, ret_buff, 1024);

	memset(temp, 0, 32);
	for (i = 0; i < ble_content_internal->respDataLen; i++) {
		sprintf(temp, "%02x", ble_content_internal->respData[i]);
		strcat(CMD_ADV_RESP_DATA, " ");
		strcat(CMD_ADV_RESP_DATA, temp);
	}
	usleep(500000);
	printf("CMD_ADV_RESP_DATA: %s\n", CMD_ADV_RESP_DATA);
	execute(CMD_ADV_RESP_DATA, ret_buff, 1024);

	// LE Set Advertise Enable Command
	execute(CMD_EN, ret_buff, 1024);

	return 1;
}

static void register_app_reply(DBusMessage *reply, void *user_data)
{
	printf("register_app_reply\n");
	DBusError derr;

	dbus_error_init(&derr);
	dbus_set_error_from_message(&derr, reply);

	if (dbus_error_is_set(&derr))
		printf("RegisterApplication: %s\n", derr.message);
	else
		printf("RegisterApplication: OK\n");

	//send_advertise();
	//gatt_set_on_adv();

	dbus_error_free(&derr);
}

static void register_app_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = "/";
	DBusMessageIter dict;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	/* TODO: Add options dictionary */

	dbus_message_iter_close_container(iter, &dict);
}

void register_app(GDBusProxy *proxy)
{
	ble_proxy = proxy;

	if (!g_dbus_proxy_method_call(proxy, "RegisterApplication",
					register_app_setup, register_app_reply,
					NULL, NULL)) {
		printf("Unable to call RegisterApplication\n");
		return;
	}
}

static void unregister_app_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to unregister application: %s\n",
				error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	printf("Application unregistered\n");

	//return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void unregister_app_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = "/";

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

static void gatt_unregister_app(GDBusProxy *proxy)
{
	if (g_dbus_proxy_method_call(proxy, "UnregisterApplication",
						unregister_app_setup,
						unregister_app_reply, NULL,
						NULL) == FALSE) {
		printf("Failed unregister profile\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}
}

int gatt_setup(void)
{
	printf("gatt_setup\n");
	gatt_create_services();
	register_app(ble_proxy);

	return 1;
}

void gatt_cleanup(void)
{
	int i;

	printf("gatt_cleanup\n");

	//gatt_unregister_app(ble_proxy);

	sleep(1);

	for (i = 0; i < ble_content_internal->char_cnt; i++) {
		printf("char_uuid[%d]: %s\n", i,  gchr[i]->path);
		g_dbus_unregister_interface(dbus_conn,  gchr[i]->path,
							GATT_CHR_IFACE);
	}

	g_dbus_unregister_interface(dbus_conn, "/service1",
						GATT_SERVICE_IFACE);
}

int gatt_open(void)
{
	printf("=== gatt_open ===\n");
	ble_enable_adv();
	BLE_FLAG = true;

	return 1;
}

void gatt_close(void)
{
	printf("release_ble_gatt gatt_init ...\n");
	BLE_FLAG = false;
}

#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */
static void bt_gethostname(char *hostname_buf)
{
	char hostname[HOSTNAME_MAX_LEN + 1];
	size_t buf_len;

	buf_len = sizeof(hostname);
	if (gethostname(hostname, buf_len) != 0)
		printf("gethostname error !!!!!!!!\n");
	hostname[buf_len - 1] = '\0';

	/* Deny sending of these local hostnames */
	if (hostname[0] == '\0' || hostname[0] == '.' || strcmp(hostname, "(none)") == 0)
		printf("gethostname format error !!!\n");
	else
		printf("gethostname: %s, len: %d \n", hostname, strlen(hostname));

	strcpy(hostname_buf, hostname);
}

static int bt_string_to_uuid128(uuid128_t *uuid, const char *string, int rever)
{
	uint32_t data0, data4;
	uint16_t data1, data2, data3, data5;
	uuid128_t u128;
	uint8_t *val = (uint8_t *) &u128;
	uint8_t tmp[16];

	if (sscanf(string, "%08x-%04hx-%04hx-%04hx-%08x%04hx",
				&data0, &data1, &data2,
				&data3, &data4, &data5) != 6)
		return -EINVAL;

	data0 = htonl(data0);
	data1 = htons(data1);
	data2 = htons(data2);
	data3 = htons(data3);
	data4 = htonl(data4);
	data5 = htons(data5);

	memcpy(&val[0], &data0, 4);
	memcpy(&val[4], &data1, 2);
	memcpy(&val[6], &data2, 2);
	memcpy(&val[8], &data3, 2);
	memcpy(&val[10], &data4, 4);
	memcpy(&val[14], &data5, 2);

	if (rever) {
		memcpy(tmp, val, 16);
		printf("UUID: ");
		for (int i = 0; i < 16; i++) {
			val[15 - i] = tmp[i];
			printf("0x%x ", tmp[i]);
		}
		printf("\n");
	}

	//bt_uuid128_create(uuid, u128);
	memset(uuid, 0, sizeof(uuid128_t));
	memcpy(uuid, &u128, sizeof(uuid128_t));

	return 0;
}

static int ble_adv_set(RkBtContent *bt_content, ble_content_t *ble_content)
{
	char hostname[HOSTNAME_MAX_LEN + 1];
	int i, name_len, uuid_len;
	struct AdvDataContent advdata;
	struct AdvRespDataContent advdataresp;
	uuid128_t uuid;

	if (bt_content->ble_content.advDataType == BLE_ADVDATA_TYPE_USER) {
		if (bt_content->ble_content.advDataLen == 0) {
			printf("ERROR:Under the premise that advDataType is BLE_ADVDATA_TYPE_USER,"
				"the user must set the correct advData");
			return -1;
		}

		memcpy(ble_content->advData, bt_content->ble_content.advData, bt_content->ble_content.advDataLen);
		ble_content->advDataLen = bt_content->ble_content.advDataLen;
		if (bt_content->ble_content.respDataLen) {
			memcpy(ble_content->respData, bt_content->ble_content.respData, bt_content->ble_content.respDataLen);
			ble_content->respDataLen = bt_content->ble_content.respDataLen;
		}
	} else {
		advdata.adv_length = 0x15;
		advdata.flag_length = 2;
		advdata.flag = AD_FLAGS;
		advdata.flag_value = 0x1a;
		advdata.service_uuid_length = 0x10 + 1;
		advdata.service_uuid_flag = AD_COMPLETE_128_SERVICE_UUID;
		bt_string_to_uuid128(&(advdata.service_uuid_value), bt_content->ble_content.server_uuid.uuid, 1);
		ble_content->advDataLen = sizeof(struct AdvDataContent);
		memcpy(ble_content->advData, (uint8_t *)(&advdata), sizeof(struct AdvDataContent));

		//============================================================================
		if (bt_content->ble_content.ble_name) {
			name_len = strlen(bt_content->ble_content.ble_name);
			if (name_len > sizeof(advdataresp.local_name_value))
				name_len = sizeof(advdataresp.local_name_value);
			advdataresp.local_name_length = name_len + 1;
		} else {
			bt_gethostname(hostname);
			name_len = strlen(hostname);
			if (name_len > sizeof(advdataresp.local_name_value))
				name_len = sizeof(advdataresp.local_name_value);
			advdataresp.local_name_length = name_len + 1;
		}
		advdataresp.local_name_flag = AD_COMPLETE_LOCAL_NAME;
		advdataresp.adv_resp_length = advdataresp.local_name_length + 1;

		for (i = 0; i < name_len; i++) {
			if (bt_content->ble_content.ble_name)
				advdataresp.local_name_value[i] = bt_content->ble_content.ble_name[i];
			else
				advdataresp.local_name_value[i] = hostname[i];
		}

		ble_content->respDataLen = advdataresp.adv_resp_length + 1;
		memcpy(ble_content->respData, (uint8_t *)(&advdataresp), ble_content->respDataLen);
	}

	uuid_len = strlen(bt_content->ble_content.server_uuid.uuid);
	memcpy(ble_content->server_uuid, bt_content->ble_content.server_uuid.uuid, uuid_len);

	/* set chr uuid */
	for (i = 0; i < bt_content->ble_content.chr_cnt; i++)
		strcpy(ble_content->char_uuid[i], bt_content->ble_content.chr_uuid[i].uuid);

	ble_content->char_cnt = bt_content->ble_content.chr_cnt;
	ble_content->cb_ble_recv_fun = bt_content->ble_content.cb_ble_recv_fun;
	ble_content->cb_ble_request_data = bt_content->ble_content.cb_ble_request_data;
}

volatile unsigned short rk_gatt_mtu = 0;
unsigned int gatt_mtu(void)
{
	return (unsigned int)rk_gatt_mtu;
}

int gatt_init(RkBtContent *bt_content)
{
	//creat random address
	char temp_addr[256];
	int i;

	default_ctrl = NULL;
	characteristic_id = 1;
	service_id = 1;
	gid = 0;

	// random addr
	srand(time(NULL) + getpid() + getpid() * 987654 + rand());
	for(i = 0; i < 6;i++)
		 le_random_addr[i] = rand() & 0xFF;

	le_random_addr[5] &= 0x3f;		/* Clear two most significant bits */
	le_random_addr[5] |= 0xc0;		/* Set second most significant bit */
	for (i = 0; i < 6;i++) {
		sprintf(temp_addr, "%02x", le_random_addr[i]);
		strcat(CMD_RA, " ");
		strcat(CMD_RA, temp_addr);
	}

	//save random addr
	memcpy(bt_content->ble_content.le_random_addr, le_random_addr, sizeof(le_random_addr));
	//adv data set
	ble_adv_set(bt_content, &ble_content_internal_bak);
	printf("gatt_init server_uuid: %s\n", ble_content_internal_bak.server_uuid);
	ble_content_internal = &ble_content_internal_bak;
	gatt_create_services();

	return 1;
}
