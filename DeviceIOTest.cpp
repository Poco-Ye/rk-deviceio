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
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/rtc.h>
#include <math.h>
#include<iostream>

#include <DeviceIo/DeviceIo.h>
#include <DeviceIo/Properties.h>
#include <DeviceIo/ScanResult.h>
#include <DeviceIo/WifiInfo.h>
#include <DeviceIo/WifiManager.h>

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::LedState;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::DeviceRTC;
using DeviceIOFramework::DevicePowerSupply;
using DeviceIOFramework::NetLinkNetworkStatus;
using DeviceIOFramework::wifi_config;
int gst_player(char *path);

static int g_player_flag_eos = 0;
static int g_player_flag_seekable = 0;
static int g_player_flag_duration = 0;

class DeviceInputWrapper: public DeviceIOFramework::DeviceInNotify{
public:
	void networkReady() {
		printf("net ready\n");
	}
	void netlinkNetworkOnlineStatus(bool status) {
		if (status)
			printf("net changed to online\n");
		else
			printf("net changed to offline\n");
	}
	void netlinkNetworkStatusChanged(NetLinkNetworkStatus networkStatus) {
		printf("%s : %d\n", __func__, networkStatus);
		switch (DeviceIo::getInstance()->getNetworkStatus()) {
			case DeviceIOFramework::NETLINK_NETWORK_CONFIG_STARTED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_WAIT_CONNECT);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_CONFIGING:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_DO_CONNECT);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_CONFIG_SUCCEEDED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_SUCCESS);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_CONFIG_FAILED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_FAILED);
				DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_RECOVERY);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_SUCCEEDED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_SUCCESS);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_FAILED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_FAILED);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_START:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_RECOVERY);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_SUCCEEDED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_SUCCESS);
				break;
			case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_FAILED:
				DeviceIo::getInstance()->controlLed(LedState::LED_NET_CONNECT_FAILED);
				break;
			default:
				break;
		}
	}
	void callback(DeviceInput event, void *data, int len) {
		//printf("hardware  event:%d \n", static_cast<int>(event));

		switch (event) {
		case DeviceInput::KEY_RAW_INPUT_EVENT: {
			struct input_event *ev = (struct input_event *)data;
			int key_code = ev->code;
			bool key_pressed = ev->value;

			if (key_pressed) {
				printf("KEY_RAW_INPUT_EVENT: key %d pressed\n", key_code);
				switch (key_code) {
					case KEY_MICMUTE:
						break;
					case KEY_MODE:
						break;
					case KEY_PLAY:
						break;
					case KEY_VOLUMEDOWN:
						break;
					case KEY_VOLUMEUP:
						break;
					default:
						break;
				}
			} else {
				if (key_code != 0)
					printf("KEY_RAW_INPUT_EVENT: key %d released\n", key_code);
			}
		}
		case DeviceInput::KEY_VOLUME_DOWN: {
			int vol = *((int *)data);
			int vol_cur = DeviceIo::getInstance()->getVolume();
			DeviceIo::getInstance()->setVolume(vol_cur - vol);
			break;
		}
		case DeviceInput::KEY_VOLUME_UP: {
			int vol = *((int *)data);
			int vol_cur = DeviceIo::getInstance()->getVolume();
			DeviceIo::getInstance()->setVolume(vol_cur + vol);
			break;
		}
	case DeviceInput::KEY_MIC_MUTE: {
			printf("key mic mute\n");
			static bool micmute = false;
			if (!micmute) {
				DeviceIo::getInstance()->controlLed(LedState::LED_MICMUTE);
			} else {
				LedState layer = LedState::LED_MICMUTE;
				DeviceIo::getInstance()->controlLed(LedState::LED_CLOSE_A_LAYER, &layer, sizeof(int));
			}

			micmute = !micmute;
			break;
		}
	case DeviceInput::KEY_ENTER_AP: {
			printf("key enter ap to config network\n");
			switch (DeviceIo::getInstance()->getNetworkStatus()) {
				case DeviceIOFramework::NETLINK_NETWORK_CONFIG_STARTED:
				case DeviceIOFramework::NETLINK_NETWORK_CONFIGING:
						DeviceIo::getInstance()->stopNetworkConfig();
						break;
				case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_START:
						DeviceIo::getInstance()->stopNetworkRecovery();
						break;
				default:
						DeviceIo::getInstance()->startNetworkConfig(90);
						break;
			}
			break;
		}
		case DeviceInput::KEY_HEADPHONE_INSERT: {
			if ((data != NULL) && *((int *)data) == 1)
				printf("headphone inserted\n");
			else
				printf("headphone plug out\n");
			break;
		}
		case DeviceInput::KEY_RK816_POWER: { //xiaojv
			printf("xiaojv key pressed\n");
			break;
		}
		case DeviceInput::KEY_SHUT_DOWN: { //shutdown
			printf("key shut down key pressed\n");
			break;
		}
		case DeviceInput::KEY_FACTORY_RESET: {
			printf("key factory reset\n");
			DeviceIo::getInstance()->factoryReset();
			break;
		}
			case DeviceInput::BT_CONNECT: {
				printf("=== BT_CONNECT ===\n");
				break;
			}
			case DeviceInput::BT_DISCONNECT: {
				printf("=== BT_DISCONNECT ===\n");
				break;
			}
			case DeviceInput::BT_START_PLAY: {
				printf("=== BT_START_PLAY ===\n");
				break;
			}
			case DeviceInput::BT_PAUSE_PLAY: {
				printf("=== BT_PAUSE_PLAY ===\n");
				break;
			}
			case DeviceInput::BT_STOP_PLAY: {
				printf("=== BT_STOP_PLAY ===\n");
			break;
		}
		case DeviceInput::GST_PLAYER_READY:
			printf("=== GST_PLAYER_READY ===\n");
			break;
		case DeviceInput::GST_PLAYER_PAUSED:
			printf("=== GST_PLAYER_PAUSED ===\n");
			break;
		case DeviceInput::GST_PLAYER_PLAYING:
			printf("=== GST_PLAYER_PLAYING ===\n");
			break;
		case DeviceInput::GST_PLAYER_SEEKABLE:
			printf("=== GST_PLAYER_SEEKABLE ===\n");
			g_player_flag_seekable = 1;
			break;
		case DeviceInput::GST_PLAYER_EOS:
			g_player_flag_eos = 1;
			printf("=== GST_PLAYER_EOS ===\n");
			break;
		case DeviceInput::GST_PLAYER_ERROR:
			printf("=== GST_PLAYER_ERROR ===\n");
			break;
		case DeviceInput::GST_PLAYER_DURATION:
			g_player_flag_duration = 1;
			printf("=== GST_PLAYER_DURATION ===\n");
			break;
		}
	}
};
int a2dp_source_main()
{
	char value[1024]; 
	bool ret;
	char buff[4096];
	BtScanParam scan_param;
	int id = 0;

	std::cout << "Bt Open source... " << std::endl;
	DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_OPEN, NULL, 0);

	char cmd[128];
	while(1) {
		scanf("%s", cmd);
		printf("CMD:%s\n", cmd);
		if (strcmp(cmd, "connect") == 0) {
			printf("CONNECT: Please input target device addr:\n");
			scanf("%s", cmd);
			printf("Connecting %s...\n", cmd);
			DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_CONNECT, cmd, 0);
		} else if (strcmp(cmd, "disconnect") == 0) {
			printf("DISCONNECT: Please input target device addr:\n");
			scanf("%s", cmd);
			printf("Disconnecting %s...\n", cmd);
			DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_DISCONNECT, cmd, 0);
		} else if (strcmp(cmd, "exit") == 0) {
			printf("EXIT\n");
			std::cout << "Bt Close source... " << std::endl;
			DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_CLOSE, NULL, 0);
			break;
		} else if (strcmp(cmd, "scan") == 0) {
			printf("SCAN\n");
			scan_param.mseconds = 10000;
			scan_param.item_cnt = 100;
			scan_param.device_list = NULL;

			std::cout << "Bt Scan start... " << std::endl;
			DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_SCAN, &scan_param, sizeof(scan_param));

			/*
			* Find the sink device from the device list,
			* which has the largest rssi value.
			*/
			BtDeviceInfo *start = scan_param.device_list;
			BtDeviceInfo *tmp = NULL;
			while (start) {
				printf("\n[%d]Name:%s\n", id++, start->name);
				printf("\tAddress:%s\n", start->address);
				printf("\tRSSI:%d\n", start->rssi_valid?start->rssi:1);
				printf("\tPlayrole:%s\n", start->playrole);

				tmp = start;
				start = start->next;
				/* Free DeviceInfo */
				free(tmp);
			}
		} else if (strcmp(cmd, "remove") == 0) {
			printf("REMOVE: Please input target device addr:\n");
			scanf("%s", cmd);
			printf("Removeing %s...\n", cmd);
			DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_REMOVE, cmd, 0);
		} else {
			ret = DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_STATUS, value, 0);
			if (ret)
				printf("STATUS: connected! addr = %s\n", value);
			else
				printf("STATUS: disconnected!\n");
		}
	}

	return 0;
}

