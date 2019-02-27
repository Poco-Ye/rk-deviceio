/*****************************************************************************
 **
 **  Name:           bluetooth_bsa.h
 **
 **  Description:    Bluetooth API
 **
 **  Copyright (c) 2019, Rockchip Corp., All Rights Reserved.
 **  Rockchip Bluetooth Core. Proprietary and confidential.
 **
 *****************************************************************************/
#ifndef BLUETOOTH_BSA_H
#define BLUETOOTH_BSA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "DeviceIo/DeviceIo.h"
#include "DeviceIo/RkBtMaster.h"
#include "DeviceIo/RkBtSink.h"
#include "DeviceIo/RkBle.h"
#include "DeviceIo/RkBtSpp.h"
#include "DeviceIo/Rk_bt.h"

int RK_bt_init(Bt_Content_t *p_bt_content);
void RK_ble_test(void *data);
int RK_bt_open(const char *bt_name);
void RK_bt_close(void);
void RK_get_bt_mac(char *bt_mac);
int RK_bta2dp_volume_up(void);
int RK_bta2dp_volume_down(void);
int rk_bt_control(DeviceIOFramework::BtControl cmd, void *data, int len);

#endif
