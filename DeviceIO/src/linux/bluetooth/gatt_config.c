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

#include "gdbus/gdbus.h"

#include "error.h"

#define GATT_MGR_IFACE			"org.bluez.GattManager1"
#define GATT_SERVICE_IFACE		"org.bluez.GattService1"
#define GATT_CHR_IFACE			"org.bluez.GattCharacteristic1"
#define GATT_DESCRIPTOR_IFACE		"org.bluez.GattDescriptor1"

/* Immediate wifi Service UUID */
#define WIFI_SERVICES_UUID       "1B7E8251-2877-41C3-B46E-CF057C562023"
#define SECURITY_CHAR_UUID       "CAC2ABA4-EDBB-4C4A-BBAF-0A84A5CD93A1"
#define HIDE_CHAR_UUID           "CAC2ABA4-EDBB-4C4A-BBAF-0A84A5CD26C7"
#define SSID_CHAR_UUID           "ACA0EF7C-EEAA-48AD-9508-19A6CEF6B356"
#define PASSWORD_CHAR_UUID       "40B7DE33-93E4-4C8B-A876-D833B415A6CE"
#define CHECKDATA_CHAR_UUID      "40B7DE33-93E4-4C8B-A876-D833B415C759"
#define NOTIFY_CHAR_UUID         "8AC32D3f-5CB9-4D44-BEC2-EE689169F626"
#define NOTIFY_DESC_UUID         "00002902-0000-1000-8000-00805f9b34fb"
#define WIFILIST_CHAR_UUID       "8AC32D3f-5CB9-4D44-BEC2-EE689169F627"
#define DEVICECONTEXT_CHAR_UUID  "8AC32D3f-5CB9-4D44-BEC2-EE689169F628"

//
#define BLE_UUID_SERVICE	"0000180A-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_WIFI_CHAR	"00009999-0000-1000-8000-00805F9B34FB"

#define BT_NAME                  "Yami"
static char reconnect_path[66];
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
static volatile bool g_dis_adv_close_ble = false;
#define min(x, y) ((x) < (y) ? (x) : (y))

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
GDBusClient *client;
guint signals;
static volatile bool BLE_FLAG = true;

#define SERVICES_UUID            "23 20 56 7c 05 cf 6e b4 c3 41 77 28 51 82 7e 1b"
//#define CMD_PARA                 "hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 02 00 00 00 00 00 00 00 07 00"
#define CMD_PARA                 "hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 01 00 00 00 00 00 00 00 07 00"
#define CMD_EN                   "hcitool -i hci0 cmd 0x08 0x000a 1"                 
#define CMD_DISEN                "hcitool -i hci0 cmd 0x08 0x000a 0"

static GMainLoop *main_loop;
static GSList *services;
static DBusConnection *connection;

