#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdbool.h>
#include <wordexp.h>
#include <sys/un.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <linux/input.h>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>

#include "DeviceIo/DeviceIo.h"
#include "DeviceIo/Rk_shell.h"

#include "a2dp_masterctrl.h"
#include "../gdbus/gdbus.h"
#include "shell.h"
#include "util.h"
#include "agent.h"
#include "gatt.h"
#include "advertising.h"
#include "../bluez_ctrl.h"
#include "../gatt_config.h"
//#include "../include/uinput.h"
#include "utility.h"
#include "slog.h"

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::BT_Device_Class;

/* String display constants */
#define COLORED_NEW COLOR_GREEN "NEW" COLOR_OFF
#define COLORED_CHG COLOR_YELLOW "CHG" COLOR_OFF
#define COLORED_DEL COLOR_RED "DEL" COLOR_OFF

#define PROMPT_ON   COLOR_BLUE "[bluetooth]" COLOR_OFF "# "
#define PROMPT_OFF  "Waiting to connect to bluetoothd..."

#define DISTANCE_VAL_INVALID    0x7FFF

DBusConnection *dbus_conn;
static GDBusProxy *agent_manager;
static char *auto_register_agent = NULL;
static RkBtContent *g_bt_content = NULL;

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

typedef struct {
	RK_BT_STATE_CALLBACK bt_state_cb;
	RK_BT_BOND_CALLBACK bt_bond_state_cb;
	RK_BT_DISCOVERY_CALLBACK bt_decovery_cb;
	RK_BT_DEV_FOUND_CALLBACK bt_dev_found_cb;
	RK_BT_SOURCE_CALLBACK bt_source_event_cb;
	RK_BLE_STATE_CALLBACK ble_state_cb;
} bt_callback_t;

struct adapter *default_ctrl;
static GDBusProxy *default_dev;
static GDBusProxy *default_src_dev = NULL;

static GDBusProxy *default_attr;
GList *ctrl_list;

volatile GDBusProxy *ble_dev = NULL;

GDBusClient *btsrc_client;
static GMainLoop *btsrc_main_loop;
/* For scan cmd */
#define BTSRC_SCAN_PROFILE_INVALID 0
#define BTSRC_SCAN_PROFILE_SOURCE  1
#define BTSRC_SCAN_PROFILE_SINK    2
/* For connect cmd */
#define BTSRC_CONNECT_IDLE   0
#define BTSRC_CONNECT_DOING  1
#define BTSRC_CONNECT_SUCESS 2
#define BTSRC_CONNECT_FAILED 3

volatile bool A2DP_SINK_FLAG;
volatile bool A2DP_SRC_FLAG;
volatile bool BLE_FLAG = 0;
volatile bool BT_OPENED = 0;

void *g_btmaster_userdata = NULL;
RK_BLE_STATE g_ble_state;
static bool g_device_discovering = false;
static pthread_t g_scan_thread = 0;
static unsigned int g_scan_time = 0;

static bt_callback_t g_bt_callback = {
	NULL, NULL, NULL, NULL, NULL, NULL,
};

#ifdef __cplusplus
extern "C" {
#endif

void register_app(GDBusProxy *proxy);
int gatt_set_on_adv(void);
void ble_wifi_clean(void);

#ifdef __cplusplus
}
#endif

extern void a2dp_sink_proxy_removed(GDBusProxy *proxy, void *user_data);
extern void a2dp_sink_proxy_added(GDBusProxy *proxy, void *user_data);
extern void a2dp_sink_property_changed(GDBusProxy *proxy, const char *name, DBusMessageIter *iter, void *user_data);
extern void adapter_changed(GDBusProxy *proxy, DBusMessageIter *iter, void *user_data);
extern void device_changed(GDBusProxy *proxy, DBusMessageIter *iter, void *user_data);
extern int init_avrcp_ctrl(void);

static volatile int ble_service_cnt = 0;

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

extern void report_avrcp_event(DeviceInput event, void *data, int len);

static const char *agent_arguments[] = {
	"on",
	"off",
	"DisplayOnly",
	"DisplayYesNo",
	"KeyboardDisplay",
	"KeyboardOnly",
	"NoInputNoOutput",
	NULL
};

static const char *ad_arguments[] = {
	"on",
	"off",
	"peripheral",
	"broadcast",
	NULL
};

#define BT_RECONNECT_CFG "/data/cfg/lib/bluetooth/reconnect_cfg"

static int a2dp_master_save_status(char *address);
static int load_last_device(char *address);
static void save_last_device(GDBusProxy *proxy);
static int bt_get_device_name_by_proxy(GDBusProxy *proxy,
			char *name_buf, int name_len);
static int bt_get_device_addr_by_proxy(GDBusProxy *proxy,
			char *addr_buf, int addr_len);

static void bt_bond_state_send(const char *bd_addr, const char *name, RK_BT_BOND_STATE state)
{
	if(g_bt_callback.bt_bond_state_cb)
		g_bt_callback.bt_bond_state_cb(bd_addr, name, state);
}

void bt_register_bond_callback(RK_BT_BOND_CALLBACK cb)
{
	g_bt_callback.bt_bond_state_cb = cb;
}

void bt_deregister_bond_callback()
{
	g_bt_callback.bt_bond_state_cb = NULL;
}

void bt_state_send(RK_BT_STATE state)
{
	if(g_bt_callback.bt_state_cb)
		g_bt_callback.bt_state_cb(state);
}

void bt_register_state_callback(RK_BT_STATE_CALLBACK cb)
{
	g_bt_callback.bt_state_cb = cb;
}

void bt_deregister_state_callback()
{
	g_bt_callback.bt_state_cb = NULL;
}

void ble_state_send(RK_BLE_STATE status)
{
	if(g_bt_callback.ble_state_cb)
		g_bt_callback.ble_state_cb(status);

	g_ble_state = status;
}

void ble_get_state(RK_BLE_STATE *p_state)
{
	if (!p_state)
		return;

	*p_state = g_ble_state;
}

void ble_register_state_callback(RK_BLE_STATE_CALLBACK cb)
{
	g_bt_callback.ble_state_cb = cb;
}

void ble_deregister_state_callback()
{
	g_bt_callback.ble_state_cb = NULL;
}

static void bt_discovery_state_send(RK_BT_DISCOVERY_STATE state)
{
	if(g_bt_callback.bt_decovery_cb)
		g_bt_callback.bt_decovery_cb(state);
}

void bt_register_discovery_callback(RK_BT_DISCOVERY_CALLBACK cb)
{
	g_bt_callback.bt_decovery_cb = cb;
}

void bt_deregister_discovery_callback()
{
	g_bt_callback.bt_decovery_cb = NULL;
}

static void bt_dev_found_send(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	dbus_uint32_t bt_class = 0;
	const char *address, *name;
	short rssi = DISTANCE_VAL_INVALID;

	if(!g_device_discovering || !g_bt_callback.bt_dev_found_cb)
		return;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter))
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "unknown";

	if(g_dbus_proxy_get_property(proxy, "Class", &iter))
		dbus_message_iter_get_basic(&iter, &bt_class);

	if (g_dbus_proxy_get_property(proxy, "RSSI", &iter))
		dbus_message_iter_get_basic(&iter, &rssi);

	g_bt_callback.bt_dev_found_cb(address, name, bt_class, rssi);
}

void bt_register_dev_found_callback(RK_BT_DEV_FOUND_CALLBACK cb)
{
	g_bt_callback.bt_dev_found_cb = cb;
}

void bt_deregister_dev_found_callback()
{
	g_bt_callback.bt_dev_found_cb = NULL;
}

static void proxy_leak(gpointer data)
{
	pr_info("Leaking proxy %p\n", data);
}

static void connect_handler(DBusConnection *connection, void *user_data)
{
	bt_shell_set_prompt(PROMPT_ON);
}

static void disconnect_handler(DBusConnection *connection, void *user_data)
{
	//bt_shell_detach();

	//bt_shell_set_prompt(PROMPT_OFF);

	g_list_free_full(ctrl_list, proxy_leak);
	ctrl_list = NULL;

	default_ctrl = NULL;
}

static void print_adapter(GDBusProxy *proxy, const char *description)
{
	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	pr_info("%s%s%sController %s %s %s\n",
				description ? "[" : "",
				description ? : "",
				description ? "] " : "",
				address, name,
				default_ctrl &&
				default_ctrl->proxy == proxy ?
				"[default]" : "");

}

static void btsrc_scan_save_device(GDBusProxy *proxy, BtScanParam *param)
{
	DBusMessageIter iter;
	const char *address, *name;
	BtDeviceInfo *device_info = NULL;
	size_t cplen = 0;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	if (param && (param->item_cnt < BT_SOURCE_SCAN_DEVICES_CNT)) {
		device_info = &(param->devices[param->item_cnt]);
		memset(device_info, 0, sizeof(BtDeviceInfo));
		cplen = sizeof(device_info->name);
		cplen = (strlen(name) > cplen) ? cplen : strlen(name);
		memcpy(device_info->name, name, cplen);
		memcpy(device_info->address, address, sizeof(device_info->address));
		param->item_cnt++;
	}
}

static void print_device(GDBusProxy *proxy, const char *description)
{
	DBusMessageIter iter;
	const char *address, *name;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	pr_info("%s%s%sDevice %s %s\n",
				description ? "[" : "",
				description ? : "",
				description ? "] " : "",
				address, name);
}

void print_fixed_iter(const char *label, const char *name,
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

		pr_info("%s%s:\n", label, name);
		bt_shell_hexdump((void *)valbool, len * sizeof(*valbool));

		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_fixed_array(iter, &valu32, &len);

		if (len <= 0)
			return;

		pr_info("%s%s:\n", label, name);
		bt_shell_hexdump((void *)valu32, len * sizeof(*valu32));

		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_fixed_array(iter, &valu16, &len);

		if (len <= 0)
			return;

		pr_info("%s%s:\n", label, name);
		bt_shell_hexdump((void *)valu16, len * sizeof(*valu16));

		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_fixed_array(iter, &vals16, &len);

		if (len <= 0)
			return;

		pr_info("%s%s:\n", label, name);
		bt_shell_hexdump((void *)vals16, len * sizeof(*vals16));

		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_fixed_array(iter, &byte, &len);

		if (len <= 0)
			return;

		pr_info("%s%s:\n", label, name);
		bt_shell_hexdump((void *)byte, len * sizeof(*byte));

		break;
	default:
		return;
	};
}

