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

#include "bluez_ctrl.h"
#include <DeviceIo/RkBle.h>

#define BT_IS_BLE_SINK_COEXIST 1

volatile bt_control_t bt_control = {
	0,
	0,
	0,
	0,
	0,
	BT_IS_BLE_SINK_COEXIST,
	BtControlType::BT_NONE
};

Bt_Content_t GBt_Content;
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

static int bt_close_a2dp_server();
static int bt_ble_open(void);
#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */

static void bt_gethostname(char *hostname_buf)
{
	char hostname[HOSTNAME_MAX_LEN + 1];
	size_t buf_len;

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
	Shell::exec("killall bluealsa", ret_buff, 1024);
	Shell::exec("killall bluealsa-aplay", ret_buff, 1024);
	Shell::exec("killall bluetoothctl", ret_buff, 1024);
	Shell::exec("killall bluetoothd", ret_buff, 1024);

kill:
	Shell::exec("killall bluetoothd", ret_buff, 1024);
	msleep(100);
	Shell::exec("pidof bluetoothd", ret_buff, 1024);
	while (ret_buff[0]) {
		msleep(10);
		goto kill;
	}

	Shell::exec("killall rtk_hciattach", ret_buff, 1024);
	msleep(800);
	Shell::exec("pidof rtk_hciattach", ret_buff, 1024);
	while (ret_buff[0]) {
		msleep(10);
		goto kill;
	}
}

static void _bt_open_server(const char *bt_name)
{
	char hostname_buf[HOSTNAME_MAX_LEN];
	char cmd_buf[64 + HOSTNAME_MAX_LEN]; /* 64 for "hciconfig hci0 name" */
	char ret_buff[1024];

	Shell::exec("pidof bluetoothd", ret_buff, 1024);
	if (ret_buff[0])
		return;

	printf("[BT_OPEN] _bt_open_server \n");
	_bt_close_server();

	Shell::exec("echo 0 > /sys/class/rfkill/rfkill0/state && sleep 2", ret_buff, 1024);
	Shell::exec("echo 1 > /sys/class/rfkill/rfkill0/state && usleep 200000", ret_buff, 1024);

	console_run("insmod /usr/lib/modules/hci_uart.ko && usleep 300000");
	Shell::exec("lsmod", ret_buff, 1024);
	while (!strstr(ret_buff, "hci_uart"))
		msleep(10);

	console_run("rtk_hciattach -n -s 115200 /dev/ttyS4 rtk_h5 &");
	sleep(2);
	Shell::exec("pidof rtk_hciattach", ret_buff, 1024);
	while (!ret_buff[0])
		msleep(10);

	//system("hcidump -i hci0 -w /tmp/h.log &");
	//sleep(1);

	Shell::exec("hciconfig hci0 up", ret_buff, 1024);
	console_run("/usr/libexec/bluetooth/bluetoothd -C -n -d -E &");
	sleep(2);
	Shell::exec("pidof bluetoothd", ret_buff, 1024);
	while (!ret_buff[0])
		msleep(10);

	Shell::exec("hciconfig hci0 up", ret_buff, 1024);
	msleep(10);
	//set Bluetooth NoInputNoOutput mode
	console_run("bluetoothctl -a NoInputNoOutput &");
	sleep(1);

	Shell::exec("hciconfig hci0 piscan", ret_buff, 1024);
	msleep(10);

	/* Use a user-specified name or a device default name? */
	if (bt_name) {
		printf("[BT_OPEN]: bt_name: %s\n", bt_name);
		sprintf(cmd_buf, "hciconfig hci0 name \'%s\'", bt_name);
	} else {
		bt_gethostname(hostname_buf);
		sprintf(cmd_buf, "hciconfig hci0 name \'%s\'", hostname_buf);
	}
	Shell::exec(cmd_buf, ret_buff, 1024);

	msleep(10);
	Shell::exec("hciconfig hci0 down", ret_buff, 1024);
	msleep(10);
	Shell::exec("hciconfig hci0 up", ret_buff, 1024);
	Shell::exec("hciconfig hci0 up", ret_buff, 1024);
	msleep(200);
}

static int bt_ble_open(void)
{
	int ret;

	gatt_open();

	return ret;
}

