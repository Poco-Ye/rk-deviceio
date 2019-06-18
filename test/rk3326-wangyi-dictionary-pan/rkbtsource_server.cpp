/*
 * Copyright (c) 2017 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <iostream>
#include <linux/input.h>
#include <linux/rtc.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <DeviceIo/Rk_shell.h>
#include <DeviceIo/Rk_system.h>
#include <DeviceIo/RkBtBase.h>
#include <DeviceIo/RkBtSink.h>
#include <DeviceIo/RkBtSource.h>

#include "rkbtsource_common.h"

/* Immediate wifi Service UUID */
#define BLE_UUID_SERVICE	"0000180A-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_WIFI_CHAR	"00009999-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_PROXIMITY	"7B931104-1810-4CBC-94DA-875C8067F845"
#define BLE_UUID_SEND		"dfd4416e-1810-47f7-8248-eb8be3dc47f9"
#define BLE_UUID_RECV		"9884d812-1810-4a24-94d3-b2c11a851fac"

static int rk_bt_server_init(const char *name)
{
	static RkBtContent bt_content;

	printf("---------------BT SERVER INIT(%s)----------------\n", name);
	memset (&bt_content, 0, sizeof(bt_content));
	bt_content.bt_name = name;
	bt_content.ble_content.ble_name = "ROCKCHIP_AUDIO BLE";
	bt_content.ble_content.server_uuid.uuid = BLE_UUID_SERVICE;
	bt_content.ble_content.server_uuid.len = UUID_128;
	bt_content.ble_content.chr_uuid[0].uuid = BLE_UUID_WIFI_CHAR;
	bt_content.ble_content.chr_uuid[0].len = UUID_128;
	bt_content.ble_content.chr_uuid[1].uuid = BLE_UUID_SEND;
	bt_content.ble_content.chr_uuid[1].len = UUID_128;
	bt_content.ble_content.chr_uuid[2].uuid = BLE_UUID_RECV;
	bt_content.ble_content.chr_uuid[2].len = UUID_128;
	bt_content.ble_content.chr_cnt = 3;
	bt_content.ble_content.advDataType = BLE_ADVDATA_TYPE_SYSTEM;
	bt_content.ble_content.cb_ble_recv_fun = NULL;
	bt_content.ble_content.cb_ble_request_data = NULL;

	rk_bt_init(&bt_content);

	return 0;
}

