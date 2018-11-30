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
#include <signal.h>
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

#include "avrcpctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"

#include "DeviceIo/DeviceIo.h"
using DeviceIOFramework::ble_config;

#define msleep(x) usleep(x * 1000)

static void execute(const char cmdline[], char recv_buff[])
{
    printf("consule_run: %s %d \n", cmdline, sizeof(recv_buff));

    FILE *stream = NULL;
    char buff[1024];

    memset(recv_buff, 0, sizeof(recv_buff));

    if((stream = popen(cmdline,"r"))!=NULL){
        while(fgets(buff,1024,stream)){
            strcat(recv_buff,buff);
        }
    }
	printf("consule_run results: %s \n", recv_buff);

    pclose(stream);
}

static const bool console_run(const char *cmdline)
{
    printf("cmdline = %s\n", cmdline);
    int ret;
    ret = system(cmdline);
    if (ret < 0) {
        printf("Running cmdline failed: %s\n", cmdline);
        return false;
    }
    return true;
}

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
#define BT_IS_BLE_SINK_COEXIST 1

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
    int is_ble_open;
    int is_a2dp_sink_open;
    int is_a2dp_source_open;
	bool is_ble_sink_coexist;
    BtControlType type;
} bt_control_t;

static bt_control_t bt_control = {
	0,
	0,
	0,
	0,
	BT_IS_BLE_SINK_COEXIST,
	BtControlType::BT_NONE
};

static void bt_a2dp_sink_cmd_process(char *data);
static int bt_close_a2dp_server();
static int bt_ble_open(ble_content_t *ble_content);
#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */

