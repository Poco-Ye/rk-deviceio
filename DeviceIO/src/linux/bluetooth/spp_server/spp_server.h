/*
 * (C) Copyright 2008-2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef __SPP_SERVER_
#define __SPP_SERVER_

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPP_MSG_TYPE_CONNECT,
    SPP_MSG_TYPE_DISCONNECT,
    SPP_MSG_TYPE_DATA
};

typedef void (*BtSppCallback)(int type, char *data, int len);

int bt_spp_server_open(BtSppCallback callback);
void bt_spp_server_close();
int bt_spp_write(char *data, int len);

#ifdef __cplusplus
}
#endif


#endif /* __SPP_SERVER_ */
