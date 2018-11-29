/*
 * Copyright (c) 2017 Baidu, Inc. All Rights Reserved.
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

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::LedState;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::DeviceRTC;
using DeviceIOFramework::DevicePowerSupply;
using DeviceIOFramework::NetLinkNetworkStatus;

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

int main(int argc, char *argv[])
{
    char value[1024]; 
    bool ret;
    char sn[128] = {0};
    char hostname[64] = {0};
    #define HOST_NAME_PREFIX "小聚音箱mini-"

	DeviceIOFramework::Properties* properties;
	properties = DeviceIOFramework::Properties::getInstance();

    class DeviceInputWrapper *input = new DeviceInputWrapper();
    DeviceIo::getInstance()->setNotify(input);

    DeviceIo::getInstance()->setVolume(30,0);

    std::cout << "version:" << DeviceIo::getInstance()->getVersion() << std::endl;

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

    while(true) {
      if (argc > 1 && !strncmp(argv[1], "debug", 5)) {
        char szBuf[64] = {0};
        std::cout<<"Command Test:suspend,factoryReset,ota,led_test"<<std::endl;
        if(!std::cin.getline(szBuf,64)) {
            std::cout << "error" << std::endl;
            continue;
        }
        if (!strncmp(szBuf, "suspend", 7)) {
            DeviceIo::getInstance()->suspend();
        }
        if (!strncmp(szBuf, "factoryReset", 12)) {
            DeviceIo::getInstance()->factoryReset();
        }
        if (!strncmp(szBuf, "ota", 3)) {
            DeviceIo::getInstance()->OTAUpdate("");
        }
        if (!strncmp(szBuf, "led_test", 3)) {
            extern int led_test(int argc, char* argv[]);
            led_test(0, NULL);
        }
      } else {
        sleep(10);
      }
    }

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

