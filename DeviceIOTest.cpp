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

class DeviceInputWrapper: public DeviceIOFramework::DeviceInNotify {
public:
    void callback(DeviceInput event, void *data, int len) {
      printf("hardware  event:%d \n", static_cast<int>(event));

      switch (event) {
        case DeviceInput::KEY_VOLUME_DOWN: {
            break;
        }
        case DeviceInput::KEY_VOLUME_UP: {
            break;
        }
	case DeviceInput::KEY_MIC_MUTE: {
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

    DeviceIo::getInstance()->getWifiMac(value);
    std::cout << "Wifi Mac: " << value << std::endl;

    ret = DeviceIo::getInstance()->isWifiConnected();
    std::cout << "is wifi connected: " << ret << std::endl;

    ret = DeviceIo::getInstance()->startNetworkConfig();


    while(true)
        sleep(1);
    return 0;
}
