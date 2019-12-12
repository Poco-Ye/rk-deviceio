#ifndef __BT_GATT_CLITEN_H__
#define __BT_GATT_CLITEN_H__

#include "gdbus/gdbus.h"

#include <DeviceIo/RkBtBase.h>
#include <DeviceIo/RkBleClient.h>

#ifdef __cplusplus
extern "C" {
#endif

void gatt_client_register_state_callback(RK_BLE_CLIENT_STATE_CALLBACK cb);
void gatt_client_register_recv_callback(RK_BLE_CLIENT_RECV_CALLBACK cb);
void gatt_client_register_dev_found_callback(RK_BT_DEV_FOUND_CALLBACK cb);
void gatt_client_state_send(RK_BLE_CLIENT_STATE state);
void gatt_client_recv_data_send(GDBusProxy *proxy, DBusMessageIter *iter);
void gatt_client_dev_found_send(GDBusProxy *proxy);
RK_BLE_CLIENT_STATE gatt_client_get_state();
void gatt_client_open();
void gatt_client_close();
int gatt_client_get_service_info(char *address, RK_BLE_CLIENT_SERVICE_INFO *info);
int gatt_client_read(char *uuid, int offset);
int gatt_client_write(char *uuid, char *data, int offset);
bool gatt_client_is_notifying(const char *uuid);
int gatt_client_notify(const char *uuid, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __BT_GATT_CLITEN_H__ */