static void wifi_status_callback(int status)
{
	printf("%s: status: %d.\n", __func__, status);
}

typedef struct {
	char *cmd;
	void (*action)(void *userdata);
} test_command_t;

static void suspend_test(void *data) {
	DeviceIo::getInstance()->suspend();
}

static void factoryReset_test(void *data) {
	DeviceIo::getInstance()->factoryReset();
}

static void ota_test(void *data) {
	DeviceIo::getInstance()->OTAUpdate("");
}

static void leds_test(void *data) {
	extern int led_test(int argc, char* argv[]);
	led_test(0, NULL);
}

static void bt_sink_test(void *data) {
	printf("---------------bt sink ----------------\n");
	DeviceIo::getInstance()->controlBt(BtControl::BT_SOURCE_CLOSE);
	DeviceIo::getInstance()->controlBt(BtControl::BT_SINK_OPEN);
	sleep(6);
	DeviceIo::getInstance()->controlBt(BtControl::BT_SINK_POWER);
}

static void bt_auto_source_test(void *data) {
	char address[17] = {0};
	printf("--------------- ble wifi ----------------\n");
	DeviceIo::getInstance()->a2dpSourceAutoConnect(address, 10000);
}

static void bt_source_test(void *data) {
	printf("---------------bt sink ----------------\n");
	DeviceIo::getInstance()->controlBt(BtControl::BT_SINK_CLOSE);
	a2dp_source_main();
}