struct characteristic *temp_chr;
struct characteristic *ble_char_chr;

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

	g_dbus_emit_property_changed(connection, desc->path,
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

	g_dbus_emit_property_changed(connection, chr->path, GATT_CHR_IFACE,
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

static gboolean service_get_includes(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *user_data)
{
	const char *uuid = user_data;
	char service_path[100] = {0,};
	DBusMessageIter array;
	char *p = NULL;

	snprintf(service_path, 100, "/service3");
	printf("Get Includes: %s\n", uuid);

	p = service_path;

	printf("Includes path: %s\n", p);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
			DBUS_TYPE_OBJECT_PATH_AS_STRING, &array);

	dbus_message_iter_append_basic(&array, DBUS_TYPE_OBJECT_PATH,
							&p);

	snprintf(service_path, 100, "/service2");
	p = service_path;
	printf("Get Includes: %s\n", p);

	dbus_message_iter_append_basic(&array, DBUS_TYPE_OBJECT_PATH,
							&p);
	dbus_message_iter_close_container(iter, &array);


	return TRUE;

}


static gboolean service_exist_includes(const GDBusPropertyTable *property,
							void *user_data)
{
	const char *uuid = user_data;

	printf("Exist Includes: %s\n", uuid);

#ifdef DUEROS
	if (strncmp(uuid, "00001111", 8) == 0)
		return TRUE;
#else
	if (strncmp(uuid, "1B7E8251", 8) == 0)
		return TRUE;
#endif

	return FALSE;
}

static const GDBusPropertyTable service_properties[] = {
	{ "Primary", "b", service_get_primary },
	{ "UUID", "s", service_get_uuid },
	//{ "Includes", "ao", service_get_includes, NULL,
	//				service_exist_includes },
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

void execute(const char cmdline[], char recv_buff[])
{
    printf("consule_run: %s\n",cmdline);

    FILE *stream = NULL;
    char buff[1024];

    memset(recv_buff, 0, sizeof(recv_buff));

    if((stream = popen(cmdline,"r"))!=NULL){
        while(fgets(buff,1024,stream)){
            strcat(recv_buff,buff);
        }
    }

    pclose(stream);
}

#define BLE_SEND_MAX_LEN (134) //(20) //(512)
static DBusMessage *chr_read_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	struct characteristic *chr = user_data;
	DBusMessage *reply;
	DBusMessageIter iter;
	const char *device;
    static char *slist = NULL;
    static char *devicesn = NULL;
	char str[512];

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

	if (!strcmp(WIFILIST_CHAR_UUID, chr->uuid))
		ble_content_internal->cb_ble_request_data(WIFILIST_CHAR_UUID);
	if (!strcmp(DEVICECONTEXT_CHAR_UUID, chr->uuid))
		ble_content_internal->cb_ble_request_data(DEVICECONTEXT_CHAR_UUID);

	chr_read(chr, &iter);
	memcpy(str, chr->value, chr->vlen);
    str[chr->vlen] = '\0';
    printf("chr_read_value[%d]: %s\n", chr->vlen, str);
	printf("	dump 8 byte: ");
	for (int i = 0; i < min(chr->vlen, 8); i++)
		printf("0x%02x ", (chr->value)[i]);
	printf("\n");

	return reply;
}

static DBusMessage *chr_write_value(DBusConnection *conn, DBusMessage *msg,
							void *user_data)
{
	printf("chr_write_value v0.3\n");
	struct characteristic *chr = user_data;
	DBusMessageIter iter;
	const uint8_t *value;
	int len;
	const char *device;
	//char str[120];
	//memset(str, 0, 120);

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

	/*
	memcpy(str, chr->value, len);
	str[len] = '\0';
	printf("chr_write_value  %p, %d\n", str, len);
	*/

	ble_content_internal->cb_ble_recv_fun(chr->uuid, chr->value, len);

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
		g_dbus_unregister_interface(connection, gchr[i]->path, GATT_CHR_IFACE);
	}
	printf("unregister_ble gservice_path: %s.\n", gservice_path);
	g_dbus_unregister_interface(connection, gservice_path, GATT_SERVICE_IFACE);
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
	if (!g_dbus_register_interface(connection, chr->path, GATT_CHR_IFACE,
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

	if (!g_dbus_register_interface(connection, desc->path,
					GATT_DESCRIPTOR_IFACE,
					desc_methods, NULL, desc_properties,
					desc, desc_iface_destroy)) {
		printf("Couldn't register descriptor interface\n");
		g_dbus_unregister_interface(connection, chr->path,
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
	if (!g_dbus_register_interface(connection, path, GATT_SERVICE_IFACE,
				NULL, NULL, service_properties,
				g_strdup(uuid), g_free)) {
		printf("Couldn't register service interface\n");
		g_free(path);
		return NULL;
	}

	return path;
}

static void create_wifi_services(void)
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
			g_dbus_unregister_interface(connection, service_path,
								GATT_SERVICE_IFACE);
			g_free(service_path);
			return;
		}
	}

	services = g_slist_prepend(services, service_path);
	printf("Registered service: %s\n", service_path);
}

int gatt_write_data(char *uuid, void *data, int len)
{
	int i;
	struct characteristic *chr;

	printf("gatt_write uuid: %s, len: [%d], data[%p]: %s\n", uuid, len, data, data);
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
	char buff[1024] = {0};
	system("hciconfig hci0 piscan");
	system("hciconfig hci0 piscan");	
	gatt_set_on_adv();
	execute(CMD_EN, buff);
}

void ble_disable_adv(void)
{
	char buff[1024] = {0};
	//g_dis_adv_close_ble = true;
	execute(CMD_DISEN, buff);
}

int gatt_set_on_adv(void)
{
	char buff[1024] = {0};
	char CMD_ADV_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0008";
	char CMD_ADV_RESP_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0009";
	char temp[32];
	int i;

	//creat random address
	char temp_addr[256];

	if (!ble_content_internal->advDataLen)
		return -1;

	if (!ble_content_internal->respDataLen)
		return -1;

	srand(time(NULL) + getpid() + getpid() * 987654 + rand());
	for(i = 0; i < 6;i++)
		 le_random_addr[i] = rand() & 0xFF;

	le_random_addr[0] &= 0x3f;		/* Clear two most significant bits */
	le_random_addr[0] |= 0xc0;		/* Set second most significant bit */
	for (i = 0; i < 6;i++) {
		sprintf(temp_addr, "%02x", le_random_addr[i]);
		strcat(CMD_RA, " ");
		strcat(CMD_RA, temp_addr);
	}
	//LE Set Random Address Command
	execute(CMD_RA, buff);

	//LE SET PARAMETERS
	execute(CMD_PARA, buff);

    // LE Set Advertising Data Command
	memset(temp, 0, 32);
	for(i = 0; i < ble_content_internal->advDataLen; i++) {
		sprintf(temp,"%02x", ble_content_internal->advData[i]);
		strcat(CMD_ADV_DATA, " ");
		strcat(CMD_ADV_DATA, temp);
	}
	printf("CMD_ADV_DATA: %s\n", CMD_ADV_DATA);
    execute(CMD_ADV_DATA, buff);

	memset(temp, 0, 32);
	for (i = 0; i < ble_content_internal->respDataLen; i++) {
		sprintf(temp, "%02x", ble_content_internal->respData[i]);
		strcat(CMD_ADV_RESP_DATA, " ");
		strcat(CMD_ADV_RESP_DATA, temp);
	}
    usleep(500000);
	printf("CMD_ADV_RESP_DATA: %s\n", CMD_ADV_RESP_DATA);	
    execute(CMD_ADV_RESP_DATA, buff);

    // LE Set Advertise Enable Command
    execute(CMD_EN, buff);
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
	gatt_set_on_adv();

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

static void register_app(GDBusProxy *proxy)
{
	if (!g_dbus_proxy_method_call(proxy, "RegisterApplication",
					register_app_setup, register_app_reply,
					NULL, NULL)) {
		printf("Unable to call RegisterApplication\n");
		return;
	}
}

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

static struct adapter *default_ctrl;
static GList *ctrl_list;
static GDBusProxy *default_dev;
static GDBusProxy *default_attr;

#undef bt_shell_printf
#define bt_shell_printf printf

static void print_fixed_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t *valbool;
	dbus_uint32_t *valu32;
	dbus_uint16_t *valu16;
	dbus_int16_t *vals16;
	unsigned char *byte;
	int len;

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_fixed_array(iter, &valbool, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)valbool, len * sizeof(*valbool));

		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_fixed_array(iter, &valu32, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)valu32, len * sizeof(*valu32));

		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_fixed_array(iter, &valu16, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)valu16, len * sizeof(*valu16));

		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_fixed_array(iter, &vals16, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)vals16, len * sizeof(*vals16));

		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_fixed_array(iter, &byte, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)byte, len * sizeof(*byte));

		break;
	default:
		return;
	};
}