static void bt_start_a2dp_source()
{
	char ret_buff[1024];

	console_run("killall bluealsa");
	console_run("killall bluealsa-aplay");

	msleep(500);
	console_run("bluealsa --profile=a2dp-source &");
	Shell::exec("pidof bluealsa", ret_buff, 1024);
	while (!ret_buff[0])
		msleep(10);

	Shell::exec("hciconfig hci0 class 0x480400", ret_buff, 1024);
	msleep(100);
	Shell::exec("hciconfig hci0 class 0x480400", ret_buff, 1024);
	msleep(100);
}

static void bt_start_a2dp_sink()
{
	char ret_buff[1024];

	console_run("killall bluealsa");
	console_run("killall bluealsa-aplay");

	msleep(500);
	console_run("bluealsa --profile=a2dp-sink &");
	Shell::exec("pidof bluealsa", ret_buff, 1024);
	while (!ret_buff[0])
		msleep(10);

	console_run("bluealsa-aplay --profile-a2dp 00:00:00:00:00:00 &");
	Shell::exec("pidof bluealsa-aplay", ret_buff, 1024);
	while (!ret_buff[0])
		msleep(10);

	Shell::exec("hciconfig hci0 class 0x240404", ret_buff, 1024);
	msleep(100);
	Shell::exec("hciconfig hci0 class 0x240404", ret_buff, 1024);
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

bool bt_sink_is_open(void)
{
	if (bt_control.is_a2dp_sink_open) {
		APP_DEBUG("bt_sink has been opened.\n");
		if (get_ps_pid("bluetoothd") && get_ps_pid("bluealsa") && get_ps_pid("bluealsa-aplay")) {
			APP_DEBUG("Bluetooth has been opened.\n");
			return 1;
		} else {
			APP_ERROR("bt_sink has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}

bool bt_source_is_open(void)
{
	if (bt_control.is_a2dp_source_open) {
		APP_DEBUG("bt_source has been opened.\n");
		if (get_ps_pid("bluetoothd") && get_ps_pid("bluealsa")) {
			APP_DEBUG("Bluetooth has been opened.\n");
			return 1;
		} else {
			APP_ERROR("bt_source has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}

bool ble_is_open()
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

int bt_control_cmd_send(enum BtControl bt_ctrl_cmd)
{
	char cmd[10];
	memset(cmd, 0, 10);
	sprintf(cmd, "%d", bt_ctrl_cmd);

	//if (bt_control.type != BtControlType::BT_SINK) {
	if (!bt_control.is_a2dp_sink_open) {
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
	case (BtControl::BT_AVRCP_STOP):
		stop_avrcp();
		break;
	case (BtControl::BT_AVRCP_BWD):
		previous_avrcp();
		break;
	case (BtControl::BT_AVRCP_FWD):
		next_avrcp();
		break;
	default:
		break;
	}

	return 0;
}

static int ble_close_server(void)
{
	int ret = 0;

	if (!ble_is_open())
		return 1;

	APP_DEBUG("ble server close\n");

	ble_disable_adv();
	gatt_close();

	if (bt_control.last_type == BtControlType::BT_SINK)
		bt_control.type = BtControlType::BT_SINK;
	else
		bt_control.type = BtControlType::BT_NONE;

	bt_control.is_ble_open = 0;

	return ret;
}

int bt_close_sink(void)
{
	int ret = 0;

	if (!bt_sink_is_open())
		return 1;

	APP_DEBUG("bt_close_sink\n");

	release_avrcp_ctrl();

	bt_control.type = BtControlType::BT_NONE;
	bt_control.is_a2dp_sink_open = 0;

	return ret;
}

int bt_close_source(void)
{
	int ret = 0;

	if (!bt_source_is_open())
		return 1;

	APP_DEBUG("bt_close_source close\n");

	if (a2dp_master_disconnect(NULL))
		sleep(3);
	release_a2dp_master_ctrl();

	bt_control.type = BtControlType::BT_NONE;
	bt_control.is_a2dp_source_open = 0;

	return ret;
}

static int bt_a2dp_sink_open(void)
{
	int ret = 0;

	APP_DEBUG("bt_a2dp_sink_server_open\n");

	if ((bt_control.last_type == BtControlType::BT_SOURCE) ||
		(bt_control.last_type == BtControlType::BT_NONE))
		bt_start_a2dp_sink();

	printf("call init_avrcp_ctrl ...\n");
	ret = a2dp_sink_open();

	return ret;
}

/* Load the Bluetooth firmware and turn on the Bluetooth SRC service. */
static int bt_a2dp_src_server_open(void)
{
	APP_DEBUG("%s\n", __func__);

	if ((bt_control.last_type == BtControlType::BT_SINK) ||
		(bt_control.last_type == BtControlType::BT_NONE))
		bt_start_a2dp_source();

	msleep(500);

	init_a2dp_master_ctrl();
	bt_control.is_a2dp_source_open = 1;

	return 0;
}

int bt_interface(BtControl type, void *data)
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

		if (bt_ble_open() < 0) {
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

	ret = Shell::exec("hciconfig hci0 | grep Address | awk '{print $3}'",ret_buff, 1024);
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
	int ret = 0;
	rk_ble_config *ble_cfg;
	bool scan;

	APP_DEBUG("controlBt, cmd: %d\n", cmd);

	switch (cmd) {
	case BtControl::BT_OPEN:
		GBt_Content = *((Bt_Content_t *)data);
		_bt_close_server();
		_bt_open_server(GBt_Content.bt_name);

		//FOR HISENSE
		//bt_adv_set((Bt_Content_t *)data);
		bt_open(&GBt_Content);

		bt_control.is_bt_open = 1;
		bt_control.type = BtControlType::BT_NONE;
		bt_control.last_type = BtControlType::BT_NONE;
		break;
	case BtControl::BT_SINK_OPEN:
		if (!bt_control.is_bt_open)
			return -1;

		bt_control.type = BtControlType::BT_SINK;
		if (bt_sink_is_open())
			return 1;

		if (bt_source_is_open()) {
			APP_ERROR("bt sink isn't coexist with source!!!\n");
			bt_close_source();
		}

		if (bt_interface(BtControl::BT_SINK_OPEN, NULL) < 0) {
			bt_control.is_a2dp_sink_open = 0;
			bt_control.type = BtControlType::BT_NONE;
		}

		bt_control.is_a2dp_sink_open = 1;
		bt_control.type = BtControlType::BT_SINK;
		bt_control.last_type = BtControlType::BT_SINK;
		break;

	case BtControl::BT_BLE_OPEN:
		if (!bt_control.is_bt_open)
			return -1;


		bt_control.type = BtControlType::BT_BLE_MODE;

		if (bt_interface(BtControl::BT_BLE_OPEN, data) < 0) {
			bt_control.is_ble_open = 0;
			bt_control.type = BtControlType::BT_NONE;
			return -1;
		}

		bt_control.is_ble_open = true;
		bt_control.type = BtControlType::BT_BLE_MODE;
		printf("=== BtControl::BT_BLE_OPEN ok ===\n");
		break;

	case BtControl::BT_SOURCE_OPEN:
		if (!bt_control.is_bt_open)
			return -1;

		printf("=== BtControl::BT_SOURCE_OPEN ===\n");
		bt_control.type = BtControlType::BT_SOURCE;

		if (bt_source_is_open()) {
			return 1;
		}

		if (bt_sink_is_open()) {
			APP_ERROR("bt sink isn't coexist with source!!!\n");
			bt_close_sink();
		}

		sleep(1);

		if (bt_interface(BtControl::BT_SOURCE_OPEN, NULL) < 0) {
			bt_control.is_a2dp_source_open = 0;
			bt_control.type = BtControlType::BT_NONE;
			return -1;
		}

		bt_control.is_a2dp_source_open = true;
		bt_control.type = BtControlType::BT_SOURCE;
		bt_control.last_type = BtControlType::BT_SOURCE;

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
		ret = a2dp_master_status((char *)data, NULL);
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
		if (bt_control_cmd_send(BtControl::BT_AVRCP_BWD) < 0) {
			APP_ERROR("Bt socket send next track cmd failed\n");
			ret = -1;
		}

		break;

	case BtControl::BT_BLE_WRITE:
		ble_cfg = (rk_ble_config *)data;
		ret = gatt_write_data(ble_cfg->uuid, ble_cfg->data, ble_cfg->len);

		break;
	case BtControl::BT_VISIBILITY:
		scan = (*(bool *)data);
		rkbt_inquiry_scan(scan);
		break;
	case BtControl::BT_GATT_MTU:
		ret = gatt_mtu();
		break;
	default:
		APP_DEBUG("%s, cmd <%d> is not implemented.\n", __func__,
				  static_cast<BtControl_rep_type>(cmd));
		break;
	}

	return ret;
}