static int rk_bt_server_deinit()
{
	char ret_buff[1024];

	printf("---------------BT SERVER DEINIT----------------\n");
	RK_shell_system("killall bluealsa");
	RK_shell_system("killall bluealsa-aplay");
	RK_shell_system("killall bluetoothctl");
	RK_shell_system("killall bluetoothd");

	msleep(100);
	RK_shell_exec("pidof bluetoothd", ret_buff, 1024);
	while (ret_buff[0]) {
		msleep(10);
		RK_shell_system("killall bluetoothd");
		msleep(100);
		RK_shell_exec("pidof bluetoothd", ret_buff, 1024);
	}

	RK_shell_system("killall rtk_hciattach");
	msleep(800);
	RK_shell_exec("pidof rtk_hciattach", ret_buff, 1024);
	while (ret_buff[0]) {
		msleep(10);
		RK_shell_system("killall rtk_hciattach");
		msleep(800);
		RK_shell_exec("pidof rtk_hciattach", ret_buff, 1024);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int i, item_cnt, ret, j;
	char buff[128] = {0};
	char scan_buff[2048] = {0};
	bt_msg_t *msg;
	scan_msg_t *scan_msg;
	int scan_msg_len = 0;
	char *offset;
	scan_devices_t *scan_devices;
	int sockfd = 0;
	struct sockaddr_un serverAddr;
	struct sockaddr_un clientAddr;
	socklen_t addr_len;
	BtScanParam scan_param;

	RK_read_version(buff, 128);
	printf("====== Version:%s =====\n", buff);
	memset(buff, 0, 128);

	sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		printf("Create socket failed!\n");
		return NULL;
	}

	/* Set client address */
	clientAddr.sun_family = AF_UNIX;
	strcpy(clientAddr.sun_path, "/tmp/rockchip_btsource_client");
	/* Set server address */
	system("rm -rf /tmp/rockchip_btsource_server");
	serverAddr.sun_family = AF_UNIX;
	strcpy(serverAddr.sun_path, "/tmp/rockchip_btsource_server");
	ret = bind(sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	if (ret < 0) {
		printf("%s:Bind Local addr failed!\n", PRINT_FLAG_ERR);
		return NULL;
	}

	msg = (bt_msg_t *)buff;
	addr_len = strlen(serverAddr.sun_path) + sizeof(serverAddr.sun_family);
	item_cnt = sizeof(bt_command_table) / sizeof(bt_command_t);

	while(1) {
		memset(buff, 0, sizeof(buff));
		ret = recv(sockfd, buff, sizeof(buff), 0);
		if (ret <= 0) {
			printf("%s:Recv cmd failed! ret = %d\n", PRINT_FLAG_ERR, ret);
			break;
		}

		for (i = 0; i < item_cnt; i++) {
			if (!strncmp(msg->cmd, bt_command_table[i].cmd, strlen(bt_command_table[i].cmd)))
				break;
		}

		if (i >= item_cnt) {
			printf("%s:Invalid cmd(%s) recved!\n", PRINT_FLAG_ERR, buff);
			continue;
		}

		switch (bt_command_table[i].cmd_id) {
			case RK_BT_SOURCE_INIT:
				if (strlen(msg->name))
					rk_bt_server_init(msg->name);
				else
					rk_bt_server_init("ROCKCHIP_AUDIO");
				rk_bt_source_open();
				printf("%s:bt server init sucess!\n", PRINT_FLAG_SUCESS);
				break;
			case RK_BT_SOURCE_CONNECT:
				ret = rk_bt_source_connect(msg->addr);
				if (ret < 0)
					printf("%s:connect %s failed!\n", PRINT_FLAG_ERR, msg->addr);
				else
					printf("%s:connect sucess!\n", PRINT_FLAG_SUCESS);
				break;
			case RK_BT_SOURCE_SCAN:
				/* Scan bluetooth devices */
				memset(&scan_param, 0, sizeof(scan_param));
				scan_param.mseconds = 10000; /* 10s for default */
				scan_param.item_cnt = 0;
				ret = rk_bt_source_scan(&scan_param);
				if (ret < 0) {
					printf("%s:scan failed!\n", PRINT_FLAG_ERR);
					break;
				}
				memset(scan_buff, 0, sizeof(scan_buff));
				scan_msg = (scan_msg_t *)scan_buff;
				scan_msg->magic[0] = 'r';
				scan_msg->magic[1] = 'k';
				scan_msg->magic[2] = 'b';
				scan_msg->magic[3] = 't';
				scan_msg->start_flag = 0x01;
				scan_msg->devices_cnt = 0;
				offset = scan_buff + sizeof(scan_msg_t);

				for (j = 0; j < scan_param.item_cnt; j++) {
					printf("\t name:%s\n", scan_param.devices[j].name);
					printf("\t address:%s\n", scan_param.devices[j].address);
					printf("\t playrole:%s\n", scan_param.devices[j].playrole);
					printf("\t rssi:%d\n\n", scan_param.devices[j].rssi);

					if (strncmp(scan_param.devices[j].playrole, "Audio Sink", 10))
						continue;

					scan_devices = (scan_devices_t *)(offset);
					ret = strlen(scan_param.devices[j].name);
					if (ret > sizeof(scan_devices->name))
						ret = sizeof(scan_devices->name);
					memcpy(scan_devices->name, scan_param.devices[j].name, ret);
					memcpy(scan_devices->addr, scan_param.devices[j].address, 17);
					if (scan_param.devices[j].rssi_valid)
						scan_devices->rssi = scan_param.devices[j].rssi * (-1);
					else
						scan_devices->rssi = 0xFF;

					scan_msg->devices_cnt++;
					offset += sizeof(scan_devices_t);
				}
				*offset = 0x04;
				scan_msg_len = sizeof(scan_msg_t) + scan_msg->devices_cnt * sizeof(scan_devices_t) + 1;
				ret = sendto(sockfd, scan_buff, scan_msg_len, 0, (struct sockaddr *)&clientAddr, sizeof(clientAddr));
				if (ret < 0) {
					printf("%s:scan failed! ret = %d\n", PRINT_FLAG_ERR, ret);
					break;
				}

				printf("%s:scan sucess!\n", PRINT_FLAG_SUCESS);
				break;
			case RK_BT_SOURCE_DISCONNECT:
				ret = rk_bt_source_disconnect(msg->addr);
				if (ret < 0)
					printf("%s:disconnect failed!\n", PRINT_FLAG_ERR);
				else
					printf("%s:disconnect sucess!\n", PRINT_FLAG_SUCESS);
				break;
			case RK_BT_SOURCE_REMOVE:
				ret = rk_bt_source_remove(msg->addr);
				if (ret < 0)
					printf("%s:remove failed!\n", PRINT_FLAG_ERR);
				else
					printf("%s:remove sucess!\n", PRINT_FLAG_SUCESS);
				break;
			case RK_BT_SOURCE_DEINIT:
				rk_bt_source_close();
				rk_bt_server_deinit();
				close(sockfd);
				printf("%s:deinit bt server sucess!\n", PRINT_FLAG_SUCESS);
				return 0;

			default:
				break;
		}
	}

	return 0;
}