static void ble_wifi_manual_test(void *data) {
	printf("--------------- ble wifi ----------------\n");
	DeviceIo::getInstance()->startNetworkConfig(600);
}

static void ble_wifi_test(void *data) {
	printf("--------------- ble wifi ----------------\n");
	DeviceIo::getInstance()->startNetworkRecovery();
}

static test_command_t process_command_table[] = {
	{"suspend", suspend_test},
	{"factoryReset", factoryReset_test},
	{"ota", ota_test},
	{"leds", leds_test},
	{"bt_sink", bt_sink_test},
	{"bt_auto_source", bt_auto_source_test},
	{"bt_source", bt_source_test},
	{"ble_wifi_manual", ble_wifi_manual_test},
	{"ble_wifi", ble_wifi_test},
};

static void show_help() {
	int i;
	printf("#### Please Input Your Test Command Index ####\n");
	for (i = 0; i < sizeof(process_command_table) / sizeof(process_command_table[0]); i++) {
		printf("%d.  %s \n", i, process_command_table[i].cmd);
	}
	printf("Which would you like: ");
}

int main(int argc, char *argv[])
{
	char value[1024]; 
	bool ret;
	char sn[128] = {0};
	char hostname[64] = {0};
	#define HOST_NAME_PREFIX "小聚音箱mini-"

	DeviceIOFramework::Properties* properties;
	properties = DeviceIOFramework::Properties::getInstance();
	properties->init();

	DeviceIOFramework::WifiManager* wifiManager;
	wifiManager = DeviceIOFramework::WifiManager::getInstance();
	wifiManager->init(properties);

	class DeviceInputWrapper *input = new DeviceInputWrapper();
	DeviceIo::getInstance()->setNotify(input);

	DeviceIo::getInstance()->setVolume(30,0);

	std::cout << "version:" << DeviceIo::getInstance()->getVersion() << std::endl;
	/*
	DeviceIo::getInstance()->getSn(sn);
	std::cout << "serial number:" << sn << std::endl;

	//set hostname of speaker before wifi/bt start
	if (strlen(sn) < 4)
		sprintf(hostname, "%s%s", HOST_NAME_PREFIX, "1234");
	else
		sprintf(hostname, "%s%s", HOST_NAME_PREFIX, sn + strlen(sn) - 4);
	DeviceIo::getInstance()->sethostname(hostname, strlen(hostname));

	std::string chipid = DeviceIo::getInstance()->getChipID();
	std::cout << "Chip ID : " << chipid.c_str() << std::endl;

	unsigned int bat_temp_period = 3;
	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_BAT_TEMP_PERIOD_DETECT, &bat_temp_period, 0);
	std::cout << "Set Battery Temperture Period Detect: " << bat_temp_period << std::endl;

	int bat_temp_threshold_min = 0;
	int bat_temp_threshold_max = 55;
	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_BAT_TEMP_THRESHOLD_MIN,
			&bat_temp_threshold_min, 0);
	std::cout << "Set Battery Temperture Threshold Min: " << bat_temp_threshold_min << std::endl;

	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_BAT_TEMP_THRESHOLD_MAX,
			&bat_temp_threshold_max, 0);
	std::cout << "Set Battery Temperture Threshold Max: " << bat_temp_threshold_max << std::endl;

	DeviceIo::getInstance()->controlPower(DevicePowerSupply::USB_ONLINE, value, 1024);
	std::cout << "Get USB Charge Status: " << value << std::endl;

	DeviceIo::getInstance()->controlPower(DevicePowerSupply::AC_ONLINE, value, 1024);
	std::cout << "Get AC Charge Status: " << value << std::endl;

	int charge_enable = 0;
	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_BAT_CHARGE_DISABLE,
			&charge_enable, 0);
	std::cout << "Disable Charge" << std::endl;

	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_GET_CHARGE_ENABLE_STATUS, value, 1);
	std::cout << "Charging Enable Status: " << ((value[0] == '1')? "YES":"NO") <<  std::endl;

	charge_enable = 1;
	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_BAT_CHARGE_ENABLE,
			&charge_enable, 0);
	std::cout << "Enable Charge" << std::endl;

	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_GET_CHARGE_ENABLE_STATUS, value, 1);
	std::cout << "Charging Enable Status: " << ((value[0] == '1')? "YES":"NO") <<  std::endl;

	struct rtc_time tmp_rtc;
	std::cout << "Rtc Read time:" << std::endl;
	DeviceIo::getInstance()->controlRtc(DeviceRTC::DEVICE_RTC_READ_TIME, &tmp_rtc, sizeof(tmp_rtc));

	unsigned int settime = 3;
	std::cout << "Rtc Set alarm wakeup after " << settime << " seconds" << std::endl;
	DeviceIo::getInstance()->controlRtc(DeviceRTC::DEVICE_RTC_SET_TIME, &settime, 0);

	std::cout << "Rtc Read alarm setting" << std::endl;
	DeviceIo::getInstance()->controlRtc(DeviceRTC::DEVICE_RTC_READ_ALARM, &tmp_rtc, 0);

	std::cout << "Rtc Enable alarm" << std::endl;
	DeviceIo::getInstance()->controlRtc(DeviceRTC::DEVICE_RTC_ENABLE_ALARM_INTERRUPT, NULL, 0);

	std::cout << "Rtc Wait alarm wakeup" << std::endl;
	DeviceIo::getInstance()->controlRtc(DeviceRTC::DEVICE_RTC_WAIT_ALARM_RING, NULL, 0);

	DeviceIo::getInstance()->controlPower(DevicePowerSupply::BATTERY_CAPACITY, value, 18);
	std::cout << "Get Battery Capacity: " << value << std::endl;

	unsigned int det_period = 30;
	DeviceIo::getInstance()->controlPower(DevicePowerSupply::POWER_CFG_CAPACITY_DETECT_PERIOD, &det_period, 0);
	std::cout << "Set Battery Capacity Detect Period: " << det_period << " seconds." << std::endl;

	DeviceIo::getInstance()->controlWifi(WifiControl::GET_WIFI_MAC, value, 18);
	std::cout << "Wifi Mac: " << value << std::endl;

	DeviceIo::getInstance()->controlBt(BtControl::GET_BT_MAC, value, 18);
	std::cout << "Bt Mac: " << value << std::endl;

	ret = DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_IS_CONNECTED);
	std::cout << "is wifi connected: " << ret << std::endl;

	int max_brightness = 255;
	int min_brightness = 0;
	DeviceIo::getInstance()->controlLed(LedState::LED_ALL_OFF);
	sleep(1);
	DeviceIo::getInstance()->controlLed(LedState::LED_PWMR_SET, &max_brightness, sizeof(int));
	sleep(1);
	DeviceIo::getInstance()->controlLed(LedState::LED_PWMR_SET, &min_brightness, sizeof(int));
	DeviceIo::getInstance()->controlLed(LedState::LED_PWMG_SET, &max_brightness, sizeof(int));
	sleep(1);
	DeviceIo::getInstance()->controlLed(LedState::LED_PWMG_SET, &min_brightness, sizeof(int));
	DeviceIo::getInstance()->controlLed(LedState::LED_PWMB_SET, &max_brightness, sizeof(int));
	sleep(1);
	DeviceIo::getInstance()->controlLed(LedState::LED_ALL_OFF);

	DeviceIo::getInstance()->setEQParameter("/data/eq_bin_new");

	DeviceIo::getInstance()->startNetworkRecovery();
	*/
	while(true) {
		if (argc > 1 && !strncmp(argv[1], "debug", 5)) {
			char szBuf[64] = {0};
			show_help();
			if(!std::cin.getline(szBuf,64)) {
				std::cout << "error" << std::endl;
				continue;
			}
			int i;
			//匹配数字
			if (szBuf[0] >= '0' && szBuf[0] <= '9') {
				i = atoi(szBuf);
				if (i >=0 && i < sizeof(process_command_table) / sizeof(process_command_table[0]))
					process_command_table[i].action(NULL);
			}
			//匹配字符串
			for (i = 0; i < sizeof(process_command_table) / sizeof(process_command_table[0]); i++) {
				if (!strcmp(szBuf, process_command_table[i].cmd)) {
					process_command_table[i].action(NULL);
					break;
				}
			}
		} else {
			sleep(10);
		}
	}

	if (NULL != wifiManager)
		delete wifiManager;

	if (NULL != properties)
		delete properties;

	return 0;
}

