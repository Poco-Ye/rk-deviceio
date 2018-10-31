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
#include <math.h>
#include<iostream>

#include <DeviceIo/DeviceIo.h>

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;
using DeviceIOFramework::LedState;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::DevicePowerSupply;

class DeviceInputWrapper: public DeviceIOFramework::DeviceInNotify {
public:
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
            break;
        }
	case DeviceInput::KEY_ENTER_AP: {
            printf("key enter ap to config network\n");
            DeviceIo::getInstance()->startNetworkConfig();
            break;
        }
        case DeviceInput::KEY_HEADPHONE_INSERT: {
            if (*((int *)data) == 1)
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
      }
    }
};

int main()
{
    char value[1024]; 
    bool ret;

    class DeviceInputWrapper *input = new DeviceInputWrapper();
    DeviceIo::getInstance()->setNotify(input);

    std::cout << "version:" << DeviceIo::getInstance()->getVersion() << std::endl;

    DeviceIo::getInstance()->getSn(value);
    std::cout << "serial number:" << value << std::endl;

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

    if (DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_IS_FIRST_CONFIG)) {
        //DeviceIo::getInstance()->controlWifi(WifiControl::OPEN_SOFTAP);

        //wifi start wpa_supplicant for scanning
        DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN);
        sleep(3);
        //wifi scanning
        char *wifi_list = (char *)malloc(10 * 1024);
        memset(wifi_list, 0, 10 * 1024);
        DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_SCAN, wifi_list, 10 * 1024);
        std::cout << "wifi list: " << wifi_list << std::endl;
        free(wifi_list);
        //bt start ble server for networkconfig
        DeviceIo::getInstance()->controlBt(BtControl::BLE_OPEN_SERVER);
    } else {
        //network start auto connect
        DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN);
        //open bt a2dp sink
        DeviceIo::getInstance()->controlBt(BtControl::BT_OPEN);
    }

    while(true) {
        sleep(1);
    }

    return 0;
}
