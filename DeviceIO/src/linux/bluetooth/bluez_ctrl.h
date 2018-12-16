#ifndef __BLUEZ_CTRL_P__
#define __BLUEZ_CTRL_P__

#include "avrcpctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/shell.h"
#include "DeviceIo/DeviceIo.h"
#include "DeviceIo/RkBtMaster.h"
#include "DeviceIo/RkBtSink.h"

using DeviceIOFramework::ble_config;
using DeviceIOFramework::bt_adv_set;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

enum class BtControlType {
	BT_NONE = 0,
	BT_SINK,
	BT_SOURCE,
	BT_BLE_MODE,
	BLE_SINK_BLE_MODE,
	BLE_WIFI_INTRODUCER
};

typedef struct {
	pthread_t tid;
	int is_bt_open;
	int is_ble_open;
	int is_a2dp_sink_open;
	int is_a2dp_source_open;
	bool is_ble_sink_coexist;
	BtControlType type;
	BtControlType last_type;
} bt_control_t;

bool ble_is_open();
bool bt_source_is_open(void);
bool bt_sink_is_open(void);
int bt_interface(BtControl type, void *data);
int bt_close_sink(void);
int bt_close_source(void);
int rk_bt_control(BtControl cmd, void *data, int len);
int bt_control_cmd_send(enum BtControl bt_ctrl_cmd);

#define msleep(x) usleep(x * 1000)

#endif /* __BLUEZ_CTRL_P__ */