void print_iter(const char *label, const char *name,
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
		pr_info("%s%s is nil\n", label, name);
		return;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		pr_info("%s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		pr_info("%s%s: %s\n", label, name, valstr);
		if (!strncmp(name, "Status", 6)) {
			if (strstr(valstr, "playing"))
				report_avrcp_event(DeviceInput::BT_START_PLAY, NULL, 0);
			else if (strstr(valstr, "paused"))
				report_avrcp_event(DeviceInput::BT_PAUSE_PLAY, NULL, 0);
			else if (strstr(valstr, "stopped"))
				report_avrcp_event(DeviceInput::BT_STOP_PLAY, NULL, 0);
		}
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		pr_info("%s%s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		pr_info("%s%s: 0x%08x\n", label, name, valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		pr_info("%s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		pr_info("%s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		pr_info("%s%s: 0x%02x\n", label, name, byte);
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
		pr_info("%s%s has unsupported type\n", label, name);
		break;
	}
}

void print_property(GDBusProxy *proxy, const char *name)
{
	DBusMessageIter iter;

	if (g_dbus_proxy_get_property(proxy, name, &iter) == FALSE)
		return;

	print_iter("\t", name, &iter);
}

static void print_uuid(const char *uuid)
{
	const char *text;

	text = bt_uuidstr_to_str(uuid);
	if (text) {
		char str[26];
		unsigned int n;

		str[sizeof(str) - 1] = '\0';

		n = snprintf(str, sizeof(str), "%s", text);
		if (n > sizeof(str) - 1) {
			str[sizeof(str) - 2] = '.';
			str[sizeof(str) - 3] = '.';
			if (str[sizeof(str) - 4] == ' ')
				str[sizeof(str) - 4] = '.';

			n = sizeof(str) - 1;
		}

		pr_info("\tUUID: %s%*c(%s)\n", str, 26 - n, ' ', uuid);
	} else
		pr_info("\tUUID: %*c(%s)\n", 26, ' ', uuid);
}

static void print_uuids(GDBusProxy *proxy)
{
	DBusMessageIter iter, value;

	if (g_dbus_proxy_get_property(proxy, "UUIDs", &iter) == FALSE)
		return;

	dbus_message_iter_recurse(&iter, &value);

	while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
		const char *uuid;

		dbus_message_iter_get_basic(&value, &uuid);

		print_uuid(uuid);

		dbus_message_iter_next(&value);
	}
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

static gboolean service_is_child(GDBusProxy *service)
{
	DBusMessageIter iter;
	const char *device;

	if (g_dbus_proxy_get_property(service, "Device", &iter) == FALSE)
		return FALSE;

	dbus_message_iter_get_basic(&iter, &device);

	if (!default_ctrl)
		return FALSE;

	ble_dev = g_dbus_proxy_lookup(default_ctrl->devices, NULL, device,
					"org.bluez.Device1");

	return ble_dev != NULL;
}

static struct adapter *find_parent(GDBusProxy *device)
{
	GList *list;

	for (list = g_list_first(ctrl_list); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;

		if (device_is_child(device, adapter->proxy) == TRUE)
			return adapter;
	}
	return NULL;
}

#define SERVER_CLASS_TELEPHONY	(1U << (22))
#define SERVER_CLASS_AUDIO		(1U << (21))
#define DEVICE_CLASS_SHIFT		8
#define DEVICE_CLASS_MASK		0x3f
#define DEVICE_CLASS_PHONE		2
#define DEVICE_CLASS_AUDIO		4

enum BT_Device_Class dist_dev_class(GDBusProxy *proxy)
{
	DBusMessageIter addrType_iter, class_iter, addr_iter, Alias_iter;
	const char *addressType = NULL;
	const char *address = NULL;
	const char *Alias = NULL;
	dbus_uint32_t valu32;

	if (g_dbus_proxy_get_property(proxy, "AddressType", &addrType_iter) == TRUE) {
		dbus_message_iter_get_basic(&addrType_iter, &addressType);
		pr_info("%s addressType:%s\n", __func__, addressType);

		if (g_dbus_proxy_get_property(proxy, "Alias", &Alias_iter) == TRUE) {
			dbus_message_iter_get_basic(&Alias_iter, &Alias);
			pr_info("%s Alias: %s\n", __func__, Alias);
		}

		if (g_dbus_proxy_get_property(proxy, "Address", &addr_iter) == TRUE) {
			dbus_message_iter_get_basic(&addr_iter, &address);
			pr_info("%s address: %s\n", __func__, address);
		}

		if (strcmp(addressType, "random") == 0)
			return BT_Device_Class::BT_BLE_DEVICE;

		if (strcmp(addressType, "public") == 0) {
			if (g_dbus_proxy_get_property(proxy, "Class", &class_iter) == TRUE) {
				dbus_message_iter_get_basic(&class_iter, &valu32);
				pr_info("%s class: 0x%x\n", __func__, valu32);

				if ((valu32 & SERVER_CLASS_TELEPHONY) &&
					(((valu32 >> DEVICE_CLASS_SHIFT) &
					DEVICE_CLASS_MASK) == DEVICE_CLASS_PHONE)) {
					pr_info("%s The device is source\n", __func__);
					return BT_Device_Class::BT_SOURCE_DEVICE;
				}
				if ((valu32 & SERVER_CLASS_AUDIO) &&
					(((valu32 >> DEVICE_CLASS_SHIFT) &
					DEVICE_CLASS_MASK) == DEVICE_CLASS_AUDIO)) {
					pr_info("%s The device is sink\n", __func__);
					return BT_Device_Class::BT_SINK_DEVICE;
				}

				if ((((valu32 >> DEVICE_CLASS_SHIFT) & DEVICE_CLASS_MASK) == DEVICE_CLASS_AUDIO) ||
					(((valu32 >> DEVICE_CLASS_SHIFT) & DEVICE_CLASS_MASK) == DEVICE_CLASS_PHONE)) {
					DBusMessageIter iter, value;
					const char *text;
					char str[26];
					unsigned int n;
					const char *uuid;
					enum BT_Device_Class ret = BT_Device_Class::BT_IDLE;

					if (g_dbus_proxy_get_property(proxy, "UUIDs", &iter) == FALSE)
						return BT_Device_Class::BT_IDLE;

					dbus_message_iter_recurse(&iter, &value);

					while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING) {
						dbus_message_iter_get_basic(&value, &uuid);

						text = bt_uuidstr_to_str(uuid);
						if (text) {
							str[sizeof(str) - 1] = '\0';

							n = snprintf(str, sizeof(str), "%s", text);
							if (n > sizeof(str) - 1) {
								  str[sizeof(str) - 2] = '.';
								  str[sizeof(str) - 3] = '.';
								  if (str[sizeof(str) - 4] == ' ')
										  str[sizeof(str) - 4] = '.';

								  n = sizeof(str) - 1;
							}

							if (strstr(str, "Audio Sink")) {
								ret = BT_Device_Class::BT_SINK_DEVICE;
								break;
							} else if (strstr(str, "Audio Source")) {
								ret = BT_Device_Class::BT_SOURCE_DEVICE;
								break;
							}
						}

						dbus_message_iter_next(&value);
					}

					return ret;
				}
			}
		}
	}

	return BT_Device_Class::BT_IDLE;
}

static void set_source_device(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	DBusMessageIter addr_iter;
	const char *address = NULL;

	default_src_dev = proxy;

	if (proxy == NULL) {
		default_src_dev = NULL;
		a2dp_master_save_status(NULL);
		a2dp_master_avrcp_close();
		a2dp_master_event_send(BT_SOURCE_EVENT_DISCONNECTED);
		return;
	}

	if (!g_dbus_proxy_get_property(proxy, "Alias", &iter)) {
		if (!g_dbus_proxy_get_property(proxy, "Address", &iter)) {
			pr_info("%s NO VAILD\n", __func__);
		}
	}

	if (g_dbus_proxy_get_property(proxy, "Address", &addr_iter) == TRUE) {
		dbus_message_iter_get_basic(&addr_iter, &address);
		pr_info("%s address: %s\n", __func__, address);
	}

	if (g_bt_callback.bt_source_event_cb) {
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECTED);
		a2dp_master_avrcp_open();
	}
	a2dp_master_save_status(address);
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

	save_last_device(proxy);

	if (!g_dbus_proxy_get_property(proxy, "Alias", &iter)) {
		if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
			goto done;
	}

	path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(&iter, &desc);
	desc = g_strdup_printf(COLOR_BLUE "[%s%s%s]" COLOR_OFF "# ", desc,
				attribute ? ":" : "",
				attribute ? attribute + strlen(path) : "");

done:
	bt_shell_set_prompt(desc ? desc : PROMPT_ON);
	g_free(desc);
}

static void device_added(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	struct adapter *adapter = find_parent(proxy);
	char dev_addr[18], dev_name[256];
	dbus_bool_t paired = FALSE;
	dbus_bool_t connected = FALSE;

	if (!adapter) {
		/* TODO: Error */
		return;
	}

	adapter->devices = g_list_append(adapter->devices, proxy);
	print_device(proxy, COLORED_NEW);
	bt_shell_set_env(g_dbus_proxy_get_path(proxy), proxy);

	bt_dev_found_send(proxy);

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter))
		dbus_message_iter_get_basic(&iter, &connected);

	if (g_dbus_proxy_get_property(proxy, "Paired", &iter) == TRUE) {
		dbus_message_iter_get_basic(&iter, &paired);
		if (!paired && connected) {
			bt_get_device_addr_by_proxy(proxy, dev_addr, 18);
			bt_get_device_name_by_proxy(proxy, dev_name, 256);
			bt_bond_state_send(dev_addr, dev_name, RK_BT_BOND_STATE_BONDING);
		}
	}

	if (default_dev)
		return;

	if (connected)
		set_default_device(proxy, NULL);
}

static struct adapter *find_ctrl(GList *source, const char *path);

static struct adapter *adapter_new(GDBusProxy *proxy)
{
	struct adapter *adapter = (struct adapter *)g_malloc0(sizeof(struct adapter));
	pr_info("=== %s ===\n", __func__);

	ctrl_list = g_list_append(ctrl_list, adapter);

	if (!default_ctrl)
		default_ctrl = adapter;

	pr_info("=== %s default_ctrl: %p ===\n", __func__, default_ctrl);

	return adapter;
}

static void adapter_added(GDBusProxy *proxy)
{
	struct adapter *adapter;
	char hostname_buf[HOSTNAME_MAX_LEN];
	pr_info("=== %s ===\n", __func__);
	adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
	if (!adapter)
		adapter = adapter_new(proxy);

	adapter->proxy = proxy;
	print_adapter(proxy, COLORED_NEW);

	if (g_bt_content && g_bt_content->bt_name) {
		pr_info("%s: bt_name: %s\n", __func__, g_bt_content->bt_name);
		rk_bt_set_device_name(g_bt_content->bt_name);
	} else {
		bt_gethostname(hostname_buf, sizeof(hostname_buf));
		pr_info("%s: bt_name: %s\n", __func__, hostname_buf);
		rk_bt_set_device_name(hostname_buf);
	}

	msleep(50);
	bt_exec_command_system("hciconfig hci0 piscan");

	bt_state_send(RK_BT_STATE_ON);
	bt_shell_set_env(g_dbus_proxy_get_path(proxy), proxy);
}

static void ad_manager_added(GDBusProxy *proxy)
{
	struct adapter *adapter;
	adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
	if (!adapter)
		adapter = adapter_new(proxy);

	adapter->ad_proxy = proxy;
}

static void proxy_added(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("BT Enter: proxy_added: %s [SNK: %d, SRC: %d]\n", interface, A2DP_SINK_FLAG, A2DP_SRC_FLAG);

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_added(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_added(proxy);
	} else if (!strcmp(interface, "org.bluez.AgentManager1")) {
		if (!agent_manager) {
			agent_manager = proxy;

			if (auto_register_agent &&
					!bt_shell_get_env("NON_INTERACTIVE"))
				agent_register(dbus_conn, agent_manager,
							auto_register_agent);
		}
	} else if (!strcmp(interface, "org.bluez.GattService1")) {
		if (service_is_child(proxy))
			gatt_add_service(proxy);

		if (ble_service_cnt == 0) {
			ble_state_send(RK_BLE_STATE_CONNECT);
			ble_wifi_clean();
			pr_info("[D: %s]: BLE DEVICE BT_BLE_ENV_CONNECT\n", __func__);
		}

		ble_service_cnt++;

	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		gatt_add_characteristic(proxy);
		//gatt_acquire_write(proxy, NULL);
	} else if (!strcmp(interface, "org.bluez.GattDescriptor1")) {
		gatt_add_descriptor(proxy);
	} else if (!strcmp(interface, "org.bluez.GattManager1")) {
		//gatt_add_manager(proxy);
		register_app(proxy);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		ad_manager_added(proxy);
	}

	if (A2DP_SINK_FLAG) {
		if ((!strcmp(interface, "org.bluez.MediaPlayer1")) ||
			(!strcmp(interface, "org.bluez.MediaFolder1")) ||
			(!strcmp(interface, "org.bluez.MediaItem1")))
			a2dp_sink_proxy_added(proxy, user_data);

		if (!strcmp(interface, "org.bluez.Device1")) {
			if ((dist_dev_class(proxy) == BT_Device_Class::BT_SOURCE_DEVICE))
				a2dp_sink_proxy_added(proxy, user_data);
		}

		if (!strcmp(interface, "org.bluez.Adapter1")) {
			a2dp_sink_proxy_added(proxy, user_data);
		}
	}

	pr_info("BT Exit: proxy_added: %s\n", interface);
}

static void set_default_attribute(GDBusProxy *proxy)
{
	const char *path;

	default_attr = proxy;

	path = g_dbus_proxy_get_path(proxy);

	set_default_device(default_dev, path);
}

static void device_removed(GDBusProxy *proxy)
{
	char dev_addr[18], dev_name[256];
	dbus_bool_t paired;
	DBusMessageIter iter;

	struct adapter *adapter = (struct adapter *)find_parent(proxy);
	if (!adapter) {
		/* TODO: Error */
		return;
	}

	if (g_dbus_proxy_get_property(proxy, "Paired", &iter) == TRUE) {
		dbus_message_iter_get_basic(&iter, &paired);
		if (paired) {
			bt_get_device_addr_by_proxy(proxy, dev_addr, 18);
			bt_get_device_name_by_proxy(proxy, dev_name, 256);
			bt_bond_state_send(dev_addr, dev_name, RK_BT_BOND_STATE_NONE);
		}
	}

	adapter->devices = g_list_remove(adapter->devices, proxy);

	print_device(proxy, COLORED_DEL);
	bt_shell_set_env(g_dbus_proxy_get_path(proxy), NULL);

	if (default_dev == proxy)
		set_default_device(NULL, NULL);
}

