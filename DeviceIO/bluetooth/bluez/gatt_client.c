#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include <unistd.h>
#include <stdlib.h>

#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/gatt.h"
#include "utility.h"
#include "slog.h"
#include "gatt_client.h"

extern struct adapter *default_ctrl;
extern GDBusProxy *ble_dev;
extern GDBusProxy *default_attr;

typedef struct {
	RK_BLE_CLIENT_STATE_CALLBACK state_cb;
	RK_BT_DEV_FOUND_CALLBACK ble_dev_found_cb;
	RK_BLE_CLIENT_RECV_CALLBACK recv_cb;
	RK_BLE_CLIENT_STATE state;
} gatt_client_control_t;

static gatt_client_control_t g_gatt_client_ctl = {
	NULL, NULL, NULL, RK_BLE_CLIENT_STATE_IDLE,
};

void gatt_client_register_state_callback(RK_BLE_CLIENT_STATE_CALLBACK cb)
{
	g_gatt_client_ctl.state_cb = cb;
}

void gatt_client_register_dev_found_callback(RK_BT_DEV_FOUND_CALLBACK cb)
{
	g_gatt_client_ctl.ble_dev_found_cb = cb;
}

void gatt_client_register_recv_callback(RK_BLE_CLIENT_RECV_CALLBACK cb)
{
	g_gatt_client_ctl.recv_cb = cb;
}

void gatt_client_state_send(RK_BLE_CLIENT_STATE state)
{
	if(g_gatt_client_ctl.state_cb)
		g_gatt_client_ctl.state_cb(state);

	g_gatt_client_ctl.state = state;
}

void gatt_client_recv_data_send(GDBusProxy *proxy, DBusMessageIter *iter)
{
	DBusMessageIter array, uuid_iter;
	const char *uuid;
	uint8_t *value;
	int len;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) {
		pr_info("%s: Unable to get value\n", __func__);
		return;
	}

	dbus_message_iter_recurse(iter, &array);
	dbus_message_iter_get_fixed_array(&array, &value, &len);

	if (len < 0) {
		pr_info("%s: Unable to parse value\n", __func__);
		return;
	}

	if (g_dbus_proxy_get_property(proxy, "UUID", &uuid_iter) == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	dbus_message_iter_get_basic(&uuid_iter, &uuid);

	if(g_gatt_client_ctl.recv_cb)
		g_gatt_client_ctl.recv_cb(uuid, value, len);
}

RK_BLE_CLIENT_STATE gatt_client_get_state()
{
	return g_gatt_client_ctl.state;
}

void gatt_client_dev_found_send(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	const char *addressType;

	if(!bt_is_discovering() || !g_gatt_client_ctl.ble_dev_found_cb)
		return;

	if (g_dbus_proxy_get_property(proxy, "AddressType", &iter) == FALSE)
		return;

	dbus_message_iter_get_basic(&iter, &addressType);
	if (strcmp(addressType, "random") != 0) {
		pr_info("%s The device isn't ble\n", __func__);
		return;
	}

	dev_found_send(proxy, g_gatt_client_ctl.ble_dev_found_cb);
}

void gatt_client_open()
{
}

void gatt_client_close()
{
	pr_info("%s\n", __func__);
	g_gatt_client_ctl.state = RK_BLE_CLIENT_STATE_IDLE;
	g_gatt_client_ctl.ble_dev_found_cb = NULL;
	g_gatt_client_ctl.recv_cb = NULL;
	g_gatt_client_ctl.state_cb = NULL;
}

int gatt_client_get_service_info(char *address, RK_BLE_CLIENT_SERVICE_INFO *info)
{
	GDBusProxy *proxy;

	if(!address || !info) {
		pr_err("%s: Invalid parameters\n", __func__);
		return -1;
	}

	proxy = find_device_by_address(address);
	if (!proxy) {
		pr_info("%s: can't find device(%s)\n", __func__, address);
		return -1;
	}

	memset(info, 0, sizeof(RK_BLE_CLIENT_SERVICE_INFO));
	gatt_get_list_attributes(g_dbus_proxy_get_path(proxy), info);
	return 0;
}

static int gatt_client_select_attribute(char *uuid)
{
	GDBusProxy *proxy;

	if (!ble_dev) {
		pr_info("%s: No ble client connected\n", __func__);
		return -1;
	}

	proxy = gatt_select_attribute(NULL, uuid);
	if (proxy) {
		set_default_attribute(proxy);
		return 0;
	}

	return -1;
}

int gatt_client_read(char *uuid, int offset)
{
	if(!uuid) {
		pr_err("%s: Invalid uuid\n", __func__);
		return -1;
	}

	if(gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		return -1;
	}

	return gatt_read_attribute(default_attr, offset);
}

int gatt_client_write(char *uuid, char *data, int offset)
{
	if(!uuid || !data) {
		pr_err("%s: Invalid uuid or data\n", __func__);
		return -1;
	}

	if(gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		return -1;
	}

	return gatt_write_attribute(default_attr, data, offset);
}

bool gatt_client_is_notifying(const char *uuid)
{
	if(!uuid) {
		pr_err("%s: Invalid uuid\n", __func__);
		return -1;
	}

	if(gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		return -1;
	}

	return gatt_get_notifying(default_attr);
}

int gatt_client_notify(const char *uuid, bool enable)
{
	if(!uuid) {
		pr_err("%s: Invalid uuid\n", __func__);
		return -1;
	}

	if(gatt_client_select_attribute(uuid) < 0) {
		pr_err("%s: select attribute failed\n", __func__);
		return -1;
	}

	return gatt_notify_attribute(default_attr, enable ? true : false);
}