static void print_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
	unsigned char byte;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;

	if (iter == NULL) {
		bt_shell_printf("%s%s is nil\n", label, name);
		return;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		bt_shell_printf("%s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		bt_shell_printf("%s%s: %s\n", label, name, valstr);
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		bt_shell_printf("%s%s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
		if ((!strncmp(name, "ServicesResolved", 16)) &&
			(valbool == TRUE)) {
			printf("=== BLE CONNECTED ===\n");
		}

		if ((!strcmp(name, "Connected"))  &&
			(valbool != TRUE)) {
				printf("=== BLE DISCONNECTED ===\n");
				sleep(1);
				gatt_set_on_adv();
		}     
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		bt_shell_printf("%s%s: 0x%08x\n", label, name, valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		bt_shell_printf("%s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		bt_shell_printf("%s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		bt_shell_printf("%s%s: 0x%02x\n", label, name, byte);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);

		if (dbus_type_is_fixed(
				dbus_message_iter_get_arg_type(&subiter))) {
			print_fixed_iter(label, name, &subiter);
			break;
		}

		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		print_iter(label, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		print_iter(label, entry, &subiter);
		g_free(entry);
		break;
	default:
		bt_shell_printf("%s%s has unsupported type\n", label, name);
		break;
	}
}

static struct adapter *find_ctrl(GList *source, const char *path)
{
        GList *list;

        for (list = g_list_first(source); list; list = g_list_next(list)) {
                struct adapter *adapter = list->data;

                if (!strcasecmp(g_dbus_proxy_get_path(adapter->proxy), path))
                        return adapter;
        }

        return NULL;
}

static void set_default_device(GDBusProxy *proxy, const char *attribute)
{
	char *desc = NULL;
	DBusMessageIter iter;
	const char *path;

	default_dev = proxy;

	if (proxy == NULL) {
		default_attr = NULL;
		goto done;
	}

	if (!g_dbus_proxy_get_property(proxy, "Alias", &iter)) {
		if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
			goto done;
	}

	path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(&iter, &desc);
	desc = g_strdup_printf("[%s%s%s]# ", desc,
				attribute ? ":" : "",
				attribute ? attribute + strlen(path) : "");

done:
	g_free(desc);
}

static gboolean device_is_child(GDBusProxy *device, GDBusProxy *master)
{
	DBusMessageIter iter;
	const char *adapter, *path;

	if (!master)
		return FALSE;

	if (g_dbus_proxy_get_property(device, "Adapter", &iter) == FALSE)
		return FALSE;

	dbus_message_iter_get_basic(&iter, &adapter);
	path = g_dbus_proxy_get_path(master);

	if (!strcmp(path, adapter))
		return TRUE;

	return FALSE;
}
static struct adapter *adapter_new(GDBusProxy *proxy)
{
	struct adapter *adapter = g_malloc0(sizeof(struct adapter));

	ctrl_list = g_list_append(ctrl_list, adapter);

	if (!default_ctrl)
		default_ctrl = adapter;

	return adapter;
}

static void proxy_added_cb(GDBusProxy *proxy, void *user_data)
{
	const char *iface;

	iface = g_dbus_proxy_get_interface(proxy);

	printf("GATT: proxy_added_cb: %s, BLE_FLAG: %d \n", iface, BLE_FLAG);

	if (!BLE_FLAG)
		return;

	if (!strcmp(iface, "org.bluez.Adapter1")) {
		struct adapter *adapter;
		adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
		if (!adapter)
			adapter = adapter_new(proxy);
		adapter->proxy = proxy;
	}

	if (g_strcmp0(iface, GATT_MGR_IFACE))
		return;

	register_app(proxy);
}
static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;
	struct adapter *ctrl;

	interface = g_dbus_proxy_get_interface(proxy);
	printf("Gatt: property_changed: %s, BLE_FLAG: %d\n", interface, BLE_FLAG);

	if (!BLE_FLAG)
		return;

	if (!strcmp(interface, "org.bluez.Device1")) {
        if (default_ctrl != NULL)
			printf("Gatt : default_ctrl is not null\n");
		if (default_ctrl && device_is_child(proxy,
					default_ctrl->proxy) == TRUE) {
			DBusMessageIter addr_iter;
			char *str;

			if (g_dbus_proxy_get_property(proxy, "AddressType",
										&addr_iter) == TRUE) {
				const char *addressType;
				dbus_message_iter_get_basic(&addr_iter, &addressType);
				printf("Gatt: addressType: %s\n", addressType);
				if (strcmp(addressType, "public") == 0)
					return 0;
			}

			if (g_dbus_proxy_get_property(proxy, "Address",
							&addr_iter) == TRUE) {
				const char *address;
				dbus_message_iter_get_basic(&addr_iter,&address);
				str = g_strdup_printf("[CHG] Device: %s ", address);                     
			} else
				str = g_strdup("");
                        
			if (strcmp(name, "Connected") == 0) {
				dbus_bool_t connected;

				dbus_message_iter_get_basic(iter, &connected);

				if (connected && default_dev == NULL) {
					set_default_device(proxy, NULL);
				} else if (!connected && default_dev == proxy) {
					set_default_device(NULL, NULL);
				}
			}

			print_iter(str, name, iter);
			g_free(str);
		}
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		DBusMessageIter addr_iter;
		char *str;

		if (g_dbus_proxy_get_property(proxy, "Address",
						&addr_iter) == TRUE) {
			const char *address;

			dbus_message_iter_get_basic(&addr_iter, &address);
			str = g_strdup_printf("[CHG] Controller %s ", address);
		} else
			str = g_strdup("");

		print_iter(str, name, iter);
		g_free(str);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		DBusMessageIter addr_iter;
		char *str;

		ctrl = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
		if (!ctrl)
			return;

		if (g_dbus_proxy_get_property(ctrl->proxy, "Address",
						&addr_iter) == TRUE) {
			const char *address;

			dbus_message_iter_get_basic(&addr_iter, &address);
			str = g_strdup_printf("[CHG] Controller %s ", address);
		} else
			str = g_strdup("");

		print_iter(str, name, iter);
		g_free(str);
	} else if (proxy == default_attr) {
		char *str;

		str = g_strdup_printf("[CHG] Attribute %s ",
						g_dbus_proxy_get_path(proxy));

		print_iter(str, name, iter);
		g_free(str);
	}
}

static gboolean signal_handler(GIOChannel *channel, GIOCondition cond,
							gpointer user_data)
{
	static bool __terminated = false;
	struct signalfd_siginfo si;
	ssize_t result;
	int fd;

	if (cond & (G_IO_NVAL | G_IO_ERR | G_IO_HUP))
		return FALSE;

	fd = g_io_channel_unix_get_fd(channel);

	result = read(fd, &si, sizeof(si));
	if (result != sizeof(si))
		return FALSE;

	switch (si.ssi_signo) {
	case SIGINT:
	case SIGTERM:
		if (!__terminated) {
			printf("Terminating\n");
			g_main_loop_quit(main_loop);
		}

		__terminated = true;
		break;
	}

	return TRUE;
}

static guint setup_signalfd(void)
{
	GIOChannel *channel;
	guint source;
	sigset_t mask;
	int fd;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
		perror("Failed to set signal mask");
		return 0;
	}

	fd = signalfd(-1, &mask, 0);
	if (fd < 0) {
		perror("Failed to create signal descriptor");
		return 0;
	}

	channel = g_io_channel_unix_new(fd);

	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);

	source = g_io_add_watch(channel,
				G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
				signal_handler, NULL);

	g_io_channel_unref(channel);

	return source;
}

