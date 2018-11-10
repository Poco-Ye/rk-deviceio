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

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::LedState;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::DeviceRTC;
using DeviceIOFramework::DevicePowerSupply;
using DeviceIOFramework::NetLinkNetworkStatus;

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
            case DeviceIOFramework::NETLINK_NETWORK_CONFIGING:
            case DeviceIOFramework::NETLINK_NETWORK_CONFIG_SUCCEEDED:
            case DeviceIOFramework::NETLINK_NETWORK_CONFIG_FAILED:
            case DeviceIOFramework::NETLINK_NETWORK_SUCCEEDED:
            case DeviceIOFramework::NETLINK_NETWORK_FAILED:
            case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_START:
            case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_SUCCEEDED:
            case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_FAILED:
                break;
            }
    }
    void callback(DeviceInput event, void *data, int len) {
      printf("hardware  event:%d \n", static_cast<int>(event));

      switch (event) {
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
				DeviceIo::getInstance()->controlBt(BtControl::BT_PLAY);
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
      }
    }
};

int main()
{
    char value[1024]; 
    bool ret;

    class DeviceInputWrapper *input = new DeviceInputWrapper();
    DeviceIo::getInstance()->setNotify(input);

    DeviceIo::getInstance()->setVolume(30,0);

    std::cout << "version:" << DeviceIo::getInstance()->getVersion() << std::endl;

    DeviceIo::getInstance()->getSn(value);
    std::cout << "serial number:" << value << std::endl;

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

    DeviceIo::getInstance()->startNetworkRecovery();

    while(true) {
        char szBuf[64] = {0};
        std::cout<<"Command Test:suspend,factoryReset,ota"<<std::endl;
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


    }

    return 0;
}
