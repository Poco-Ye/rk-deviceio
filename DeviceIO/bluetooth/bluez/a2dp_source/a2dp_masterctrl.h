#ifndef __A2DP_SOURCE_CTRL__
#define __A2DP_SOURCE_CTRL__

#include "DeviceIo/RkBtBase.h"
#include "DeviceIo/RkBtSource.h"

typedef enum _bt_devices_type {
	BT_DEVICES_A2DP_SINK,
	BT_DEVICES_A2DP_SOURCE,
	BT_DEVICES_BLE,
	BT_DEVICES_HFP,
	BT_DEVICES_SPP,
} BtDeviceType;

int bt_open(RkBtContent *bt_content);
int bt_close();
int init_a2dp_master_ctrl();
int release_a2dp_master_ctrl();
int a2dp_master_scan(void *data, int len);
int a2dp_master_connect(char *address);
int a2dp_master_disconnect(char *address);
int a2dp_master_status(char *addr_buf, int addr_len, char *name_buf, int name_len);
int a2dp_master_remove(char *address);
void a2dp_master_register_cb(void *userdata, RK_BT_SOURCE_CALLBACK cb);
void a2dp_master_clear_cb();
int a2dp_master_avrcp_open();
int a2dp_master_avrcp_close();
int reconn_last_devices(BtDeviceType type);
int disconnect_current_devices();

int ble_disconnect(void);

#endif /* __A2DP_SOURCE_CTRL__ */
