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

#ifndef DEVICEIO_FRAMEWORK_BLUETOOTH_H
#define DEVICEIO_FRAMEWORK_BLUETOOTH_H

#include <DeviceIo/DeviceIo.h>

using DeviceIOFramework::BtControl;

#define SYSTEM_BT_POWER_ON     "hciconfig hci0 up"
#define SYSTEM_BT_POWER_DOWN     "hciconfig hci0 down"

#define BT_SACN_SW 1
#if BT_SACN_SW
#define SYSTEM_BT_NOSCAN_CMD  "hciconfig hci0 noscan"
#define SYSTEM_BT_PISCAN_CMD  "hciconfig hci0 piscan"
#endif

#define SYSTEM_BT_RESTART_UP_CMD  "/oem/bt_restart.sh 1 &"
#define SYSTEM_BT_RESTART_DOWN_CMD  "/oem/bt_restart.sh 0 &"

#define SYSTEM_BT_RESTART_CMD1  "killall bluealsa"
#define SYSTEM_BT_RESTART_CMD2	"/oem/bt_start.sh &"

int rk_bt_control(BtControl cmd, void *data, int len);

#endif //DEVICEIO_FRAMEWORK_BLUETOOTH_H
