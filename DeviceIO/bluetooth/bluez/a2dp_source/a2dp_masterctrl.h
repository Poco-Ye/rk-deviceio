#ifndef __A2DP_SOURCE_CTRL__
#define __A2DP_SOURCE_CTRL__

#include "DeviceIo/RkBtBase.h"
#include "DeviceIo/RkBtSource.h"

#define DEV_PLATFORM_UNKNOWN    0 /* unknown platform */
#define DEV_PLATFORM_IOS        1 /* Apple iOS */
#define IOS_VENDOR_SOURCE_BT    76 /* Bluetooth SIG, apple id = 0x4c */
#define IOS_VENDOR_SOURCE_USB   1452 /* USB Implementer's Forum, apple id = 0x05ac */

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
int get_dev_platform(char *address);
int get_current_dev_platform();

int ble_disconnect(void);

#endif /* __A2DP_SOURCE_CTRL__ */
