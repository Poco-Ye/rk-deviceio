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
#include <DeviceIo/RK_log.h>
#include <DeviceIo/Rk_shell.h>
#include "../utility/utility.h"

#define BT_IS_BLE_SINK_COEXIST 1

volatile bt_control_t bt_control = {
	0,
	0,
	0,
	0,
	0,
	0,
	BT_IS_BLE_SINK_COEXIST,
	BtControlType::BT_NONE,
	BtControlType::BT_NONE
};

RkBtContent GBt_Content;

static int bt_close_a2dp_server();
static int bt_ble_open(void);

int bt_gethostname(char *hostname_buf, const size_t size)
{
	char hostname[HOSTNAME_MAX_LEN + 1];
	size_t buf_len = sizeof(hostname) - 1;

	memset(hostname_buf, 0, size);
	memset(hostname, 0, sizeof(hostname));

	if (gethostname(hostname, buf_len) != 0) {
		RK_LOGE("bt_gethostname gethostname error !!!!!!!!\n");
		return -1;
	}

	/* Deny sending of these local hostnames */
	if (hostname[0] == '\0' || hostname[0] == '.' || strcmp(hostname, "(none)") == 0) {
		RK_LOGE("bt_gethostname gethostname format error !!!\n");
		return -2;
	}

	strncpy(hostname_buf, hostname, strlen(hostname) > (size - 1) ? (size - 1) : strlen(hostname));
	return 0;
}

static int _bt_close_server(void)
{
	RK_LOGD("=== _bt_close_server ===\n");

	if (bt_kill_task("bluealsa") < 0)
		return -1;

	if (bt_kill_task("bluealsa-aplay") < 0)
		return -1;

	if (bt_kill_task("bluetoothctl") < 0)
		return -1;

	if (bt_kill_task("bluetoothd") < 0)
		return -1;

	bt_exec_command_system("hciconfig hci0 down");
	if (bt_kill_task("rtk_hciattach") < 0)
		return -1;

	return 0;
}

static int _bt_open_server(const char *bt_name)
{
	char ret_buff[1024];
	char bt_buff[1024];

	if (bt_get_ps_pid("bluetoothd")) {
		RK_LOGD("_bt_open_server bt has already opened\n");
		return 0;
	}

	RK_LOGD("[BT_OPEN] _bt_open_server \n");

	bt_exec_command_system("echo 0 > /sys/class/rfkill/rfkill0/state && usleep 10000");
	bt_exec_command_system("echo 1 > /sys/class/rfkill/rfkill0/state && usleep 10000");

	/* check bt vendor (exteran/rkwifibt) */
	if (access("/usr/bin/bt_init.sh", F_OK)) {
		RK_LOGE("[BT_OPEN]  bt_init.sh not exist !!!\n");
		if (access("/userdata/bt_pcba_test", F_OK)) {
			RK_LOGE("[BT_OPEN] userdata bt_pcba_test not exist !!!\n");
			return -1;
		}
	}

	/* realtek init */
	bt_exec_command("cat /usr/bin/bt_init.sh | grep rtk_hciattach", bt_buff, 1024);
	if (bt_buff[0]) {
		bt_exec_command_system("insmod /usr/lib/modules/hci_uart.ko && usleep 50000");
		bt_exec_command("lsmod", ret_buff, 1024);
		if (!strstr(ret_buff, "hci_uart")) {
			RK_LOGE("open bt server: insmod hci_uart.ko failed!\n");
			return -1;
		}

		RK_LOGE("bt_buff: %s \n", bt_buff);
		bt_exec_command_system(bt_buff);

		sleep(1);
		if (!bt_get_ps_pid("rtk_hciattach")) {
			RK_LOGE("open bt server error: rtk_hciattach failed!\n");
			return -1;
		}
	}

	/* broadcom or cypress init */
	bt_exec_command("cat /usr/bin/bt_init.sh | grep brcm_patchram_plus1", bt_buff, 1024);
	if (bt_buff[0]) {
		RK_LOGE("bt_buff: %s \n", bt_buff);
		bt_exec_command_system(bt_buff);
		sleep(1);
		bt_exec_command("pidof brcm_patchram_plus1", ret_buff, 1024);
		if (!ret_buff[0]) {
			RK_LOGE("open bt server failed! error: brcm_patchram_plus1 failed!\n");
			return -1;
		}
	}

	bt_exec_command_system("hciconfig hci0 up");
	msleep(10);

	/* run bluetoothd */
	if (bt_run_task("bluetoothd", "/usr/libexec/bluetooth/bluetoothd -C -n -d -E &")) {
		RK_LOGE("open bt server failed! error: bluetoothd failed!\n");
		return -1;
	}
	msleep(100);

	bt_exec_command_system("hciconfig hci0 up");
	msleep(10);

	//set Bluetooth NoInputNoOutput mode
	bt_exec_command_system("bluetoothctl -a NoInputNoOutput &");
	msleep(10);

	bt_exec_command("hciconfig hci0 pageparms 18:1024", ret_buff, 1024);
	msleep(50);
	bt_exec_command("hciconfig hci0 inqparms 18:2048", ret_buff, 1024);
	msleep(50);
	bt_exec_command("hcitool cmd 0x03 0x47 0x01", ret_buff, 1024);
	msleep(50);
	bt_exec_command("hcitool cmd 0x03 0x43 0x01", ret_buff, 1024);
	msleep(50);

	RK_LOGE("_bt_open_server end\n");
	return 0;
}