static pthread_t p_gatt_init = 0;
int gatt_init(ble_content_t *ble_content);
int gatt_main(ble_content_t *ble_content)
{
	printf("=== gatt_init p_gatt_init: 0x%x===\n", p_gatt_init);
	if (p_gatt_init) {
		ble_enable_adv();
		system("hciconfig hci0 piscan");
		BLE_FLAG = true;
		return 1;
	}

	pthread_create(&p_gatt_init, NULL, gatt_init, ble_content);
	return 1;
}

int gatt_init(ble_content_t *ble_content)
{
	default_ctrl = NULL;
	ctrl_list = NULL;
	default_dev = NULL;
	default_attr = NULL;
	characteristic_id = 1;
	service_id = 1;
	gid = 0;

	signals = setup_signalfd();
	if (signals == 0)
		return -errno;

	ble_content_internal_bak = *ble_content;
	ble_content_internal = &ble_content_internal_bak;
	connection = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);

	main_loop = g_main_loop_new(NULL, FALSE);

	g_dbus_attach_object_manager(connection);

	printf("gatt-service unique name: %s\n",
				dbus_bus_get_unique_name(connection));

	create_wifi_services();

	client = g_dbus_client_new(connection, "org.bluez", "/");

	printf("Gatt: gatt_init BLE_FLAG: %d\n", BLE_FLAG);
	BLE_FLAG = true;
	g_dbus_client_set_proxy_handlers(client, proxy_added_cb, NULL, property_changed,
									NULL);

	g_main_loop_run(main_loop);
	printf("exit gatt_init ...\n");
	//g_dbus_client_unref(client);	
	unregister_ble();
	//sleep(2);
	g_source_remove(signals);
	g_slist_free_full(services, g_free);
	dbus_connection_unref(connection);
	default_ctrl = NULL;
	ctrl_list = NULL;
	default_dev = NULL;
	default_attr = NULL;
	printf("exit gatt_init ok \n");

	pthread_exit(0);
	printf("exit gatt_init end \n");

	return 0;
}

void release_ble_gatt(void)
{
	printf("release_ble_gatt gatt_init ...\n");
	//g_main_loop_quit(main_loop);
	BLE_FLAG = false;
}
