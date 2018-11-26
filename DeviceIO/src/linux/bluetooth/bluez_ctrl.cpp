/*
 * Copyright (c) 2018 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "../Logger.h"
#include "../shell.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <unistd.h>
#include <signal.h>
#include "avrcpctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"

#include "DeviceIo/DeviceIo.h"

using DeviceIOFramework::BtControl;

int rk_bt_control(BtControl cmd, void *data, int len);

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

#define dbg(fmt, ...) APP_DEBUG("[rk bt debug ]" fmt, ##__VA_ARGS__)
#define err(fmt, ...) APP_ERROR("[rk bt error ]" fmt, ##__VA_ARGS__)

/* as same as APP_BLE_WIFI_INTRODUCER_GATT_ATTRIBUTE_SIZE */
#define BLE_SOCKET_RECV_LEN 22

#ifdef BLUEZ5_UTILS
static char sock_path[] = "/data/bluez5_utils/socket_dueros";
#else
static char sock_path[] = "/data/bsa/config/socket_dueros";
#endif

#define BT_STATUS_PATH "/data/bsa/config/bt_config.xml"

enum class BtControlType {
    BT_NONE = 0,
    BT_AUDIO_PLAY,
    BLE_WIFI_INTRODUCER
};

typedef struct {
    pthread_t tid;
    int socket_recv_done;
    bool is_bt_open;
    int is_ble_open;
    int is_a2dp_open;
    BtControlType type;
} bt_control_t;

static bt_control_t bt_control = {0, 0, false, 0, 0, BtControlType::BT_NONE};


static void bt_a2dp_sink_cmd_process(char *data);
static int bt_close_a2dp_server();

static bool bt_server_is_open()
{
    return bt_control.is_bt_open;
}

static bool ble_is_open()
{
    bool ret = false;

    if (bt_control.is_ble_open) {
        ret = true;
    }

    return ret;
}

static bool bt_is_a2dp_open()
{
    bool ret = false;

    if (bt_control.is_a2dp_open) {
        ret = true;
    }

    return ret;
}

static int bt_control_cmd_send(enum BtControl bt_ctrl_cmd)
{
    char cmd[10];
    memset(cmd, 0, 10);
    sprintf(cmd, "%d", bt_ctrl_cmd);

    if (bt_control.type != BtControlType::BT_AUDIO_PLAY) {
        APP_DEBUG("Not bluetooth play mode, don`t send bluetooth control commands\n");
        return 0;
    }

    APP_DEBUG("bt_control_cmd_send, cmd: %s, len: %d\n", cmd, strlen(cmd));
	switch (bt_ctrl_cmd) {
	case (BtControl::BT_PLAY):
	case (BtControl::BT_RESUME_PLAY):
		play_avrcp();
		break;
	case (BtControl::BT_PAUSE_PLAY):
		pause_avrcp();
		break;
	case (BtControl::BT_AVRCP_STOP):
		stop_avrcp();
		break;
	case (BtControl::BT_AVRCP_BWD):
		previous_avrcp();
		break;
	case (BtControl::BT_AVRCP_FWD):
		next_avrcp();
		break;
	}

    return 0;
}

static int bt_server_open(void)
{
    if (bt_control.is_bt_open)
        return 0;

    bt_control.is_bt_open = true;

    return 0;
}

static int bt_server_close(void)
{
    if (!bt_server_is_open())
        return 0;

    bt_control.is_bt_open = false;

    return 0;
}

static int bt_a2dp_sink_server_open(void)
{
    APP_DEBUG("bt_a2dp_sink_server_open\n");

    if (bt_is_a2dp_open())
        bt_close_a2dp_server();

    system("bt_realtek_start start");
	sleep(6);
	init_avrcp_ctrl();
    bt_control.is_a2dp_open = 1;
    return 0;
}

static int ble_wifi_introducer_server_open(void)
{
    if (-1 == system("/usr/bin/bt_realtek_wificonfig start")) {
        APP_ERROR("Start bluez5 utils bt failed, errno: %d\n", errno);
        return -1;
    }

    bt_control.is_ble_open = 1;
    APP_DEBUG("ble_wifi_introducer_server_open\n");
    return 0;
}

static int ble_wifi_introducer_server_close(void)
{
    if (-1 == system("/usr/bin/bt_realtek_wificonfig stop")) {
        APP_ERROR("Stop bluez5 utils bt failed, errno: %d\n", errno);
        return -1;
    }

    bt_control.is_ble_open = 0;
    APP_DEBUG("ble_wifi_introducer_server_close\n");

    return 0;
}

static int ble_open_server()
{
    int ret = 0;

    if (bt_control.is_ble_open) {
        return ret;
    }

    bt_control.type = BtControlType::BLE_WIFI_INTRODUCER;

    if (ble_wifi_introducer_server_open() < 0) {
        goto error;
    }

    return ret;

error:
    bt_control.type = BtControlType::BT_NONE;
    return -1;
}

static int ble_close_server()
{
    int ret = 0;

    if (!bt_control.is_ble_open) {
        return ret;
    }

    if (ble_wifi_introducer_server_close() < 0) {
        ret = -1;
    }

    bt_control.type = BtControlType::BT_NONE;

    return ret;
}

static int bt_open_a2dp_server()
{
    int ret = 0;

    if (bt_control.is_a2dp_open) {
        return ret;
    }

    bt_control.type = BtControlType::BT_AUDIO_PLAY;

    if (bt_a2dp_sink_server_open() < 0) {
        goto error;
    }

    return ret;
error:
    bt_control.type = BtControlType::BT_NONE;
    return -1;
}