static int bt_ble_open(void)
{
	int ret;

	gatt_open();
	RK_LOGD("%s: ret: 0x%x\n", __func__, ret);

	return 1;
}

static int bt_start_a2dp_source()
{
	char ret_buff[1024];

	bt_exec_command_system("killall bluealsa");
	bt_exec_command_system("killall bluealsa-aplay");

	msleep(500);
	bt_exec_command_system("bluealsa --profile=a2dp-source &");
	bt_exec_command("pidof bluealsa", ret_buff, 1024);
	if (!ret_buff[0]) {
		RK_LOGE("start a2dp source profile failed!\n");
		return -1;
	}

	bt_exec_command_system("hciconfig hci0 class 0x480400");
	msleep(100);
	bt_exec_command_system("hciconfig hci0 class 0x480400");
	msleep(100);

	return 0;
}

static int bt_start_a2dp_sink(int sink_only)
{
	char ret_buff[1024];

	bt_kill_task("bluealsa");
	bt_kill_task("bluealsa-aplay");

	msleep(500);
	if (sink_only)
		bt_exec_command_system("bluealsa --profile=a2dp-sink --a2dp-volume &");
	else
		bt_exec_command_system("bluealsa --a2dp-volume &");
	bt_exec_command("pidof bluealsa", ret_buff, 1024);
	if (!ret_buff[0]) {
		RK_LOGE("start a2dp sink profile failed!\n");
		return -1;
	}

	bt_exec_command_system("bluealsa-aplay --profile-a2dp 00:00:00:00:00:00 &");
	bt_exec_command("pidof bluealsa-aplay", ret_buff, 1024);
	if (!ret_buff[0]) {
		RK_LOGE("start a2dp sink play server failed!\n");
		return -1;
	}

	bt_exec_command_system("hciconfig hci0 class 0x240404");
	msleep(100);
	bt_exec_command_system("hciconfig hci0 class 0x240404");
	msleep(200);
	RK_LOGD("bt_start_a2dp_sink exit\n");

	return 0;
}

static int bt_start_hfp()
{
	char ret_buff[1024];

	bt_exec_command_system("killall bluealsa");
	bt_exec_command_system("killall bluealsa-aplay");

	msleep(500);
	bt_exec_command_system("bluealsa --profile=hfp-hf &");
	bt_exec_command("pidof bluealsa", ret_buff, 1024);
	if (!ret_buff[0]) {
		RK_LOGE("start hfp-hf profile failed!\n");
		return -1;
	}

	bt_exec_command_system("hciconfig hci0 class 0x240404");
	msleep(100);
	bt_exec_command_system("hciconfig hci0 class 0x240404");
	msleep(200);
	RK_LOGD("%s exit\n", __func__);

	return 0;
}


static int get_ps_pid(const char Name[])
{
	int len;
	char name[32] = {0};
	len = strlen(Name);
	strncpy(name,Name,len);
	name[31] ='\0';
	char cmdresult[256] = {0};
	char cmd[64] = {0};
	FILE *pFile = NULL;
	int  pid = 0;

	sprintf(cmd, "pidof %s", name);
	pFile = popen(cmd, "r");
	if (pFile != NULL)  {
		while (fgets(cmdresult, sizeof(cmdresult), pFile)) {
			pid = atoi(cmdresult);
			break;
		}
		pclose(pFile);
	}
	return pid;
}

