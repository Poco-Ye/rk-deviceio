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
#include <sys/select.h>
#include <linux/input.h>
#include <linux/rtc.h>
#include <DeviceIo/Rk_system.h>

#include "bt_test.h"
#include "rk_ble_app.h"

typedef struct {
	char *cmd;
	void (*action)(void *userdata);
} test_command_t;

static test_command_t process_command_table[] = {
	{"", NULL},
	{"bt_server_open", bt_test_init_open},
	{"bt_test_source_auto_start", bt_test_source_auto_start},
	{"bt_test_source_connect_status", bt_test_source_connect_status},
	{"bt_test_source_auto_stop", bt_test_source_auto_stop},
	{"bt_test_sink_open", bt_test_sink_open},
	{"bt_test_sink_visibility00", bt_test_sink_visibility00},
	{"bt_test_sink_visibility01", bt_test_sink_visibility01},
	{"bt_test_sink_visibility10", bt_test_sink_visibility10},
	{"bt_test_sink_visibility11", bt_test_sink_visibility11},
	{"bt_test_sink_status", bt_test_sink_status},
	{"bt_test_sink_music_play", bt_test_sink_music_play},
	{"bt_test_sink_music_pause", bt_test_sink_music_pause},
	{"bt_test_sink_music_next", bt_test_sink_music_next},
	{"bt_test_sink_music_previous", bt_test_sink_music_previous},
	{"bt_test_sink_music_stop", bt_test_sink_music_stop},
	{"bt_test_sink_reconnect_disenable", bt_test_sink_reconnect_disenable},
	{"bt_test_sink_reconnect_enable", bt_test_sink_reconnect_enable},
	{"bt_test_sink_disconnect", bt_test_sink_disconnect},
	{"bt_test_sink_close", bt_test_sink_close},
	{"bt_test_ble_start", bt_test_ble_start},
	{"bt_test_ble_write", bt_test_ble_write},
	{"bt_test_ble_stop", bt_test_ble_stop},
	{"bt_test_ble_get_status", bt_test_ble_get_status},
	{"bt_test_spp_open", bt_test_spp_open},
	{"bt_test_spp_write", bt_test_spp_write},
	{"bt_test_spp_close", bt_test_spp_close},
	{"bt_test_spp_status", bt_test_spp_status},
};

static void show_help() {
	int i;
	printf("#### Please Input Your Test Command Index ####\n");
	for (i = 0; i < sizeof(process_command_table) / sizeof(process_command_table[0]); i++) {
		printf("%02d.  %s \n", i, process_command_table[i].cmd);
	}
	printf("Which would you like: ");
}

int main(int argc, char *argv[])
{
	int i, item_cnt;
	char szBuf[64] = {0};
	char version[64] = {0};

	RK_read_version(version, 64);
	std::cout << "version:" << version << std::endl;
	item_cnt = sizeof(process_command_table) / sizeof(test_command_t);

	if (argc > 1 && !strncmp(argv[1], "blewifi", 5)) {
		rk_ble_wifi_init();
	}

	while(true) {
		if (argc > 1 && !strncmp(argv[1], "bluetooth", 5)) {
			memset(szBuf, 0, sizeof(szBuf));
			show_help();
			if(!std::cin.getline(szBuf, 64)) {
				std::cout << "error" << std::endl;
				continue;
			}

			i = atoi(szBuf);
			if ((i >= 1) && (i < item_cnt))
				process_command_table[i].action(NULL);

			continue;
		}

		sleep(10);
	}

	return 0;
}