static int bt_close_a2dp_server()
{
    int ret = 0;

    if (!bt_control.is_a2dp_open) {
        return ret;
    }

	system("killall bluealsa");
	system("killall bluealsa-aplay");
	system("killall bluetoothctl");
	system("killall bluetoothd");
	system("killall rtk_hciattach");

    bt_control.type = BtControlType::BT_NONE;
    return ret;
}

/* Load the Bluetooth firmware and turn on the Bluetooth SRC service. */
static int bt_a2dp_src_server_open(void)
{
    APP_DEBUG("%s\n", __func__);

    if (bt_is_a2dp_open())
        bt_close_a2dp_server();

    system("bt_a2dp_source start");
    init_a2dp_master_ctrl();
    sleep(3);
    bt_control.is_a2dp_open = 1;
    return 0;
}

static int bt_open(BtControl type)
{
    int ret = 0;

    if (ble_is_open()) {
        APP_DEBUG("Close ble wifi config server.");

        if (ble_close_server() < 0) {
            ret = -1;
            return ret;
        }
    }

    if (type == BtControl::BT_SINK_OPEN) {
        APP_DEBUG("Open a2dp sink.");

        if (bt_open_a2dp_server() < 0) {
            ret = -1;
            return ret;
        }
    } else if (type == BtControl::BT_SOURCE_OPEN) {
        APP_DEBUG("Open a2dp source.");

        if (bt_a2dp_src_server_open() < 0) {
            ret = -1;
            return ret;
        }
    }

    return ret;
}

static int ble_open()
{
    int ret = 0;

    if (bt_is_a2dp_open()) {
        APP_DEBUG("Close a2dp sink.");
        bt_close_a2dp_server();
    }

    if (ble_open_server() < 0) {
        ret = -1;
        return ret;
    }

    return ret;
}

static int get_bt_mac(char *bt_mac)
{
    char ret_buff[1024] = {0};
    bool ret;

    ret = Shell::exec("hciconfig hci0 | grep Address | awk '{print $3}'",ret_buff);
    if(!ret){
        APP_ERROR("get bt address failed.\n");
        return false;
    }
    strncpy(bt_mac, ret_buff, 17);
    return 0;
}

int rk_bt_control(BtControl cmd, void *data, int len)
{
    using BtControl_rep_type = std::underlying_type<BtControl>::type;

    APP_DEBUG("controlBt, cmd: %d\n", cmd);

    int ret = 0;

    switch (cmd) {
    case BtControl::BT_IS_OPENED:
        ret = bt_is_a2dp_open();
        break;

    case BtControl::BLE_IS_OPENED:
        ret = ble_is_open();
        break;

    case BtControl::BLE_OPEN_SERVER:
        ret = bt_server_open();

        if (!ret)
            ret = ble_open();

        break;

    case BtControl::BT_SINK_OPEN:
        ret = bt_server_open();

        if (!ret)
            ret = bt_open(BtControl::BT_SINK_OPEN);

        break;

    case BtControl::BT_SOURCE_OPEN:
        ret = bt_server_open();

        if (!ret)
            ret = bt_open(BtControl::BT_SOURCE_OPEN);

        break;

    case BtControl::BT_SOURCE_SCAN:
        ret = a2dp_master_scan(data, len);
        break;

    case BtControl::BT_SOURCE_CONNECT:
        ret = a2dp_master_connect((char *)data);
        break;

    case BtControl::BT_SOURCE_DISCONNECT:
        ret = a2dp_master_disconnect((char *)data);
        break;

    case BtControl::BT_SOURCE_STATUS:
        ret = a2dp_master_status((char *)data);
        break;

    case BtControl::BT_SOURCE_REMOVE:
        ret = a2dp_master_remove((char *)data);
        break;

    case BtControl::BT_CLOSE:
        ret = bt_close_a2dp_server();

        if (!ret)
            ret = bt_server_close();

        break;
    case BtControl::BLE_CLOSE_SERVER:
        ret = ble_close_server();

        if (!ret)
            ret = bt_server_close();

        break;

    case BtControl::GET_BT_MAC:
        if (get_bt_mac((char *)data) <= 0)
            ret = -1;

        break;

    case BtControl::BT_VOLUME_UP:
        if (bt_control_cmd_send(BtControl::BT_VOLUME_UP) < 0) {
            APP_ERROR("Bt socket send volume up cmd failed\n");
            ret = -1;
        }

        break;

    case BtControl::BT_VOLUME_DOWN:
        if (bt_control_cmd_send(BtControl::BT_VOLUME_UP) < 0) {
            APP_ERROR("Bt socket send volume down cmd failed\n");
            ret = -1;
        }

        break;

	case BtControl::BT_PLAY:
    case BtControl::BT_RESUME_PLAY:
        if (bt_control_cmd_send(BtControl::BT_RESUME_PLAY) < 0) {
            APP_ERROR("Bt socket send play cmd failed\n");
            ret = -1;
        }

        break;
    case BtControl::BT_PAUSE_PLAY:
        if (bt_control_cmd_send(BtControl::BT_PAUSE_PLAY) < 0) {
            APP_ERROR("Bt socket send pause cmd failed\n");
            ret = -1;
        }

        break;

    case BtControl::BT_AVRCP_FWD:
        if (bt_control_cmd_send(BtControl::BT_AVRCP_FWD) < 0) {
            APP_ERROR("Bt socket send previous track cmd failed\n");
            ret = -1;
        }

        break;

    case BtControl::BT_AVRCP_BWD:
        if (bt_control_cmd_send(BtControl::BT_AVRCP_FWD) < 0) {
            APP_ERROR("Bt socket send next track cmd failed\n");
            ret = -1;
        }

        break;

    default:
        APP_DEBUG("%s, cmd <%d> is not implemented.\n", __func__,
                  static_cast<BtControl_rep_type>(cmd));
    }

    return ret;
}