bool bt_sink_is_open(void)
{
	if (bt_control.is_a2dp_sink_open) {
		if (get_ps_pid("bluetoothd") && get_ps_pid("bluealsa") && get_ps_pid("bluealsa-aplay")) {
			return 1;
		} else {
			RK_LOGE("bt_sink has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}

bool bt_hfp_is_open(void)
{
	if (bt_control.is_hfp_open) {
		RK_LOGD("bt hfp has been opened.\n");
		if (get_ps_pid("bluetoothd") && get_ps_pid("bluealsa")) {
			RK_LOGD("Bluetooth has been opened.\n");
			return 1;
		} else {
			RK_LOGE("bt hfp has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}


bool bt_source_is_open(void)
{
	if (bt_control.is_a2dp_source_open) {
		RK_LOGD("bt_source has been opened.\n");
		if (get_ps_pid("bluetoothd") && get_ps_pid("bluealsa")) {
			RK_LOGD("Bluetooth has been opened.\n");
			return 1;
		} else {
			RK_LOGE("bt_source has been opened but bluetoothd server exit.\n");
		}
	}

	return 0;
}

bool ble_is_open()
{
	bool ret = 0;

	if (bt_control.is_ble_open) {
		if (get_ps_pid("bluetoothd")) {
			RK_LOGD("ble has been opened.\n");
			return true;
		} else {
			RK_LOGE("ble has been opened but bluetoothd server exit.\n");
		}
	}
	RK_LOGE("ble not open.\n");
	return ret;
}

int bt_control_cmd_send(enum BtControl bt_ctrl_cmd)
{
	char cmd[10];
	memset(cmd, 0, 10);
	sprintf(cmd, "%d", bt_ctrl_cmd);

	//if (bt_control.type != BtControlType::BT_SINK) {
	if (!bt_control.is_a2dp_sink_open) {
		RK_LOGD("Not bluetooth play mode, don`t send bluetooth control commands\n");
		return 0;
	}

	RK_LOGD("bt_control_cmd_send, cmd: %s, len: %d\n", cmd, strlen(cmd));
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

	RK_LOGD("ble server close\n");

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

	if (!bt_control.is_a2dp_sink_open)
		return 1;

	RK_LOGD("bt_close_sink\n");

	if (bt_hfp_is_open()) {
		release_avrcp_ctrl2();
		bt_control.type = BtControlType::BT_HFP_HF;
		system("killall bluealsa-aplay");
	} else {
		system("killall bluealsa-aplay");

		if(!disconnect_current_devices())
			sleep(3);

		release_avrcp_ctrl();
		system("killall bluealsa");
		bt_control.type = BtControlType::BT_NONE;
	}

	bt_control.is_a2dp_sink_open = 0;

	return ret;
}

int bt_close_source(void)
{
	int ret = 0;

	a2dp_master_avrcp_close();
	if (!bt_source_is_open())
		return 1;

	RK_LOGD("bt_close_source close\n");

	if (!a2dp_master_disconnect(NULL))
		sleep(3);
	release_a2dp_master_ctrl();

	bt_control.type = BtControlType::BT_NONE;
	bt_control.is_a2dp_source_open = 0;

	return ret;
}

static int bt_a2dp_sink_open(void)
{
	int ret = 0;

	RK_LOGD("bt_a2dp_sink_server_open\n");

	ret = bt_start_a2dp_sink(1);
	if (ret == 0) {
		RK_LOGD("call init_avrcp_ctrl ...\n");
		ret = a2dp_sink_open();
	}

	return ret;
}

static int bt_hfp_hf_open(void)
{
	int ret = 0;

	RK_LOGD("%s is called!\n", __func__);
	ret = bt_start_hfp();
	if (ret == 0)
		system("hciconfig hci0 piscan");

	return ret;
}

static int bt_hfp_with_sink_open(void)
{
	int ret = 0;

	RK_LOGD("%s is called!\n", __func__);
	ret = bt_start_a2dp_sink(0);
	if (ret == 0)
		ret = a2dp_sink_open();

	return ret;
}

/* Load the Bluetooth firmware and turn on the Bluetooth SRC service. */
static int bt_a2dp_src_server_open(void)
{
	RK_LOGD("%s\n", __func__);

	if ((bt_control.last_type == BtControlType::BT_SINK) ||
		(bt_control.last_type == BtControlType::BT_HFP_HF) ||
		(bt_control.last_type == BtControlType::BT_SINK_HFP_MODE) ||
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
		RK_LOGD("Open a2dp sink.");

		if (bt_a2dp_sink_open() < 0) {
			ret = -1;
			return ret;
		}
	} else if (type == BtControl::BT_SOURCE_OPEN) {
		RK_LOGD("Open a2dp source.");

		if (bt_a2dp_src_server_open() < 0) {
			ret = -1;
			return ret;
		}
	} else if (type == BtControl::BT_BLE_OPEN) {
		RK_LOGD("Open ble.");

		if (bt_ble_open() < 0) {
			ret = -1;
			return ret;
		}
	} else if (type == BtControl::BT_HFP_OPEN) {
		RK_LOGD("Open bt hfp.");
		bt_hfp_hf_open();
	} else if (type == BtControl::BT_HFP_SINK_OPEN) {
		RK_LOGD("Open bt hfp with sink.");
		bt_hfp_with_sink_open();
	}

	return ret;
}

static int get_bt_mac(char *bt_mac)
{
	char ret_buff[1024] = {0};
	bool ret;

	bt_exec_command("hciconfig hci0 | grep Address | awk '{print $3}'", ret_buff, 1024);
	if (!ret_buff[0]) {
		RK_LOGE("get bt address failed.\n");
		return false;
	}
	strncpy(bt_mac, ret_buff, 17);
	return 0;
}

int rk_bt_control(BtControl cmd, void *data, int len)
{
	using BtControl_rep_type = std::underlying_type<BtControl>::type;
	int ret = 0;
	RkBleConfig *ble_cfg;
	bool scan;

	RK_LOGD("controlBt, cmd: %d\n", cmd);

	switch (cmd) {
	case BtControl::BT_OPEN:
		if (_bt_close_server() < 0) {
			RK_LOGD("_bt_close_server failed\n");
			return -1;
		}

		if(data) {
			GBt_Content = *((RkBtContent *)data);

			if (_bt_open_server(GBt_Content.bt_name) < 0) {
				RK_LOGD("_bt_open_server failed\n");
				return -1;
			}

			if (bt_open(&GBt_Content) < 0) {
				RK_LOGD("bt_open failed\n");
				return -1;
			}
		} else {
			if (_bt_open_server(NULL) < 0) {
				RK_LOGD("_bt_open_server failed\n");
				return -1;
			}

			if (bt_open(NULL) < 0) {
				RK_LOGD("bt_open failed\n");
				return -1;
			}
		}

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
			RK_LOGE("bt sink isn't coexist with source!!!\n");
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
		RK_LOGD("=== BtControl::BT_BLE_OPEN ok ===\n");
		break;

	case BtControl::BT_SOURCE_OPEN:
		if (!bt_control.is_bt_open)
			return -1;

		RK_LOGD("=== BtControl::BT_SOURCE_OPEN ===\n");
		bt_control.type = BtControlType::BT_SOURCE;

		if (bt_source_is_open()) {
			return 1;
		}

		if (bt_sink_is_open()) {
			RK_LOGE("bt sink isn't coexist with source!!!\n");
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
		ret = a2dp_master_status(NULL, 0, NULL, 0);
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
		ble_disconnect();
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
			RK_LOGE("Bt socket send volume up cmd failed\n");
			ret = -1;
		}

		break;

	case BtControl::BT_VOLUME_DOWN:
		if (bt_control_cmd_send(BtControl::BT_VOLUME_UP) < 0) {
			RK_LOGE("Bt socket send volume down cmd failed\n");
			ret = -1;
		}

		break;

	case BtControl::BT_PLAY:
	case BtControl::BT_RESUME_PLAY:
		if (bt_control_cmd_send(BtControl::BT_RESUME_PLAY) < 0) {
			RK_LOGE("Bt socket send play cmd failed\n");
			ret = -1;
		}

		break;
	case BtControl::BT_PAUSE_PLAY:
		if (bt_control_cmd_send(BtControl::BT_PAUSE_PLAY) < 0) {
			RK_LOGE("Bt socket send pause cmd failed\n");
			ret = -1;
		}

		break;

	case BtControl::BT_AVRCP_FWD:
		if (bt_control_cmd_send(BtControl::BT_AVRCP_FWD) < 0) {
			RK_LOGE("Bt socket send previous track cmd failed\n");
			ret = -1;
		}

		break;

	case BtControl::BT_AVRCP_BWD:
		if (bt_control_cmd_send(BtControl::BT_AVRCP_BWD) < 0) {
			RK_LOGE("Bt socket send next track cmd failed\n");
			ret = -1;
		}

		break;

	case BtControl::BT_BLE_WRITE:
		ble_cfg = (RkBleConfig *)data;
		ret = gatt_write_data(ble_cfg->uuid, ble_cfg->data, ble_cfg->len);

		break;
	case BtControl::BT_VISIBILITY:
		scan = (*(bool *)data);
		rkbt_inquiry_scan(scan);
		break;
	case BtControl::BT_GATT_MTU:
		ret = gatt_mtu();
		break;
	case BtControl::BT_BLE_DISCONNECT:
		ret = ble_disconnect();
		break;
	case BtControl::BT_BLE_SETUP:
		ret = gatt_setup();
		break;
	case BtControl::BT_BLE_CLEAN:
		gatt_cleanup();
		break;
	default:
		RK_LOGD("%s, cmd <%d> is not implemented.\n", __func__,
				  static_cast<BtControl_rep_type>(cmd));
		break;
	}

	return ret;
}