static void bt_gethostname(char *hostname_buf)
{
	char hostname[HOSTNAME_MAX_LEN + 1];
	size_t buf_len;
	int i;

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

static void _bt_close_server()
{
	char ret_buff[1024];

	printf("=== _bt_close_server ===\n");
	execute("killall bluealsa", ret_buff);
	execute("killall bluealsa-aplay", ret_buff);
	execute("killall bluetoothctl", ret_buff);
	execute("killall bluetoothd", ret_buff);
	execute("killall rtk_hciattach", ret_buff);
	msleep(800);
}

static void _bt_open_server()
{
	char ret_buff[1024];
	char hostname_buf[HOSTNAME_MAX_LEN];
	char cmd_buf[66];

	printf("=== _bt_open_server ===\n");
	_bt_close_server();
	execute("echo 0 > /sys/class/rfkill/rfkill0/state && sleep 1", ret_buff);
	execute("echo 1 > /sys/class/rfkill/rfkill0/state && usleep 200000", ret_buff);

	console_run("insmod /usr/lib/modules/hci_uart.ko && sleep 1");
	memset(ret_buff, 0, 1024);
	execute("lsmod", ret_buff);
	while (!strstr(ret_buff, "hci_uart"))
		msleep(10);

	console_run("rtk_hciattach -n -s 115200 /dev/ttyS4 rtk_h5 &");
	sleep(2);
	memset(ret_buff, 0, 1024);
	execute("pidof rtk_hciattach", ret_buff);
	while (!ret_buff[0])
		msleep(10);
	
	execute("hciconfig hci0 up", ret_buff);
	console_run("/usr/libexec/bluetooth/bluetoothd -C -n -d -E &");
	sleep(2);
	memset(ret_buff, 0, 1024);
	execute("pidof bluetoothd", ret_buff);
	while (!ret_buff[0])
		msleep(10);

	execute("hciconfig hci0 up", ret_buff);
	msleep(10);
	//set Bluetooth NoInputNoOutput mode
	console_run("bluetoothctl -a NoInputNoOutput &");
	sleep(1);

	execute("hciconfig hci0 piscan", ret_buff);
	msleep(10);

	bt_gethostname(hostname_buf);
	sprintf(cmd_buf, "hciconfig hci0 name \'%s\'", hostname_buf);
	execute(cmd_buf, ret_buff);

	msleep(10);
	execute("hciconfig hci0 down", ret_buff);
	msleep(10);
	execute("hciconfig hci0 up", ret_buff);
	msleep(10);
}

static int bt_ble_open(ble_content_t *ble_content)
{
	int ret;

	ret = gatt_main(ble_content);

	return ret;
}

static void bt_start_a2dp_source()
{
	char ret_buff[1024];

	memset(ret_buff, 0, 1024);

sdp:
	msleep(500);
	execute("sdptool add A2SRC", ret_buff);
	if (!strcmp("Audio source service registered", ret_buff))
		goto sdp;

	console_run("bluealsa --profile=a2dp-source &");
	memset(ret_buff, 0, 1024);
	execute("pidof bluealsa", ret_buff);
	while (!ret_buff[0])
		msleep(10);

	console_run("bluealsa-aplay --profile-a2dp 00:00:00:00:00:00 &");
	memset(ret_buff, 0, 1024);
	execute("pidof bluealsa-aplay", ret_buff);
	while (!ret_buff[0])
		msleep(10);	
	execute("hciconfig hci0 class 0x480400", ret_buff);
	msleep(100);
}

static void bt_start_a2dp_sink()
{
	char ret_buff[1024];

sdp:
	msleep(500);
	execute("sdptool add A2SNK", ret_buff);
	if (!strcmp("Audio sink service registered", ret_buff))
		goto sdp;

	console_run("bluealsa --profile=a2dp-sink &");
	memset(ret_buff, 0, 1024);
	execute("pidof bluealsa", ret_buff);
	while (!ret_buff[0])
		msleep(10);

	console_run("bluealsa-aplay --profile-a2dp 00:00:00:00:00:00 &");
	memset(ret_buff, 0, 1024);
	execute("pidof bluealsa-aplay", ret_buff);
	while (!ret_buff[0])
		msleep(10);

	execute("hciconfig hci0 class 0x240404", ret_buff);
	msleep(200);
	printf("bt_start_a2dp_sink exit\n");
}

static int get_ps_pid(const char Name[])
{
    int len;
    char name[20] = {0};
    len = strlen(Name);
    strncpy(name,Name,len);
    name[len] ='\0';
    char cmdresult[256] = {0};
    char cmd[20] = {0};
    FILE *pFile = NULL;
    int  pid = 0;

    sprintf(cmd, "pidof %s", name);
    pFile = popen(cmd, "r");
    if (pFile != NULL)  {
        while (fgets(cmdresult, sizeof(cmdresult), pFile)) {
            pid = atoi(cmdresult);
            break;
        }
    }
    pclose(pFile);
    return pid;
}

static bool bt_sink_is_open(void)
{
	if (bt_control.is_a2dp_sink_open) {
		if (get_ps_pid("bluetoothd")) {
			APP_DEBUG("Bluetooth has been opened.\n");
			return 1;
		} else {
			APP_ERROR("Bluetooth has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}

static bool bt_source_is_open(void)
{
	if (bt_control.is_a2dp_source_open) {
		if (get_ps_pid("bluetoothd")) {
			APP_DEBUG("Bluetooth has been opened.\n");
			return 1;
		} else {
			APP_ERROR("Bluetooth has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}

static bool ble_is_open()
{
    bool ret = 0;

	if (bt_control.is_ble_open) {
		if (get_ps_pid("bluetoothd")) {
			APP_DEBUG("ble has been opened.\n");
			return true;
		} else {
			APP_ERROR("ble has been opened but bluetoothd server exit.\n");
		}
	}
	APP_ERROR("ble not open.\n");
	return ret;
}

static int bt_control_cmd_send(enum BtControl bt_ctrl_cmd)
{
    char cmd[10];
    memset(cmd, 0, 10);
    sprintf(cmd, "%d", bt_ctrl_cmd);

    if (bt_control.type != BtControlType::BT_SINK) {
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

static int ble_close_server(void)
{
	int ret = 0;

	APP_DEBUG("ble server close\n");

	if (!ble_is_open())
		return 0;

	if (bt_control.is_ble_sink_coexist) {
		ble_disable_adv();
		return 0;
	}

	release_ble_gatt();
	usleep(666666);
	_bt_close_server();

	bt_control.type = BtControlType::BT_NONE;
	bt_control.is_ble_open = 0;

	return ret;
}

static int bt_close_sink(void)
{
	int ret = 0;

	APP_DEBUG("ble server close\n");

	if (!bt_sink_is_open())
		return 0;

	if ((bt_control.is_ble_sink_coexist) && ble_is_open())
		return 0;

	release_avrcp_ctrl();
	_bt_close_server();

	bt_control.type = BtControlType::BT_NONE;
	bt_control.is_a2dp_sink_open = 0;

	return ret;
}

static int bt_close_source(void)
{
	int ret = 0;

	APP_DEBUG("ble_close_source close\n");

	if (!bt_source_is_open())
		return 0;

	release_a2dp_master_ctrl();
	_bt_close_server();
	bt_control.type = BtControlType::BT_NONE;
	bt_control.is_a2dp_source_open = 0;

	return ret;
}


static int bt_a2dp_sink_open(void)
{
	int ret = 0;

	APP_DEBUG("bt_a2dp_sink_server_open\n");

	bt_start_a2dp_sink();
	printf("call init_avrcp_ctrl ...\n");
	ret = init_avrcp_ctrl();

	return ret;
}

/* Load the Bluetooth firmware and turn on the Bluetooth SRC service. */
static int bt_a2dp_src_server_open(void)
{
	APP_DEBUG("%s\n", __func__);

	bt_start_a2dp_source();
	msleep(800);
	init_a2dp_master_ctrl();
	bt_control.is_a2dp_source_open = 1;
	return 0;
}

static int bt_interface(BtControl type, void *data)
{
    int ret = 0;

	if (type == BtControl::BT_SINK_OPEN) {
		APP_DEBUG("Open a2dp sink.");

		if (bt_a2dp_sink_open() < 0) {
			ret = -1;
			return ret;
		}
	} else if (type == BtControl::BT_SOURCE_OPEN) {
		APP_DEBUG("Open a2dp source.");

		if (bt_a2dp_src_server_open() < 0) {
			ret = -1;
			return ret;
		}
	} else if (type == BtControl::BT_BLE_OPEN) {
		APP_DEBUG("Open ble.");

		if (bt_ble_open(data) < 0) {
			ret = -1;
			return ret;
		}
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
	case BtControl::BT_SINK_OPEN:
		if (bt_source_is_open()) {
			APP_ERROR("bt sink isn't coexist with source!!!\n");
			bt_close_source();
		}

		printf("=== BtControl::BT_SINK_OPEN ===\n");
		/* setup 1: is sink open */
		if (bt_sink_is_open())
			return 0;

		printf("=== bt sink coexist: %d ===\n", bt_control.is_ble_sink_coexist);
		/* setup 3: is coexist */
		if (bt_control.is_ble_sink_coexist) {
			if (!ble_is_open()) {
				_bt_open_server();
			}
		} else {
			if (ble_is_open()) {
				APP_DEBUG("Close ble wifi config server.\n");
				if (ble_close_server() < 0)
					return -1;
			}
		}

		if (bt_interface(BtControl::BT_SINK_OPEN, NULL) < 0) {
			bt_control.is_a2dp_sink_open = 0;
			bt_control.type = BtControlType::BT_NONE;
		}

		bt_control.is_a2dp_sink_open = 1;
		bt_control.type = BtControlType::BT_SINK;

		break;

    case BtControl::BT_BLE_OPEN:
		if (bt_source_is_open()) {
			bt_close_source();
		}

		/* setup 3: is coexist */
		if (bt_control.is_ble_sink_coexist) {
			if (bt_sink_is_open() && ble_is_open()) {
				ble_enable_adv();
				return 0;
			} if (!bt_sink_is_open() && !ble_is_open()) {
				_bt_open_server();
			} if (bt_sink_is_open() && !ble_is_open()) {
				//NULL
			} if (!bt_sink_is_open() && ble_is_open()) {
				ble_enable_adv();
				return 0;
			}
		} else {
			if (bt_sink_is_open()) {
				APP_DEBUG("Close bt sink server.\n");
				if (bt_close_sink() < 0)
					return -1;
			}
		}

		if (bt_interface(BtControl::BT_BLE_OPEN, data) < 0) {
			bt_control.is_ble_open = 0;
			bt_control.type = BtControlType::BT_NONE;
		}

		bt_control.is_ble_open = true;
		bt_control.type = BtControlType::BT_BLE_MODE;

		break;

    case BtControl::BT_SOURCE_OPEN:
		printf("=== BtControl::BT_SINK_OPEN ===\n");
		/* setup 1: is source open */
		if (bt_source_is_open())
			return 0;

		bt_control.is_a2dp_source_open = 0;
		bt_control.is_ble_open = 0;
		bt_control.type = BtControlType::BT_NONE;

		if (bt_sink_is_open()) {
			bt_control.is_a2dp_sink_open = 0;
			release_avrcp_ctrl();
		}
		if (ble_is_open()) {
			bt_control.is_ble_open = 0;			
			release_ble_gatt();
		}

		_bt_close_server();
		_bt_open_server();

		if (bt_interface(BtControl::BT_SOURCE_OPEN, NULL) < 0) {
			bt_control.is_a2dp_source_open = 0;
			bt_control.type = BtControlType::BT_NONE;
		}

		bt_control.is_a2dp_source_open = true;
		bt_control.type = BtControlType::BT_SOURCE;

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

	case BtControl::BT_SINK_CLOSE:
		bt_close_sink();
		break;

	case BtControl::BT_SOURCE_CLOSE:
		bt_close_source();
		break;

	case BtControl::BT_BLE_COLSE:
		ble_close_server();
		break;

	case BtControl::BT_SINK_IS_OPENED:
		ret = bt_sink_is_open();
		break;

	case BtControl::BT_SOURCE_IS_OPENED:
		ret = bt_source_is_open();
		break;

	case BtControl::BT_BLE_IS_OPENED:
		ret = ble_is_open();
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

	case BtControl::BT_BLE_WRITE:
		struct ble_config *ble_cfg = data;
		gatt_write_data(ble_cfg->uuid, ble_cfg->data, ble_cfg->len);

		break;

	case BtControl::BT_SINK_POWER:
		a2dp_sink_cmd_power(1);

		break;

    default:
        APP_DEBUG("%s, cmd <%d> is not implemented.\n", __func__,
                  static_cast<BtControl_rep_type>(cmd));
    }

    return ret;
}
