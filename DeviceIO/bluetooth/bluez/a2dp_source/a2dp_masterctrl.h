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
int connect_by_address(char *addr);
int disconnect_by_address(char *addr);
int pair_by_addr(char *addr);
int unpair_by_addr(char *addr);
int bt_set_device_name(char *name);
int bt_get_device_name(char *name_buf, int name_len);
int bt_get_device_addr(char *addr_buf, int addr_len);
int bt_get_default_dev_addr(char *addr_buf, int addr_len);
void bt_display_devices();
void bt_display_paired_devices();
int bt_get_paired_devices(bt_paried_device **dev_list, int *count);
int bt_free_paired_devices(bt_paried_device **dev_list);
void bt_start_discovery(unsigned int mseconds);
void bt_cancel_discovery();
bool bt_is_discovering();
bool bt_is_connected();
void bt_register_bond_callback(RK_BT_BOND_CALLBACK cb);
void bt_deregister_bond_callback();

int ble_disconnect(void);

#endif /* __A2DP_SOURCE_CTRL__ */