static void adapter_removed(GDBusProxy *proxy)
{
	GList *ll;

	for (ll = g_list_first(ctrl_list); ll; ll = g_list_next(ll)) {
		struct adapter *adapter = (struct adapter *)ll->data;

		if (adapter->proxy == proxy) {
			print_adapter(proxy, COLORED_DEL);
			bt_shell_set_env(g_dbus_proxy_get_path(proxy), NULL);

			if (default_ctrl && default_ctrl->proxy == proxy) {
				default_ctrl = NULL;
				set_default_device(NULL, NULL);
			}

			ctrl_list = g_list_remove_link(ctrl_list, ll);
			g_list_free(adapter->devices);
			g_free(adapter);
			g_list_free(ll);
			return;
		}
	}
}

static void proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("BT Enter: proxy_removed: %s [SNK: %d, SRC: %d]\n", interface, A2DP_SINK_FLAG, A2DP_SRC_FLAG);

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_removed(proxy);
	} else if (!strcmp(interface, "org.bluez.AgentManager1")) {
		if (agent_manager == proxy) {
			agent_manager = NULL;
			if (auto_register_agent)
				agent_unregister(dbus_conn, NULL);
		}
	} else if (!strcmp(interface, "org.bluez.GattService1")) {
		gatt_remove_service(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);

		ble_service_cnt--;

		if (ble_service_cnt == 0) {
			ble_dev = NULL;
			ble_state_send(RK_BLE_STATE_DISCONNECT);
			pr_info("[BLE: %s]: BLE DEVICE DISCONNECTED [BF: %d]\n", __func__, BLE_FLAG);
			sleep(1);
			if (BLE_FLAG)
				gatt_set_on_adv();
			else
				ble_disable_adv();
		}
	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		gatt_remove_characteristic(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattDescriptor1")) {
		gatt_remove_descriptor(proxy);

		if (default_attr == proxy)
			set_default_attribute(NULL);
	} else if (!strcmp(interface, "org.bluez.GattManager1")) {
		gatt_remove_manager(proxy);
	} else if (!strcmp(interface, "org.bluez.LEAdvertisingManager1")) {
		ad_unregister(dbus_conn, NULL);
	}

	if (A2DP_SINK_FLAG)
		a2dp_sink_proxy_removed(proxy, user_data);
	pr_info("BT Exit: proxy_removed: %s\n", interface);
}

static struct adapter *find_ctrl(GList *source, const char *path)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;

		if (!strcasecmp(g_dbus_proxy_get_path(adapter->proxy), path))
			return adapter;
	}

	return NULL;
}

static void device_paired_process(GDBusProxy *proxy,
					DBusMessageIter *iter, char *dev_addr)
{
	dbus_bool_t valbool = FALSE;
	char dev_name[256];

	bt_get_device_name_by_proxy(proxy, dev_name, 256);

	dbus_message_iter_get_basic(iter, &valbool);
	if(valbool)
		bt_bond_state_send(dev_addr, dev_name, RK_BT_BOND_STATE_BONDED);
	else
		bt_bond_state_send(dev_addr, dev_name, RK_BT_BOND_STATE_NONE);
}

static void device_connected_process(GDBusProxy *proxy,
					DBusMessageIter *iter, void *user_data)
{
	dbus_bool_t connected;
	enum BT_Device_Class bdc;

	dbus_message_iter_get_basic(iter, &connected);

	if (connected && default_dev == NULL)
		set_default_device(proxy, NULL);
	else if (!connected && default_dev == proxy)
		set_default_device(NULL, NULL);

	bdc = dist_dev_class(proxy);

	//bt_source
	if (A2DP_SRC_FLAG) {
		if (!connected && default_src_dev == proxy) {
			if (bdc == BT_Device_Class::BT_SINK_DEVICE) {
				set_source_device(NULL);
			}
		}
		if (connected && (dist_dev_class(proxy) == BT_Device_Class::BT_SINK_DEVICE))
			set_source_device(proxy);
	}

	//bt_sink
	if (A2DP_SINK_FLAG) {
		if (bdc == BT_Device_Class::BT_SOURCE_DEVICE)
			device_changed(proxy, iter, user_data);
	}
}

static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;
	struct adapter *ctrl;

	interface = g_dbus_proxy_get_interface(proxy);
	pr_info("BT Enter: property_changed: %s [SNK: %d, SRC: %d]\n", interface, A2DP_SINK_FLAG, A2DP_SRC_FLAG);

	if (!strcmp(interface, "org.bluez.Device1")) {
		if (default_ctrl && device_is_child(proxy,
					default_ctrl->proxy) == TRUE) {
			DBusMessageIter addr_iter;
			char *str;
			char dev_addr[18];

			if(!bt_get_device_addr_by_proxy(proxy, dev_addr, 18))
				str = g_strdup_printf("[" COLORED_CHG
						"] Device %s ", dev_addr);
			else
				str = g_strdup("");

			if (strcmp(name, "Paired") == 0) {
				device_paired_process(proxy, iter, dev_addr);
			}

			if (strcmp(name, "Connected") == 0) {
				device_connected_process(proxy, iter, user_data);
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
			str = g_strdup_printf("[" COLORED_CHG
						"] Controller %s ", address);
		} else
			str = g_strdup("");

		if (!strcmp(name, "Powered"))
			adapter_changed(proxy, iter, user_data);

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
			str = g_strdup_printf("[" COLORED_CHG
						"] Controller %s ",
						address);
		} else
			str = g_strdup("");

		print_iter(str, name, iter);
		g_free(str);
	} else if (proxy == default_attr) {
		char *str;

		str = g_strdup_printf("[" COLORED_CHG "] Attribute %s ",
						g_dbus_proxy_get_path(proxy));

		print_iter(str, name, iter);
		g_free(str);
	}

	if (A2DP_SINK_FLAG)
		a2dp_sink_property_changed(proxy, name, iter, user_data);

	pr_info("BT Exit: property_changed: %s\n", interface);
}

static void message_handler(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	pr_info("[SIGNAL] %s.%s\n", dbus_message_get_interface(message),
					dbus_message_get_member(message));
}

static struct adapter *find_ctrl_by_address(GList *source, const char *address)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;
		DBusMessageIter iter;
		const char *str;

		if (g_dbus_proxy_get_property(adapter->proxy,
					"Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strcasecmp(str, address))
			return adapter;
	}

	return NULL;
}

static GDBusProxy *find_proxy_by_address(GList *source, const char *address)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		GDBusProxy *proxy = (GDBusProxy *)list->data;
		DBusMessageIter iter;
		const char *str;

		if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strcasecmp(str, address))
			return proxy;
	}

	return NULL;
}

static gboolean check_default_ctrl(void)
{
	if (!default_ctrl) {
		pr_info("No default controller available\n");
		return FALSE;
	}

	return TRUE;
}

static gboolean parse_argument(int argc, char *argv[], const char **arg_table,
					const char *msg, dbus_bool_t *value,
					const char **option)
{
	const char **opt;

	if (!strcmp(argv[1], "on") || !strcmp(argv[1], "yes")) {
		*value = TRUE;
		if (option)
			*option = "";
		return TRUE;
	}

	if (!strcmp(argv[1], "off") || !strcmp(argv[1], "no")) {
		*value = FALSE;
		return TRUE;
	}

	for (opt = arg_table; opt && *opt; opt++) {
		if (strcmp(argv[1], *opt) == 0) {
			*value = TRUE;
			*option = *opt;
			return TRUE;
		}
	}

	pr_info("Invalid argument %s\n", argv[1]);
	return FALSE;
}

static void cmd_list(int argc, char *argv[])
{
	GList *list;

	for (list = g_list_first(ctrl_list); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;
		print_adapter(adapter->proxy, NULL);
	}
}

