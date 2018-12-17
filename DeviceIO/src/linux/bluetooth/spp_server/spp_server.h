/*
 * (C) Copyright 2008-2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __SPP_SERVER_
#define __SPP_SERVER_

#include "DeviceIo/RkBtSpp.h"

#ifdef __cplusplus
extern "C" {
#endif

int bt_spp_server_open(RK_btspp_callback callback);
void bt_spp_server_close();
int bt_spp_write(char *data, int len);
int bt_spp_get_status();
int bt_spp_set_channel(int channel);
int bt_spp_get_channel();

#ifdef __cplusplus
}
#endif

#endif /* __SPP_SERVER_ */