int gst_player(char *path)
{
	bool ret;
	int timer = 0;
	int64_t duration = 0;
	int64_t position = 0;

	printf("<%s> File Path:%s\n", __func__, path);
	ret = DeviceIo::getInstance()->gstPlayerCreate(path);
	if (ret) {
		printf("<%s> Create Player failed!\n", __func__);
		return -1;
	} else
		printf("<%s> Create Player sucess!\n", __func__);

	ret = DeviceIo::getInstance()->gstPlayerStart();
	if (ret) {
		printf("<%s> Player start failed!\n", __func__);
		return -1;
	} else
		printf("<%s> Player start sucess!\n", __func__);

	while(!g_player_flag_eos) {
		if (timer == 10) {
			if (!g_player_flag_seekable)
				printf("<%s> Seek func is not enable yet.\n", __func__);
			else {
				printf("<%s> Already played for 10 seconds, seek to 20 seconds\n", __func__);
				ret = DeviceIo::getInstance()->gstPlayerSeek(20);
				if (ret)
					printf("<%s> Seek error\n", __func__);
			}
		} else if (timer == 20) {
			printf("<%s> Test pause.\n", __func__);
			ret = DeviceIo::getInstance()->gstPlayerPause();
			if (ret)
				printf("<%s> Pause error\n", __func__);
		} else if (timer == 25) {
			printf("<%s> Test resume.\n", __func__);
			ret = DeviceIo::getInstance()->gstPlayerResume();
			if (ret)
				printf("<%s> Resume error\n", __func__);
		}

		if (!duration && g_player_flag_duration) {
			duration = DeviceIo::getInstance()->gstPlayerGetDuration();
			if (duration < 0) {
				printf("<%s> Get duration failed!\n", __func__);
				duration = 0;
			} else
				printf("<%s> Duration = %dms\n", __func__, (int)(duration / 1000000));
		}

		position = DeviceIo::getInstance()->gstPlayerGetPosition();
		if (position > 0)
			printf("<%s> Position:%dms\n", __func__, (int)(position / 1000000));
		else
			printf("<%s> Get position failed!\n", __func__);

		sleep(1);
		timer++;
	}

	DeviceIo::getInstance()->gstPlayerClose();
	printf("<%s> END\n", __func__);
	return 0;
}