static void cmd_show(int argc, char *argv[])
{
	struct adapter *adapter;
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *address;

	if (argc < 2 || !strlen(argv[1])) {
		if (check_default_ctrl() == FALSE)
			return bt_shell_noninteractive_quit(EXIT_FAILURE);

		proxy = default_ctrl->proxy;
	} else {
		adapter = find_ctrl_by_address(ctrl_list, argv[1]);
		if (!adapter) {
			pr_info("Controller %s not available\n",
								argv[1]);
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		}
		proxy = adapter->proxy;
	}

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == TRUE) {
		const char *type;

		dbus_message_iter_get_basic(&iter, &type);

		pr_info("Controller %s (%s)\n", address, type);
	} else {
		pr_info("Controller %s\n", address);
	}

	print_property(proxy, "Name");
	print_property(proxy, "Alias");
	print_property(proxy, "Class");
	print_property(proxy, "Powered");
	print_property(proxy, "Discoverable");
	print_property(proxy, "Pairable");
	print_uuids(proxy);
	print_property(proxy, "Modalias");
	print_property(proxy, "Discovering");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_select(int argc, char *argv[])
{
	struct adapter *adapter;

	adapter = find_ctrl_by_address(ctrl_list, argv[1]);
	if (!adapter) {
		pr_info("Controller %s not available\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (default_ctrl && default_ctrl->proxy == adapter->proxy)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	default_ctrl = adapter;
	print_adapter(adapter->proxy, NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_devices(BtScanParam *param)
{
	GList *ll;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	for (ll = g_list_first(default_ctrl->devices); ll; ll = g_list_next(ll)) {
		GDBusProxy *proxy = (GDBusProxy *)ll->data;

		if(param)
			btsrc_scan_save_device(proxy, param);
		else
			print_device(proxy, NULL);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_paired_devices()
{
	GList *ll;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	for (ll = g_list_first(default_ctrl->devices);
			ll; ll = g_list_next(ll)) {
		GDBusProxy *proxy = (GDBusProxy *)ll->data;
		DBusMessageIter iter;
		dbus_bool_t paired;

		if (g_dbus_proxy_get_property(proxy, "Paired", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &paired);
		if (!paired)
			continue;

		print_device(proxy, NULL);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void generic_callback(const DBusError *error, void *user_data)
{
	char *str = (char *)user_data;

	if (dbus_error_is_set(error)) {
		pr_info("Set failed: %s\n", error->name);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	} else {
		pr_info("Changing succeeded\n");
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}
}

static void cmd_system_alias(int argc, char *argv[])
{
	char *name;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	name = g_strdup(argv[1]);

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, g_free) == TRUE)
		return;

	g_free(name);
}

static void cmd_reset_alias(int argc, char *argv[])
{
	char *name;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	name = g_strdup("");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, g_free) == TRUE)
		return;

	g_free(name);
}

static void cmd_power(int argc, char *argv[])
{
	dbus_bool_t powered;
	char *str;

	if (!parse_argument(argc, argv, NULL, NULL, &powered, NULL))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	str = g_strdup_printf("power %s", powered == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Powered",
					DBUS_TYPE_BOOLEAN, &powered,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);
}

static void cmd_pairable(int argc, char *argv[])
{
	dbus_bool_t pairable;
	char *str;

	if (!parse_argument(argc, argv, NULL, NULL, &pairable, NULL))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	str = g_strdup_printf("pairable %s", pairable == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Pairable",
					DBUS_TYPE_BOOLEAN, &pairable,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_discoverable(int argc, char *argv[])
{
	dbus_bool_t discoverable;
	char *str;

	if (!parse_argument(argc, argv, NULL, NULL, &discoverable, NULL))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	str = g_strdup_printf("discoverable %s",
				discoverable == TRUE ? "on" : "off");

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Discoverable",
					DBUS_TYPE_BOOLEAN, &discoverable,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_agent(int argc, char *argv[])
{
	dbus_bool_t enable;
	const char *capability;

	if (!parse_argument(argc, argv, agent_arguments, "capability",
						&enable, &capability))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (enable == TRUE) {
		g_free(auto_register_agent);
		auto_register_agent = g_strdup(capability);

		if (agent_manager)
			agent_register(dbus_conn, agent_manager,
						auto_register_agent);
		else
			pr_info("Agent registration enabled\n");
	} else {
		g_free(auto_register_agent);
		auto_register_agent = NULL;

		if (agent_manager)
			agent_unregister(dbus_conn, agent_manager);
		else
			pr_info("Agent registration disabled\n");
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_default_agent(int argc, char *argv[])
{
	agent_default(dbus_conn, agent_manager);
}

static void discovery_reply(DBusMessage *message, void *user_data)
{
	dbus_bool_t enable = GPOINTER_TO_UINT(user_data);
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to %s discovery: %s\n",
				enable == TRUE ? "start" : "stop", error.name);
		if(enable)
			bt_discovery_state_send(RK_BT_DISC_START_FAILED);

		dbus_error_free(&error);
		return;
	}

	if(enable)
		bt_discovery_state_send(RK_BT_DISC_STARTED);

	pr_info("Discovery %s\n", enable ? "started" : "stopped");
	/* Leave the discovery running even on noninteractive mode */
}

static struct set_discovery_filter_args {
	char *transport;
	dbus_uint16_t rssi;
	dbus_int16_t pathloss;
	char **uuids;
	size_t uuids_len;
	dbus_bool_t duplicate;
	bool set;
} filter = {
	NULL,
	DISTANCE_VAL_INVALID,
	DISTANCE_VAL_INVALID,
	NULL,
	0,
	false,
	true,
};

static void set_discovery_filter_setup(DBusMessageIter *iter, void *user_data)
{
	struct set_discovery_filter_args *args = (struct set_discovery_filter_args *)user_data;
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	g_dbus_dict_append_array(&dict, "UUIDs", DBUS_TYPE_STRING,
							&args->uuids,
							args->uuids_len);

	if (args->pathloss != DISTANCE_VAL_INVALID)
		g_dbus_dict_append_entry(&dict, "Pathloss", DBUS_TYPE_UINT16,
						&args->pathloss);

	if (args->rssi != DISTANCE_VAL_INVALID)
		g_dbus_dict_append_entry(&dict, "RSSI", DBUS_TYPE_INT16,
						&args->rssi);

	if (args->transport != NULL)
		g_dbus_dict_append_entry(&dict, "Transport", DBUS_TYPE_STRING,
						&args->transport);

	if (args->duplicate)
		g_dbus_dict_append_entry(&dict, "DuplicateData",
						DBUS_TYPE_BOOLEAN,
						&args->duplicate);

	dbus_message_iter_close_container(iter, &dict);
}


static void set_discovery_filter_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("SetDiscoveryFilter failed: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	filter.set = true;

	pr_info("SetDiscoveryFilter success\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void set_discovery_filter(void)
{
	if (check_default_ctrl() == FALSE || filter.set)
		return;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "SetDiscoveryFilter",
		set_discovery_filter_setup, set_discovery_filter_reply,
		&filter, NULL) == FALSE) {
		pr_info("Failed to set discovery filter\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	filter.set = true;
}

static int cmd_scan(const char *cmd)
{
	dbus_bool_t enable;
	const char *method;

	if (strcmp(cmd, "on") == 0) {
		enable = TRUE;
	} else if (strcmp(cmd, "off") == 0){
		enable = FALSE;
	} else {
		pr_info("ERROR: %s cmd(%s) is invalid!\n", __func__, cmd);
		return -1;
	}

	if (check_default_ctrl() == FALSE)
		return -1;

	if (enable == TRUE) {
		set_discovery_filter();
		method = "StartDiscovery";
	} else
		method = "StopDiscovery";

	pr_info("%s method = %s\n", __func__, method);

	if (g_dbus_proxy_method_call(default_ctrl->proxy, method,
				NULL, discovery_reply,
				GUINT_TO_POINTER(enable), NULL) == FALSE) {
		pr_info("Failed to %s discovery\n",
					enable == TRUE ? "start" : "stop");
		return -1;
	}

	return 0;
}

static void cmd_scan_filter_uuids(int argc, char *argv[])
{
	if (argc < 2 || !strlen(argv[1])) {
		char **uuid;

		for (uuid = filter.uuids; uuid && *uuid; uuid++)
			print_uuid(*uuid);

		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	g_strfreev(filter.uuids);
	filter.uuids = NULL;
	filter.uuids_len = 0;

	if (!strcmp(argv[1], "all"))
		goto commit;

	filter.uuids = g_strdupv(&argv[1]);
	if (!filter.uuids) {
		pr_info("Failed to parse input\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	filter.uuids_len = g_strv_length(filter.uuids);

commit:
	filter.set = false;
}

static void cmd_scan_filter_rssi(int argc, char *argv[])
{
	if (argc < 2 || !strlen(argv[1])) {
		if (filter.rssi != DISTANCE_VAL_INVALID)
			pr_info("RSSI: %d\n", filter.rssi);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	filter.pathloss = DISTANCE_VAL_INVALID;
	filter.rssi = atoi(argv[1]);

	filter.set = false;
}

static void cmd_scan_filter_pathloss(int argc, char *argv[])
{
	if (argc < 2 || !strlen(argv[1])) {
		if (filter.pathloss != DISTANCE_VAL_INVALID)
			pr_info("Pathloss: %d\n",
						filter.pathloss);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	filter.rssi = DISTANCE_VAL_INVALID;
	filter.pathloss = atoi(argv[1]);

	filter.set = false;
}

static void cmd_scan_filter_transport(int argc, char *argv[])
{
	if (argc < 2 || !strlen(argv[1])) {
		if (filter.transport)
			pr_info("Transport: %s\n",
					filter.transport);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	g_free(filter.transport);
	filter.transport = g_strdup(argv[1]);

	filter.set = false;
}

static void cmd_scan_filter_duplicate_data(int argc, char *argv[])
{
	if (argc < 2 || !strlen(argv[1])) {
		pr_info("DuplicateData: %s\n",
				filter.duplicate ? "on" : "off");
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	if (!strcmp(argv[1], "on"))
		filter.duplicate = true;
	else if (!strcmp(argv[1], "off"))
		filter.duplicate = false;
	else {
		pr_info("Invalid option: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	filter.set = false;
}

static void filter_clear_uuids(void)
{
	g_strfreev(filter.uuids);
	filter.uuids = NULL;
	filter.uuids_len = 0;
}

static void filter_clear_rssi(void)
{
	filter.rssi = DISTANCE_VAL_INVALID;
}

static void filter_clear_pathloss(void)
{
	filter.pathloss = DISTANCE_VAL_INVALID;
}

static void filter_clear_transport(void)
{
	g_free(filter.transport);
	filter.transport = NULL;
}

static void filter_clear_duplicate(void)
{
	filter.duplicate = false;
}

struct clear_entry {
	const char *name;
	void (*clear) (void);
};

static const struct clear_entry filter_clear[] = {
	{ "uuids", filter_clear_uuids },
	{ "rssi", filter_clear_rssi },
	{ "pathloss", filter_clear_pathloss },
	{ "transport", filter_clear_transport },
	{ "duplicate-data", filter_clear_duplicate },
	{}
};

static char *filter_clear_generator(const char *text, int state)
{
	static int index, len;
	const char *arg;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	while ((arg = filter_clear[index].name)) {
		index++;

		if (!strncmp(arg, text, len))
			return strdup(arg);
	}

	return NULL;
}

static gboolean data_clear(const struct clear_entry *entry_table,
							const char *name)
{
	const struct clear_entry *entry;
	bool all = false;

	if (!name || !strlen(name) || !strcmp("all", name))
		all = true;

	for (entry = entry_table; entry && entry->name; entry++) {
		if (all || !strcmp(entry->name, name)) {
			entry->clear();
			if (!all)
				goto done;
		}
	}

	if (!all) {
		pr_info("Invalid argument %s\n", name);
		return FALSE;
	}

done:
	return TRUE;
}

static void cmd_scan_filter_clear(int argc, char *argv[])
{
	bool all = false;

	if (argc < 2 || !strlen(argv[1]))
		all = true;

	if (!data_clear(filter_clear, all ? "all" : argv[1]))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	filter.set = false;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	set_discovery_filter();
}

static struct GDBusProxy *find_device(int argc, char *argv[])
{
	GDBusProxy *proxy;

	if (argc < 2 || !strlen(argv[1])) {
		if (default_dev)
			return default_dev;
		pr_info("Missing device address argument\n");
		return NULL;
	}

	if (check_default_ctrl() == FALSE)
		return NULL;

	proxy = find_proxy_by_address(default_ctrl->devices, argv[1]);
	if (!proxy) {
		pr_info("Device %s not available\n", argv[1]);
		return NULL;
	}

	return proxy;
}

static struct GDBusProxy *find_device_by_address(char *address)
{
	GDBusProxy *proxy;

	if (!strlen(address)) {
		if (default_dev)
			return default_dev;
		pr_info("Missing device address argument\n");
		return NULL;
	}

	if (check_default_ctrl() == FALSE)
		return NULL;

	proxy = find_proxy_by_address(default_ctrl->devices, address);
	if (!proxy) {
		pr_info("Device %s not available\n", address);
		return NULL;
	}

	return proxy;
}

static void cmd_info(int argc, char *argv[])
{
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *address;

	proxy = find_device(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == TRUE) {
		const char *type;

		dbus_message_iter_get_basic(&iter, &type);

		pr_info("Device %s (%s)\n", address, type);
	} else {
		pr_info("Device %s\n", address);
	}

	print_property(proxy, "Name");
	print_property(proxy, "Alias");
	print_property(proxy, "Class");
	print_property(proxy, "Appearance");
	print_property(proxy, "Icon");
	print_property(proxy, "Paired");
	print_property(proxy, "Trusted");
	print_property(proxy, "Blocked");
	print_property(proxy, "Connected");
	print_property(proxy, "LegacyPairing");
	print_property(proxy, "Modalias");
	print_property(proxy, "ManufacturerData");
	print_property(proxy, "ServiceData");
	print_property(proxy, "RSSI");
	print_property(proxy, "TxPower");
	print_property(proxy, "AdvertisingFlags");
	print_property(proxy, "AdvertisingData");
	print_uuids(proxy);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void pair_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to pair: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	pr_info("Pairing successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static const char *proxy_address(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	const char *addr;

	if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
		return NULL;

	dbus_message_iter_get_basic(&iter, &addr);

	return addr;
}

static int cmd_pair(GDBusProxy *proxy)
{
	if (!proxy)
		return -1;

	if (g_dbus_proxy_method_call(proxy, "Pair", NULL, pair_reply,
							NULL, NULL) == FALSE) {
		pr_info("%s: Failed to pair\n", __func__);
		return -1;
	}

	pr_info("%s: Attempting to pair with %s\n", __func__, proxy_address(proxy));
	return 0;
}

static void cmd_trust(int argc, char *argv[])
{
	GDBusProxy *proxy;
	dbus_bool_t trusted;
	char *str;

	proxy = find_device(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	trusted = TRUE;

	str = g_strdup_printf("%s trust", proxy_address(proxy));

	if (g_dbus_proxy_set_property_basic(proxy, "Trusted",
					DBUS_TYPE_BOOLEAN, &trusted,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_untrust(int argc, char *argv[])
{
	GDBusProxy *proxy;
	dbus_bool_t trusted;
	char *str;

	proxy = find_device(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	trusted = FALSE;

	str = g_strdup_printf("%s untrust", proxy_address(proxy));

	if (g_dbus_proxy_set_property_basic(proxy, "Trusted",
					DBUS_TYPE_BOOLEAN, &trusted,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_block(int argc, char *argv[])
{
	GDBusProxy *proxy;
	dbus_bool_t blocked;
	char *str;

	proxy = find_device(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	blocked = TRUE;

	str = g_strdup_printf("%s block", proxy_address(proxy));

	if (g_dbus_proxy_set_property_basic(proxy, "Blocked",
					DBUS_TYPE_BOOLEAN, &blocked,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_unblock(int argc, char *argv[])
{
	GDBusProxy *proxy;
	dbus_bool_t blocked;
	char *str;

	proxy = find_device(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	blocked = FALSE;

	str = g_strdup_printf("%s unblock", proxy_address(proxy));

	if (g_dbus_proxy_set_property_basic(proxy, "Blocked",
					DBUS_TYPE_BOOLEAN, &blocked,
					generic_callback, str, g_free) == TRUE)
		return;

	g_free(str);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void remove_device_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to remove device: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	pr_info("Device has been removed\n");
	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void remove_device_setup(DBusMessageIter *iter, void *user_data)
{
	char *path = (char *)user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

static int remove_device(GDBusProxy *proxy)
{
	char *path;

	path = g_strdup(g_dbus_proxy_get_path(proxy));

	if (check_default_ctrl() == FALSE)
		return -1;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "RemoveDevice",
						remove_device_setup,
						remove_device_reply,
						path, g_free) == FALSE) {
		pr_info("%s: Failed to remove device\n", __func__);
		g_free(path);
		return -1;
	}

	pr_info("%s: Attempting to remove device with %s\n", __func__, proxy_address(proxy));
	return 0;
}

static void cmd_remove(int argc, char *argv[])
{
	GDBusProxy *proxy;

	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (strcmp(argv[1], "*") == 0) {
		GList *list;

		for (list = default_ctrl->devices; list;
						list = g_list_next(list)) {
			GDBusProxy *proxy = (GDBusProxy *)list->data;

			remove_device(proxy);
		}
		return;
	}

	proxy = find_proxy_by_address(default_ctrl->devices, argv[1]);
	if (!proxy) {
		pr_info("Device %s not available\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	remove_device(proxy);
}

static void cmd_list_attributes(int argc, char *argv[])
{
	GDBusProxy *proxy;

	proxy = find_device(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_list_attributes(g_dbus_proxy_get_path(proxy));

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_set_alias(int argc, char *argv[])
{
	char *name;

	if (!default_dev) {
		pr_info("No device connected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	name = g_strdup(argv[1]);

	if (g_dbus_proxy_set_property_basic(default_dev, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, g_free) == TRUE)
		return;

	g_free(name);

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_select_attribute(int argc, char *argv[])
{
	GDBusProxy *proxy;

	if (!default_dev) {
		pr_info("No device connected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	proxy = gatt_select_attribute(default_attr, argv[1]);
	if (proxy) {
		set_default_attribute(proxy);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static struct GDBusProxy *find_attribute(int argc, char *argv[])
{
	GDBusProxy *proxy;

	if (argc < 2 || !strlen(argv[1])) {
		if (default_attr)
			return default_attr;
		pr_info("Missing attribute argument\n");
		return NULL;
	}

	proxy = gatt_select_attribute(default_attr, argv[1]);
	if (!proxy) {
		pr_info("Attribute %s not available\n", argv[1]);
		return NULL;
	}

	return proxy;
}

static void cmd_attribute_info(int argc, char *argv[])
{
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *iface, *uuid, *text;

	proxy = find_attribute(argc, argv);
	if (!proxy)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_get_property(proxy, "UUID", &iter) == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	dbus_message_iter_get_basic(&iter, &uuid);

	text = bt_uuidstr_to_str(uuid);
	if (!text)
		text = g_dbus_proxy_get_path(proxy);

	iface = g_dbus_proxy_get_interface(proxy);
	if (!strcmp(iface, "org.bluez.GattService1")) {
		pr_info("Service - %s\n", text);

		print_property(proxy, "UUID");
		print_property(proxy, "Primary");
		print_property(proxy, "Characteristics");
		print_property(proxy, "Includes");
	} else if (!strcmp(iface, "org.bluez.GattCharacteristic1")) {
		pr_info("Characteristic - %s\n", text);

		print_property(proxy, "UUID");
		print_property(proxy, "Service");
		print_property(proxy, "Value");
		print_property(proxy, "Notifying");
		print_property(proxy, "Flags");
		print_property(proxy, "Descriptors");
	} else if (!strcmp(iface, "org.bluez.GattDescriptor1")) {
		pr_info("Descriptor - %s\n", text);

		print_property(proxy, "UUID");
		print_property(proxy, "Characteristic");
		print_property(proxy, "Value");
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_read(int argc, char *argv[])
{
	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_read_attribute(default_attr, argc, argv);
}

static void cmd_write(int argc, char *argv[])
{
	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_write_attribute(default_attr, argc, argv);
}

static void cmd_acquire_write(int argc, char *argv[])
{
	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_acquire_write(default_attr, argv[1]);
}

static void cmd_release_write(int argc, char *argv[])
{
	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_release_write(default_attr, argv[1]);
}

static void cmd_acquire_notify(int argc, char *argv[])
{
	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_acquire_notify(default_attr, argv[1]);
}

static void cmd_release_notify(int argc, char *argv[])
{
	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_release_notify(default_attr, argv[1]);
}

static void cmd_notify(int argc, char *argv[])
{
	dbus_bool_t enable;

	if (!parse_argument(argc, argv, NULL, NULL, &enable, NULL))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (!default_attr) {
		pr_info("No attribute selected\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	gatt_notify_attribute(default_attr, enable ? true : false);
}

static void cmd_register_app(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_register_app(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_unregister_app(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_unregister_app(dbus_conn, default_ctrl->proxy);
}

static void cmd_register_service(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_register_service(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_register_includes(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_register_include(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_unregister_includes(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_unregister_include(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_unregister_service(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_unregister_service(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_register_characteristic(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_register_chrc(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_unregister_characteristic(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_unregister_chrc(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_register_descriptor(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_register_desc(dbus_conn, default_ctrl->proxy, argc, argv);
}

static void cmd_unregister_descriptor(int argc, char *argv[])
{
	if (check_default_ctrl() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	gatt_unregister_desc(dbus_conn, default_ctrl->proxy, argc, argv);
}

static char *generic_generator(const char *text, int state,
					GList *source, const char *property)
{
	static int index, len;
	GList *list;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	for (list = g_list_nth(source, index); list;
						list = g_list_next(list)) {
		GDBusProxy *proxy = (GDBusProxy *)list->data;
		DBusMessageIter iter;
		const char *str;

		index++;

		if (g_dbus_proxy_get_property(proxy, property, &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strncasecmp(str, text, len))
			return strdup(str);
	}

	return NULL;
}

static char *ctrl_generator(const char *text, int state)
{
	static int index = 0;
	static int len = 0;
	GList *list;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	for (list = g_list_nth(ctrl_list, index); list;
						list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;
		DBusMessageIter iter;
		const char *str;

		index++;

		if (g_dbus_proxy_get_property(adapter->proxy,
					"Address", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &str);

		if (!strncasecmp(str, text, len))
			return strdup(str);
	}

	return NULL;
}

static char *dev_generator(const char *text, int state)
{
	return generic_generator(text, state,
			default_ctrl ? default_ctrl->devices : NULL, "Address");
}

static char *attribute_generator(const char *text, int state)
{
	return gatt_attribute_generator(text, state);
}

static char *argument_generator(const char *text, int state,
					const char *args_list[])
{
	static int index, len;
	const char *arg;

	if (!state) {
		index = 0;
		len = strlen(text);
	}

	while ((arg = args_list[index])) {
		index++;

		if (!strncmp(arg, text, len))
			return strdup(arg);
	}

	return NULL;
}

static char *capability_generator(const char *text, int state)
{
	return argument_generator(text, state, agent_arguments);
}

static void cmd_advertise(int argc, char *argv[])
{
	dbus_bool_t enable;
	const char *type;

	if (!parse_argument(argc, argv, ad_arguments, "type",
					&enable, &type))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (!default_ctrl || !default_ctrl->ad_proxy) {
		pr_info("LEAdvertisingManager not found\n");
		bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (enable == TRUE)
		ad_register(dbus_conn, default_ctrl->ad_proxy, type);
	else
		ad_unregister(dbus_conn, default_ctrl->ad_proxy);
}

static char *ad_generator(const char *text, int state)
{
	return argument_generator(text, state, ad_arguments);
}

static void cmd_advertise_uuids(int argc, char *argv[])
{
	ad_advertise_uuids(dbus_conn, argc, argv);
}

static void cmd_advertise_service(int argc, char *argv[])
{
	ad_advertise_service(dbus_conn, argc, argv);
}

static void cmd_advertise_manufacturer(int argc, char *argv[])
{
	ad_advertise_manufacturer(dbus_conn, argc, argv);
}

static void cmd_advertise_data(int argc, char *argv[])
{
	ad_advertise_data(dbus_conn, argc, argv);
}

static void cmd_advertise_discoverable(int argc, char *argv[])
{
	dbus_bool_t discoverable;

	if (argc < 2) {
		ad_advertise_discoverable(dbus_conn, NULL);
		return;
	}

	if (!parse_argument(argc, argv, NULL, NULL, &discoverable, NULL))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	ad_advertise_discoverable(dbus_conn, &discoverable);
}

static void cmd_advertise_discoverable_timeout(int argc, char *argv[])
{
	long int value;
	char *endptr = NULL;

	if (argc < 2) {
		ad_advertise_discoverable_timeout(dbus_conn, NULL);
		return;
	}

	value = strtol(argv[1], &endptr, 0);
	if (!endptr || *endptr != '\0' || value > UINT16_MAX) {
		pr_info("Invalid argument\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	ad_advertise_discoverable_timeout(dbus_conn, &value);
}

static void cmd_advertise_tx_power(int argc, char *argv[])
{
	dbus_bool_t powered;

	if (argc < 2) {
		ad_advertise_tx_power(dbus_conn, NULL);
		return;
	}

	if (!parse_argument(argc, argv, NULL, NULL, &powered, NULL))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	ad_advertise_tx_power(dbus_conn, &powered);
}

static void cmd_advertise_name(int argc, char *argv[])
{
	if (argc < 2) {
		ad_advertise_local_name(dbus_conn, NULL);
		return;
	}

	if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "yes") == 0) {
		ad_advertise_name(dbus_conn, true);
		return;
	}

	if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "no") == 0) {
		ad_advertise_name(dbus_conn, false);
		return;
	}

	ad_advertise_local_name(dbus_conn, argv[1]);
}

static void cmd_advertise_appearance(int argc, char *argv[])
{
	long int value;
	char *endptr = NULL;

	if (argc < 2) {
		ad_advertise_local_appearance(dbus_conn, NULL);
		return;
	}

	if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "yes") == 0) {
		ad_advertise_appearance(dbus_conn, true);
		return;
	}

	if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "no") == 0) {
		ad_advertise_appearance(dbus_conn, false);
		return;
	}

	value = strtol(argv[1], &endptr, 0);
	if (!endptr || *endptr != '\0' || value > UINT16_MAX) {
		pr_info("Invalid argument\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	ad_advertise_local_appearance(dbus_conn, &value);
}

static void cmd_advertise_duration(int argc, char *argv[])
{
	long int value;
	char *endptr = NULL;

	if (argc < 2) {
		ad_advertise_duration(dbus_conn, NULL);
		return;
	}

	value = strtol(argv[1], &endptr, 0);
	if (!endptr || *endptr != '\0' || value > UINT16_MAX) {
		pr_info("Invalid argument\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	ad_advertise_duration(dbus_conn, &value);
}

static void cmd_advertise_timeout(int argc, char *argv[])
{
	long int value;
	char *endptr = NULL;

	if (argc < 2) {
		ad_advertise_timeout(dbus_conn, NULL);
		return;
	}

	value = strtol(argv[1], &endptr, 0);
	if (!endptr || *endptr != '\0' || value > UINT16_MAX) {
		pr_info("Invalid argument\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	ad_advertise_timeout(dbus_conn, &value);
}

static void ad_clear_uuids(void)
{
	ad_disable_uuids(dbus_conn);
}

static void ad_clear_service(void)
{
	ad_disable_service(dbus_conn);
}

static void ad_clear_manufacturer(void)
{
	ad_disable_manufacturer(dbus_conn);
}

static void ad_clear_data(void)
{
	ad_disable_data(dbus_conn);
}

static void ad_clear_tx_power(void)
{
	dbus_bool_t powered = false;

	ad_advertise_tx_power(dbus_conn, &powered);
}

static void ad_clear_name(void)
{
	ad_advertise_name(dbus_conn, false);
}

static void ad_clear_appearance(void)
{
	ad_advertise_appearance(dbus_conn, false);
}

static void ad_clear_duration(void)
{
	long int value = 0;

	ad_advertise_duration(dbus_conn, &value);
}

static void ad_clear_timeout(void)
{
	long int value = 0;

	ad_advertise_timeout(dbus_conn, &value);
}

static const struct clear_entry ad_clear[] = {
	{ "uuids",      ad_clear_uuids },
	{ "service",        ad_clear_service },
	{ "manufacturer",   ad_clear_manufacturer },
	{ "data",       ad_clear_data },
	{ "tx-power",       ad_clear_tx_power },
	{ "name",       ad_clear_name },
	{ "appearance",     ad_clear_appearance },
	{ "duration",       ad_clear_duration },
	{ "timeout",        ad_clear_timeout },
	{}
};

static void cmd_ad_clear(int argc, char *argv[])
{
	bool all = false;

	if (argc < 2 || !strlen(argv[1]))
		all = true;

	if(!data_clear(ad_clear, all ? "all" : argv[1]))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static const struct option options[] = {
	{ "agent",  required_argument, 0, 'a' },
	{ 0, 0, 0, 0 }
};

static const char *agent_option;

static const char **optargs[] = {
	&agent_option
};

static const char *help[] = {
	"Register agent handler: <capability>"
};

static const struct bt_shell_opt opt = {
	.options = options,
	.optno = sizeof(options) / sizeof(struct option),
	.optstr = "a:",
	.optarg = optargs,
	.help = help,
};

static void client_ready(GDBusClient *client, void *user_data)
{
	return;
}

static guint reconnect_timer;

static void connect_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = (GDBusProxy *)user_data;
	DBusError error;
	static int conn_count = 2;
	DBusMessageIter iter;
	const char *address;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to connect: %s\n", error.name);
		dbus_error_free(&error);

		g_dbus_proxy_get_property(proxy, "Address", &iter);
		dbus_message_iter_get_basic(&iter, &address);

		conn_count--;
		if (conn_count > 0) {
			if (reconnect_timer) {
				g_source_remove(reconnect_timer);
				reconnect_timer = 0;
			}
			reconnect_timer = g_timeout_add_seconds(3,
						a2dp_master_connect, address);
			return;
		}

		if (dist_dev_class(proxy) == BT_Device_Class::BT_SINK_DEVICE)
			a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED);

		conn_count = 2;
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	pr_info("Connection successful\n");
	set_default_device(proxy, NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void disconn_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = (GDBusProxy *)user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to disconnect: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	pr_info("%s: Successful disconnected\n", __func__);

	//check disconnect
	if(bt_is_connected())
		pr_info("\n\n%s: The ACL link still exists!\n\n\n", __func__);

	if (proxy == default_dev)
		set_default_device(NULL, NULL);

//	if (proxy == default_src_dev)
//		set_source_device(NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void a2dp_source_clean(void)
{
	dbus_conn = NULL;
	agent_manager = NULL;
	auto_register_agent = NULL;

	default_ctrl = NULL;
	ctrl_list = NULL;
	default_dev = NULL;
	default_attr = NULL;

	btsrc_client = NULL;
	//btsrc_main_loop = NULL;

	A2DP_SRC_FLAG = 0;
	A2DP_SINK_FLAG = 0;
	BLE_FLAG = 0;
	default_src_dev = NULL;
}

void *init_a2dp_master(void *)
{
	return 0;
}

void bluetooth_open(RkBtContent *bt_content)
{
	a2dp_source_clean();
	A2DP_SRC_FLAG = 1;
	A2DP_SINK_FLAG = 1;

	if (agent_option)
		auto_register_agent = g_strdup(agent_option);
	else
		auto_register_agent = g_strdup("");

	dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);
	g_dbus_attach_object_manager(dbus_conn);

	btsrc_client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");
	if (NULL == btsrc_client) {
		pr_info("btsrc_client inti fail");
		dbus_connection_unref(dbus_conn);
		return NULL;
	}

	btsrc_main_loop = g_main_loop_new(NULL, FALSE);
	init_avrcp_ctrl();

	if(bt_content) {
		g_bt_content = bt_content;
		gatt_init(bt_content);
	} else {
		g_bt_content = NULL;
	}

	g_dbus_client_set_connect_watch(btsrc_client, connect_handler, NULL);
	g_dbus_client_set_disconnect_watch(btsrc_client, disconnect_handler, NULL);
	g_dbus_client_set_signal_watch(btsrc_client, message_handler, NULL);
	g_dbus_client_set_proxy_handlers(btsrc_client, proxy_added, proxy_removed,
						  property_changed, NULL);

	pr_info("#### %s server start...\n", __func__);
	BT_OPENED = 1;

	g_main_loop_run(btsrc_main_loop);

	//clean up
	g_dbus_client_unref(btsrc_client);
	dbus_connection_unref(dbus_conn);
	g_main_loop_unref(btsrc_main_loop);


	g_list_free_full(ctrl_list, proxy_leak);
	g_free(auto_register_agent);
	//a2dp_source_clean();

	pr_info("#### %s server exit!\n", __func__);
	pthread_exit(0);
}

static pthread_t bt_thread = 0;
int bt_open(RkBtContent *bt_content)
{
	int confirm_cnt = 10;

	if (bt_thread)
		return 0;

	if (pthread_create(&bt_thread, NULL, bluetooth_open, bt_content))
		return -1;

	pthread_setname_np(bt_thread, "bluetooth_open");
	pr_info("%s: thread_name: bluetooth_open, tid: %lu\n", __func__, bt_thread);

	while (confirm_cnt--) {
		if (BT_OPENED)
			return 0;
		usleep(100 * 1000);
	}

	return -1;
}

int bt_close(void)
{
	int ret = 0;

	g_main_loop_quit(btsrc_main_loop);
	pr_info("%s g_main_loop_quit\n", __func__);
	/*
	ret = pthread_join(bt_thread, NULL);
	if (ret) {
		pr_info("ERROR: %s waite for bt server thread exit failed!\n", __func__);
		return -1;
	} else
		pr_info("%s exit ok\n", __func__);
	*/
	bt_thread = 0;

	return 0;
}

int init_a2dp_master_ctrl()
{
	pr_info("init_a2dp_master_ctrl A2DP_SRC_FLAG: %lu\n", A2DP_SRC_FLAG);

	A2DP_SRC_FLAG = 1;
	A2DP_SINK_FLAG = 0;
	return 1;
}

int release_a2dp_master_ctrl() {
	pr_info("release_a2dp_master_ctrl start ...\n");
	A2DP_SRC_FLAG = 0;
	return 1;
}

static int a2dp_master_get_rssi(GDBusProxy *proxy)
{
	int retry_cnt = 5;
	DBusMessageIter iter;
	short rssi = DISTANCE_VAL_INVALID;

	while (retry_cnt--) {
		if (g_dbus_proxy_get_property(proxy, "RSSI", &iter) == FALSE) {
			usleep(10000); //10ms
			continue;
		}
		break;
	}

	if (retry_cnt >= 0)
		dbus_message_iter_get_basic(&iter, &rssi);

	return rssi;
}

static int a2dp_master_get_playrole(GDBusProxy *proxy)
{
	int ret = BTSRC_SCAN_PROFILE_INVALID;
	enum BT_Device_Class device_class;

	device_class = dist_dev_class(proxy);
	if (device_class == BT_Device_Class::BT_SINK_DEVICE)
		ret = BTSRC_SCAN_PROFILE_SINK;
	else if (device_class == BT_Device_Class::BT_SOURCE_DEVICE)
		ret = BTSRC_SCAN_PROFILE_SOURCE;

	return ret;
}

int a2dp_master_scan(void *arg, int len)
{
	BtScanParam *param = NULL;
	BtDeviceInfo *start = NULL;
	GDBusProxy *proxy;
	int ret = 0;
	int i;

	if (check_default_ctrl() == FALSE)
		return -1;

	param = (BtScanParam *)arg;
	if (len < sizeof(BtScanParam)) {
		pr_info("%s parameter error. BtScanParam setting is incorrect\n", __func__);
		return -1;
	}

	if(g_device_discovering) {
		pr_info("%s: devices discovering\n", __func__);
		return -1;
	}
	g_device_discovering = true;

	pr_info("=== scan on ===\n");
	cmd_scan("on");
	if (param->mseconds > 100) {
		pr_info("Waiting for Scan(%d ms)...\n", param->mseconds);
		usleep(param->mseconds * 1000);
	} else {
		pr_info("warning:%dms is too short, scan time is changed to 2s.\n",
			param->mseconds);
		usleep(2000 * 1000);
	}

	cmd_devices(param);
	pr_info("=== parse scan device (cnt:%d) ===\n", param->item_cnt);
	for (i = 0; i < param->item_cnt; i++) {
		start = &param->devices[i];
		proxy = find_device_by_address(start->address);
		if (!proxy) {
			pr_info("%s find_device_by_address failed!\n", __func__);
			continue;
		}
		/* Get bluetooth rssi */
		ret = a2dp_master_get_rssi(proxy);
		if (ret != DISTANCE_VAL_INVALID) {
			start->rssi = ret;
			start->rssi_valid = TRUE;
		}
		/* Get bluetooth AudioProfile */
		ret = a2dp_master_get_playrole(proxy);
		if (ret == BTSRC_SCAN_PROFILE_SINK)
			memcpy(start->playrole, "Audio Sink", strlen("Audio Sink"));
		else if (ret == BTSRC_SCAN_PROFILE_SOURCE)
			memcpy(start->playrole, "Audio Source", strlen("Audio Source"));
		else
			memcpy(start->playrole, "Unknow", strlen("Unknow"));
	}

	pr_info("=== scan off ===\n");
	cmd_scan("off");
	g_device_discovering = false;

	return 0;
}

int a2dp_master_connect(char *t_address)
{
	GDBusProxy *proxy;
	char address[18] = {'\0'};

	if (!t_address || (strlen(t_address) < 17)) {
		pr_info("ERROR: %s(len:%d) address error!\n", address, strlen(t_address));
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED);
		return -1;
	}
	memcpy(address, t_address, 17);

	if (check_default_ctrl() == FALSE) {
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED);
		return -1;
	}

	proxy = find_proxy_by_address(default_ctrl->devices, address);
	if (!proxy) {
		pr_info("Device %s not available\n", address);
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED);
		return -1;
	}

	if (g_dbus_proxy_method_call(proxy, "Connect", NULL, connect_reply,
							proxy, NULL) == FALSE) {
		pr_info("Failed to connect\n");
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED);
		return -1;
	}

	pr_info("Attempting to connect to %s\n", address);

	return 0;
}

void ble_disconn_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = (GDBusProxy *)user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("Failed to disconnect: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (proxy == ble_dev) {
		ble_dev = NULL;
		pr_info("Successful disconnected ble\n");
	} else {
		pr_info("Failed disconnected ble\n");
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

int ble_disconnect(void)
{
	if (!ble_dev) {
		pr_info("ble no connect\n");
		return 0;
	}

	if (g_dbus_proxy_method_call(ble_dev, "Disconnect", NULL, ble_disconn_reply,
							ble_dev, NULL) == FALSE) {
		pr_info("Failed to disconnect\n");
		return 0;
	}

	pr_info("Attempting to disconnect ble from %s\n", proxy_address(ble_dev));

	return 1;
}

static int disconnect_by_proxy(GDBusProxy *proxy)
{
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	if (g_dbus_proxy_method_call(proxy, "Disconnect", NULL, disconn_reply,
							proxy, NULL) == FALSE) {
		pr_info("Failed to disconnect\n");
		return -1;
	}

	pr_info("Attempting to disconnect from %s\n", proxy_address(proxy));
	return 0;
}

int a2dp_master_disconnect(char *address)
{
	if (!default_dev) {
		pr_info("%s: bt source no connect\n", __func__);
		return -1;
	}

	return disconnect_by_proxy(default_dev);
}

/*
 * Get the Bluetooth connection status.
 * Input parameters:
 *     Addr_buff -> if not empty, the interface will resolve the address
 *     of the current connection and store it in addr_buf.
 * return value:
 *    0-> not connected;
 *    1-> is connected;
 */
int a2dp_master_status(char *addr_buf, int addr_len, char *name_buf, int name_len)
{
	DBusMessageIter iter;
	const char *address;
	const char *name;

	if (!default_src_dev)
		return 0;

	if (addr_buf) {
		if (g_dbus_proxy_get_property(default_src_dev, "Address", &iter) == FALSE) {
			pr_info("WARING: Bluetooth connected, but can't get address!\n");
			return 0;
		}
		dbus_message_iter_get_basic(&iter, &address);
		memset(addr_buf, 0, addr_len);
		memcpy(addr_buf, address, (strlen(address) > addr_len) ? addr_len : strlen(address));
	}

	if (name_buf) {
		if (g_dbus_proxy_get_property(default_src_dev, "Alias", &iter) == FALSE) {
			pr_info("WARING: Bluetooth connected, but can't get device name!\n");
			return 0;
		}

		dbus_message_iter_get_basic(&iter, &name);
		memset(name_buf, 0, name_len);
		memcpy(name_buf, name, (strlen(name) > name_len) ? name_len : strlen(name));
	}

	return 1;
}

int a2dp_master_remove(char *t_address)
{
	GDBusProxy *proxy;
	char address[18] = {'\0'};

	if (check_default_ctrl() == FALSE)
		return -1;

	if (strcmp(t_address, "*") == 0) {
		GList *list;

		for (list = default_ctrl->devices; list; list = g_list_next(list)) {
			GDBusProxy *proxy = (GDBusProxy *)list->data;
			remove_device(proxy);
		}
		return 0;
	} else if (!t_address || (strlen(t_address) < 17)) {
		pr_info("ERROR: %s address error!\n", t_address);
		return -1;
	}

	memcpy(address, t_address, 17);
	proxy = find_proxy_by_address(default_ctrl->devices, address);
	if (!proxy) {
		pr_info("Device %s not available\n", address);
		return -1;
	}

	remove_device(proxy);
	return 0;
}

static int a2dp_master_save_status(char *address)
{
	char buff[100] = {0};
	struct sockaddr_un serverAddr;
	int snd_cnt = 3;
	int sockfd;

	sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		pr_info("FUNC:%s create sockfd failed!\n", __func__);
		return 0;
	}

	serverAddr.sun_family = AF_UNIX;
	strcpy(serverAddr.sun_path, "/tmp/a2dp_master_status");
	memset(buff, 0, sizeof(buff));

	if (address)
		sprintf(buff, "status:connect;address:%s;", address);
	else
		sprintf(buff, "status:disconnect;");

	while(snd_cnt--) {
		sendto(sockfd, buff, strlen(buff), MSG_DONTWAIT, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
		usleep(1000); //5ms
	}

	close(sockfd);
	return 0;
}

void a2dp_master_event_send(RK_BT_SOURCE_EVENT event)
{
	if(g_bt_callback.bt_source_event_cb)
		g_bt_callback.bt_source_event_cb(g_btmaster_userdata, event);
}

void a2dp_master_register_cb(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
	g_bt_callback.bt_source_event_cb = cb;
	g_btmaster_userdata = userdata;
	return;
}

void a2dp_master_deregister_cb()
{
	g_bt_callback.bt_source_event_cb = NULL;
	g_btmaster_userdata = NULL;
	return;
}

/**********************************************
 *      bt source avrcp
 **********************************************/
static int g_bt_source_avrcp_thread_runing;
static pthread_t g_bt_source_avrcp_thread;

static int is_bluealsa_event(char *node)
{
	char sys_path[100] = {0};
	char node_info[100] = {0};
	int fd = 0;

	if (strncmp(node, "event", 5))
		return 0;

	sprintf(sys_path, "sys/class/input/%s/device/name", node);
	fd = open(sys_path, O_RDONLY);
	if (fd < 0)
		return 0;

	if (read(fd, node_info, sizeof(node_info)) < 0) {
		close(fd);
		return 0;
	}

	/* BlueAlsa addr like XX:XX:XX:XX:XX:XX */
	if ((strlen(node_info) == 18) &&
		(node_info[2] == ':') && (node_info[5] == ':') &&
		(node_info[8] == ':') && (node_info[11] == ':') &&
		(node_info[14] == ':')) {
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}


static int get_input_event_id()
{
	DIR *dir;
	struct dirent *ptr;
	char node_name[7]; //eventXX
	int id = -1;

	if ((dir = opendir("/dev/input")) == NULL) {
		pr_info("ERROR: %s Open dir \"/dev/input\" error\n", __func__);
		return -1;
	}

	while ((ptr = readdir(dir)) != NULL) {
		if ((strcmp(ptr->d_name, ".") == 0) || (strcmp(ptr->d_name, "..") == 0) || //current dir OR parrent dir
			(ptr->d_type == 10) || (ptr->d_type == 4)) { //link file or dir
			continue;
		} else if ((ptr->d_type == 8) || (ptr->d_type == 2)) { //file
			if (strncmp(ptr->d_name, "event", 5) != 0)
				continue;

			if (is_bluealsa_event(ptr->d_name)) {
				memset(node_name, 0, sizeof(node_name));
				memcpy(node_name, ptr->d_name, strlen(ptr->d_name));
				if ((node_name[5] >= '0') && (node_name[5] <= '9'))
					id = node_name[5] - '0';
				else if ((node_name[6] >= '0') && (node_name[6] <= '9'))
					id = id * 10 + (node_name[6] - '0');
				break;
			}
		}
	}
	closedir(dir);

	return id;
}

static void *bt_source_listen_avrcp_event(void *arg)
{
	fd_set rfds;
	int ret, fd, id;
	struct input_event ev_key;
	struct timeval tv;
	char path[100] = {0};
	int try_cnt = 30;

	pr_info("### %s start...\n", __func__);
	while ((try_cnt--) && g_bt_source_avrcp_thread_runing) {
		id = get_input_event_id();
		if (id >= 0) {
			sprintf(path, "/dev/input/event%d", id);
			fd = open(path, O_RDONLY);
			if (fd > 0)
				g_bt_source_avrcp_thread_runing = 1;
			else
				pr_info("WARNING: %s open %s failed!\n", __func__, path);

			break;
		}

		usleep(200000); /* 100ms */
		pr_info("INFO: %s waite for bt source avrcp event node. 200ms\n", __func__);
	}

	tv.tv_sec = 0;
	tv.tv_usec = 100000;/* 100ms */
	while (g_bt_source_avrcp_thread_runing) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		if (select(fd + 1, &rfds, NULL, NULL, &tv) < 0) {
			pr_info("ERROR:%s wait bt source avrcp event failed!\n", __func__);
			break;
		}

		if (FD_ISSET(fd, &rfds) == 0)
			continue;

		ret = read(fd, &ev_key, sizeof(ev_key));
		if (ret == -1) {
			pr_info("ERROR:%s read bt source avrcp event failed!\n", __func__);
			break;
		}

		/* ignore illegal key code and only key down is captured */
		if ((ev_key.code == 0) || (ev_key.value != 1))
			continue;

		switch(ev_key.code) {
			case KEY_PLAYCD:
				a2dp_master_event_send(BT_SOURCE_EVENT_RC_PLAY);
				break;
			case KEY_PAUSECD:
				a2dp_master_event_send(BT_SOURCE_EVENT_RC_PAUSE);
				break;
			case KEY_VOLUMEUP:
				a2dp_master_event_send(BT_SOURCE_EVENT_RC_VOL_UP);
				break;
			case KEY_VOLUMEDOWN:
				a2dp_master_event_send(BT_SOURCE_EVENT_RC_VOL_DOWN);
				break;
			case KEY_NEXTSONG:
				a2dp_master_event_send(BT_SOURCE_EVENT_RC_BACKWARD);
				break;
			case KEY_PREVIOUSSONG:
				a2dp_master_event_send(BT_SOURCE_EVENT_RC_FORWARD);
				break;
			default:
				break;
		}
	}

	if (fd > 0)
		close(fd);

	pr_info("### %s end...\n", __func__);
	return NULL;
}

int a2dp_master_avrcp_open()
{
	int ret = -1;

	g_bt_source_avrcp_thread_runing = 1;
	if (!g_bt_source_avrcp_thread)
		pthread_create(&g_bt_source_avrcp_thread, NULL, bt_source_listen_avrcp_event, NULL);

	return ret;
}

int a2dp_master_avrcp_close()
{
	pr_info("### %s start...\n", __func__);
	g_bt_source_avrcp_thread_runing = 0;
	if (g_bt_source_avrcp_thread)
		pthread_join(g_bt_source_avrcp_thread, NULL);

	g_bt_source_avrcp_thread = 0;
	pr_info("### %s end...\n", __func__);
	return 0;
}

static void save_last_device(GDBusProxy *proxy)
{
	int fd;
	const char *object_path, *address;
	DBusMessageIter iter, class_iter;
	dbus_uint32_t valu32;
	char buff[512] = {0};

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE) {
		pr_info("Get adapter address error");
		return;
	}

	dbus_message_iter_get_basic(&iter, &address);
	pr_info("Connected device address: %s", address);

	if (g_dbus_proxy_get_property(proxy, "Class", &class_iter) == FALSE) {
		pr_info("Get adapter Class error");
		return;
	}

	dbus_message_iter_get_basic(&class_iter, &valu32);
	pr_info("Connected device class: 0x%x\n", valu32);

	object_path = g_dbus_proxy_get_path(proxy);
	pr_info("Connected device object path: %s\n", object_path);

	fd = open(BT_RECONNECT_CFG, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		pr_info("Open %s error: %s\n", BT_RECONNECT_CFG, strerror(errno));
		return;
	}

	sprintf(buff, "ADDRESS:%s;CLASS:%x;PATH:%s;", address, valu32, object_path);
	write(fd, buff, strlen(buff));
	fsync(fd);
	close(fd);
}

static int load_last_device(char *address)
{
	int fd, ret, i;
	char buff[512] = {0};
	char *start = NULL, *end = NULL;

	pr_info("Load path %s\n", BT_RECONNECT_CFG);

	ret = access(BT_RECONNECT_CFG, F_OK);
	if (ret == -1) {
		pr_info("%s does not exist\n", BT_RECONNECT_CFG);
		return -1;
	}

	fd = open(BT_RECONNECT_CFG, O_RDONLY);
	if (fd == -1) {
		pr_info("Open %s error: %s\n", BT_RECONNECT_CFG, strerror(errno));
		return -1;
	}

	ret = read(fd, buff, sizeof(buff));
	if (ret < 0) {
		pr_info("read %s error: %s\n", BT_RECONNECT_CFG, strerror(errno));
		return -1;
	}

	start = strstr(buff, "ADDRESS:");
	end = strstr(buff, ";");
	if (!start || !end || (end < start)) {
		pr_info("file %s content invalid(address): %s\n", BT_RECONNECT_CFG, buff);
		return -1;
	}
	start += strlen("ADDRESS:");
	if (address)
		memcpy(address, start, end - start);

	return 0;
}

static void reconn_last_device_reply(DBusMessage * message, void *user_data)
{
	DBusError err;

	dbus_error_init(&err);
	if (dbus_set_error_from_message(&err, message) == TRUE) {
		pr_info("Reconnect failed!\n");
		dbus_error_free(&err);
	}
}

int reconn_last_devices(BtDeviceType type)
{
	GDBusProxy *proxy;
	DBusMessageIter addr_iter, addrType_iter;
	int fd, ret, reconnect = 1;
	char buff[100] = {0};
	char address[48] = {0};
	enum BT_Device_Class device_class;

	fd = open("/userdata/cfg/bt_reconnect", O_RDONLY);
	if (fd > 0) {
		ret = read(fd, buff, sizeof(buff));
		if (ret > 0) {
			if (strstr(buff, "bluez-reconnect:disable"))
				reconnect = 0;
		}
		close(fd);
	}

	if (reconnect == 0) {
		pr_info("%s: automatic reconnection is disabled!\n", __func__);
		return 0;
	}

	if (bt_is_connected()) {
		pr_info("%s: The device is connected and does not need to be reconnected!\n", __func__);
		return 0;
	}

	ret = load_last_device(address);
	if (ret < 0)
		return ret;

	proxy = find_device_by_address(address);
	if (!proxy) {
		pr_info("Invalid proxy, stop reconnecting\n");
		return -1;
	}

	device_class = dist_dev_class(proxy);
	if (device_class == BT_Device_Class::BT_IDLE) {
		pr_info("Invalid device_class, stop reconnecting\n");
		return -1;
	}

	switch(type) {
		case BT_DEVICES_A2DP_SINK:
			if (device_class != BT_Device_Class::BT_SINK_DEVICE)
				reconnect = 0;
			break;
		case BT_DEVICES_A2DP_SOURCE:
			if (device_class != BT_Device_Class::BT_SOURCE_DEVICE)
				reconnect = 0;
			break;
		case BT_DEVICES_BLE:
			if (device_class != BT_Device_Class::BT_BLE_DEVICE)
				reconnect = 0;
			break;
		case BT_DEVICES_HFP:
			if (device_class != BT_Device_Class::BT_SOURCE_DEVICE)
				reconnect = 0;
			break;
		case BT_DEVICES_SPP:
			break;
		default:
			reconnect = 0;
	}

	if (reconnect == 0) {
		pr_info("Unable to find a suitable reconnect device!\n");
		return -1;
	}

	if (g_dbus_proxy_method_call(proxy, "Connect", NULL,
		reconn_last_device_reply, NULL, NULL) == FALSE) {
		pr_info("Failed to call org.bluez.Device1.Connect\n");
		return -1;
	}

	return 0;
}

int disconnect_current_devices()
{
	if (!default_dev) {
		pr_info("%s: No connected device\n", __func__);
		return -1;
	}

	return disconnect_by_proxy(default_dev);
}

int get_dev_platform(char *address)
{
	int vendor, platform = DEV_PLATFORM_UNKNOWN;
	char *str;
	const char *valstr;
	GDBusProxy *proxy;
	DBusMessageIter iter;

	if(!address) {
		pr_info("%s: Invalid address\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	proxy = find_device_by_address(address);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	if (g_dbus_proxy_get_property(proxy, "Modalias", &iter) == FALSE) {
		pr_info("%s: WARING: can't get Modalias!\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	dbus_message_iter_get_basic(&iter, &valstr);
	pr_info("%s: Modalias valstr = %s\n", __func__, valstr);

	str = strstr(valstr, "v");
	if(str) {
		if(!strncasecmp(str + 1, "004c", 4))
			vendor = IOS_VENDOR_SOURCE_BT;
		else if(!strncasecmp(str + 1, "05ac", 4))
			vendor = IOS_VENDOR_SOURCE_USB;
	}

	if(vendor == IOS_VENDOR_SOURCE_BT || vendor == IOS_VENDOR_SOURCE_USB)
		platform = DEV_PLATFORM_IOS;

	pr_info("%s: %s is %s\n", __func__, address,
		platform == DEV_PLATFORM_UNKNOWN ? "Unknown Platform" : "Apple IOS");

	return platform;
}

int get_current_dev_platform()
{
	if (!default_dev) {
		pr_info("%s: No connected device\n", __func__);
		return DEV_PLATFORM_UNKNOWN;
	}

	return get_dev_platform(proxy_address(default_dev));
}

static void connect_by_address_reply(DBusMessage *message, void *user_data)
{
	GDBusProxy *proxy = (GDBusProxy*)user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		pr_info("%s: Failed to connect: %s\n", __func__, error.name);
		dbus_error_free(&error);
		//set_default_device(NULL, NULL);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	pr_info("%s: Connection successful\n", __func__);
	set_default_device(proxy, NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

int connect_by_address(char *addr)
{
	GDBusProxy *proxy;

	if (!addr || (strlen(addr) < 17)) {
		pr_info("%s: address(%s) error!\n", __func__, addr);
		return -1;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	if (g_dbus_proxy_method_call(proxy, "Connect", NULL,
		connect_by_address_reply, proxy, NULL) == FALSE) {
		pr_info("%s: Failed to call org.bluez.Device1.Connect\n", __func__);
		return -1;
	}

	return 0;
}

int disconnect_by_address(char *addr)
{
	GDBusProxy *proxy;

	if (!addr || (strlen(addr) < 17)) {
		pr_info("%s: address(%s) error!\n", __func__, addr);
		return -1;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	return disconnect_by_proxy(proxy);
}

void bt_display_devices()
{
	cmd_devices(NULL);
}

void bt_display_paired_devices()
{
	cmd_paired_devices();
}

RkBtPraiedDevice *bt_create_one_paired_dev(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	const char *address, *name;
	dbus_int16_t rssi = -100;
	dbus_bool_t is_connected = FALSE;

	RkBtPraiedDevice *new_device = (RkBtPraiedDevice*)malloc(sizeof(RkBtPraiedDevice));

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &address);
	else
		address = "<unknown>";

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter) == TRUE)
		dbus_message_iter_get_basic(&iter, &is_connected);
	else
		pr_info("can't get Connected status\n");

	new_device->remote_address = (char *)malloc(strlen(address) + 1);
	strncpy(new_device->remote_address, address, strlen(address));
	new_device->remote_address[strlen(address)] = '\0';

	new_device->remote_name = (char *)malloc(strlen(name) + 1);
	strncpy(new_device->remote_name, name, strlen(name));
	new_device->remote_name[strlen(name)] = '\0';

	new_device->is_connected = is_connected;
	new_device->next = NULL;

	return new_device;
}

static int list_paired_dev_push_back(RkBtPraiedDevice **dev_list, GDBusProxy *proxy)
{
	if(dev_list == NULL) {
		pr_info("%s: invalid dev_list\n", __func__);
		return -1;
	}

	if(*dev_list == NULL) {
		*dev_list = bt_create_one_paired_dev(proxy);
	} else {
		RkBtPraiedDevice *cur_dev = *dev_list;
		while(cur_dev->next != NULL)
			cur_dev = cur_dev->next;

		RkBtPraiedDevice *new_dev = bt_create_one_paired_dev(proxy);
		cur_dev->next = new_dev;
	}

	return 0;
}

int bt_get_paired_devices(RkBtPraiedDevice **dev_list, int *count)
{
	GList *ll;

	*count = 0;

	if (check_default_ctrl() == FALSE)
		return -1;

	for (ll = g_list_first(default_ctrl->devices);
			ll; ll = g_list_next(ll)) {
		GDBusProxy *proxy = (GDBusProxy *)ll->data;
		DBusMessageIter iter;
		dbus_bool_t paired;

		if (g_dbus_proxy_get_property(proxy, "Paired", &iter) == FALSE)
			continue;

		dbus_message_iter_get_basic(&iter, &paired);
		if (!paired)
			continue;

		if(!list_paired_dev_push_back(dev_list, proxy))
			(*count)++;
	}

	return 0;
}

int bt_free_paired_devices(RkBtPraiedDevice *dev_list)
{
	RkBtPraiedDevice *dev_tmp = NULL;

	if(dev_list == NULL) {
		pr_info("%s: dev_list is empty, don't need to clear\n", __func__);
		return -1;
	}

	while(dev_list->next != NULL) {
		pr_info("%s: free dev: %s\n", __func__, dev_list->remote_address);
		dev_tmp = dev_list->next;
		free(dev_list->remote_address);
		free(dev_list->remote_name);
		free(dev_list);
		dev_list = dev_tmp;
	}

	if(dev_list != NULL) {
		pr_info("%s: last free dev: %s\n", __func__, dev_list->remote_address);
		free(dev_list->remote_address);
		free(dev_list->remote_name);
		free(dev_list);
		dev_list = NULL;
	}

	return 0;
}

int pair_by_addr(char *addr)
{
	GDBusProxy *proxy;
	char dev_name[256];

	if (!addr || (strlen(addr) < 17)) {
		pr_info("%s: address(%s) error!\n", __func__, addr);
		return -1;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	bt_get_device_name_by_proxy(proxy, dev_name, 256);
	bt_bond_state_send(addr, dev_name, RK_BT_BOND_STATE_BONDING);

	return cmd_pair(proxy);
}

int unpair_by_addr(char *addr)
{
	GDBusProxy *proxy;
	char *path;

	if (!addr || (strlen(addr) < 17)) {
		pr_info("%s: address(%s) error!\n", __func__, addr);
		return -1;
	}

	proxy = find_device_by_address(addr);
	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	/* There is no direct unpair method, removing device will clear pairing information */
	return remove_device(proxy);
}

int bt_set_device_name(char *name)
{
	if (!name) {
		pr_info("%s: Invalid bt name: %s\n", __func__, name);
		return -1;
	}

	if (check_default_ctrl() == FALSE)
		return -1;

	if (!default_ctrl->proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Alias",
					DBUS_TYPE_STRING, &name,
					generic_callback, name, NULL) == FALSE) {
		pr_info("%s: set Alias property error\n", __func__);
		return -1;
	}

	pr_info("%s: Attempting to set device name %s\n", __func__, name);
	return 0;
}

static int bt_get_device_name_by_proxy(GDBusProxy *proxy,
			char *name_buf, int name_len)
{
	DBusMessageIter iter;
	const char *name;

	memset(name_buf, 0, name_len);

	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	if (!name_buf || name_len <= 0) {
		pr_info("%s: Invalid name buffer, name_len: %d\n", __func__, name_len);
		return -1;
	}

	if (g_dbus_proxy_get_property(proxy, "Alias", &iter) == FALSE) {
		pr_info("WARING: Bluetooth connected, but can't get device name!\n");
		return -1;
	}

	dbus_message_iter_get_basic(&iter, &name);
	memcpy(name_buf, name, (strlen(name) > name_len) ? name_len : strlen(name));

	return 0;
}

int bt_get_device_name(char *name_buf, int name_len)
{
	if (check_default_ctrl() == FALSE)
		return -1;

	if (!default_ctrl->proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	return bt_get_device_name_by_proxy(default_ctrl->proxy, name_buf, name_len);
}

static int bt_get_device_addr_by_proxy(GDBusProxy *proxy,
			char *addr_buf, int addr_len)
{
	DBusMessageIter iter;
	const char *address;

	memset(addr_buf, 0, addr_len);

	if (!proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	if (!addr_buf || addr_len < 17) {
		pr_info("%s: Invalid address buffer, addr_len: %d\n", __func__, addr_len);
		return -1;
	}

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE) {
		pr_info("WARING: Bluetooth connected, but can't get address!\n");
		return -1;
	}

	dbus_message_iter_get_basic(&iter, &address);
	memcpy(addr_buf, address, (strlen(address) > addr_len) ? addr_len : strlen(address));

	return 0;
}

int bt_get_device_addr(char *addr_buf, int addr_len)
{
	if (check_default_ctrl() == FALSE)
		return -1;

	if (!default_ctrl->proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return -1;
	}

	return bt_get_device_addr_by_proxy(default_ctrl->proxy, addr_buf, addr_len);
}


int bt_get_default_dev_addr(char *addr_buf, int addr_len)
{
	if (!default_dev) {
		pr_info("%s: no connected device\n", __func__);
		return -1;
	}

	return bt_get_device_addr_by_proxy(default_dev, addr_buf, addr_len);
}

static void *bt_scan_devices(void *arg)
{
	unsigned int scan_time = 0;

	pr_info("=== scan on ===\n");
	if(cmd_scan("on") < 0) {
		bt_discovery_state_send(RK_BT_DISC_START_FAILED);
		goto done;
	}

	while(g_device_discovering) {
		usleep(1000 * 1000);
		scan_time += 1000;
		if(scan_time >= g_scan_time) {
			pr_info("%s: the scan is complete\n", __func__);
			break;
		}
	}

	bt_cancel_discovery(RK_BT_DISC_STOPPED_AUTO);

done:
	pr_info("%s: Exit bt scan thread\n", __func__);
	return NULL;
}

int bt_start_discovery(unsigned int mseconds)
{
	int ret;

	if(g_device_discovering) {
		pr_info("%s: devices discovering\n", __func__);
		return -1;
	}

	g_device_discovering = true;

	if (mseconds < 1000) {
		pr_info("%s: %d ms is too short, scan time is changed to 2s.\n", __func__, mseconds);
		g_scan_time = 2000;
	} else {
		pr_info("%s: scan time: %d\n", __func__, mseconds);
		g_scan_time = mseconds;
	}

	ret = pthread_create(&g_scan_thread, NULL, bt_scan_devices, NULL);
	if (ret) {
		pr_info("%s: scan thread create failed!\n", __func__);
		bt_discovery_state_send(RK_BT_DISC_START_FAILED);
		return -1;
	}

	pthread_setname_np(g_scan_thread, "scan_thread");
	pr_info("%s scan_thread tid: %lu\n", __func__, g_scan_thread);

	return 0;
}

int bt_cancel_discovery(RK_BT_DISCOVERY_STATE state)
{
	if(!g_device_discovering) {
		pr_info("%s: discovery canceling or canceled\n", __func__);
		return -1;
	}

	g_device_discovering = false;
	if (g_scan_thread) {
		pthread_join(g_scan_thread, NULL);
		g_scan_thread = 0;
	}

	g_scan_time = 0;

	pr_info("=== scan off ===\n");
	cmd_scan("off");
	bt_discovery_state_send(state);
	return 0;
}

bool bt_is_discovering()
{
	DBusMessageIter iter;
	dbus_bool_t valbool;

	if (check_default_ctrl() == FALSE)
		return false;

	if (!default_ctrl->proxy) {
		pr_info("%s: Invalid proxy\n", __func__);
		return false;
	}

	if (g_dbus_proxy_get_property(default_ctrl->proxy, "Discovering", &iter) == FALSE) {
		pr_info("WARING: Bluetooth connected, but can't get Discovering!\n");
		return false;
	}

	dbus_message_iter_get_basic(&iter, &valbool);

	return valbool;
}

/*
 * / # hcitool con
 * Connections:
 *      > ACL 64:A2:F9:68:1E:7E handle 1 state 1 lm SLAVE AUTH ENCRYPT
 *      > LE 60:9C:59:31:7F:B9 handle 16 state 1 lm SLAVE
 */
bool bt_is_connected()
{
	bool ret = false;
	char buf[1024];

	memset(buf, 0, 1024);
	RK_shell_exec("hcitool con", buf, 1024);
	usleep(300000);

	if (strstr(buf, "ACL") || strstr(buf, "LE"))
		ret = true;

	return ret;
}
