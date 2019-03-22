/*
 * (C) Copyright 2008-2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <termios.h>
#include <poll.h>
#include <pthread.h>
#include <sys/prctl.h>

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>

#include "spp_server.h"
#include "DeviceIo/DeviceIo.h"

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

#define SPP_SERVER_CHANNEL 1

static bool g_spp_server_running;
static int g_client_sk;
static int g_server_sk;
static RK_BT_SPP_STATUS_CALLBACK g_status_callback;
static RK_BT_SPP_RECV_CALLBACK g_recv_callback;
static int g_spp_server_channel = 1;
static int g_spp_server_status = RK_BT_SPP_STATE_IDLE;

static void report_spp_event(DeviceInput event, void *data, int len) {
	if (DeviceIo::getInstance()->getNotify())
		DeviceIo::getInstance()->getNotify()->callback(event, data, len);
}

static void *init_bt_spp_server(void *arg)
{
	struct sockaddr_rc loc_addr = {0}, rem_addr = {0};
	char buf[1024] = {0};
	char rem_addr_str[96] = {0};
	int bytes_read, result;
	int opt = sizeof(rem_addr);
	int parame = 1;

	prctl(PR_SET_NAME,"init_bt_spp_server");

	g_server_sk = socket(PF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if(g_server_sk < 0) {
		perror("create socket error");
		return;
	}

	/* Set address reuse */
	setsockopt(g_server_sk, SOL_SOCKET, SO_REUSEADDR, (const void *)&parame, sizeof(parame));

	loc_addr.rc_family = AF_BLUETOOTH;
	loc_addr.rc_bdaddr = *BDADDR_ANY;
	loc_addr.rc_channel = g_spp_server_channel;

	result = bind(g_server_sk, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
	if(result<0) {
		perror("bind socket error:");
		return;
	}

	result = listen(g_server_sk, 1);
	if(result < 0) {
		printf("error:%d/n:", result);
		perror("listen error:");
		return;
	}
	g_spp_server_running = true;

REPEAT:
	printf("Accepting...\n");
	g_client_sk = accept(g_server_sk, (struct sockaddr *)&rem_addr, &opt);
	if(g_client_sk < 0) {
		perror("accept error");
		return;
	} else {
		printf("OK!\n");
	}

	/* Reset buff */
	memset(buf, 0, sizeof(buf));
	memset(rem_addr_str, 0, sizeof(rem_addr_str));
	/* Get remote device addr */
	ba2str(&rem_addr.rc_bdaddr, rem_addr_str);
	fprintf(stderr, "accepted connection from %s \n", rem_addr_str);
	g_spp_server_status = RK_BT_SPP_STATE_CONNECT;
	report_spp_event(DeviceInput::SPP_CLIENT_CONNECT, (void *)rem_addr_str,
					 strlen(rem_addr_str));
	if (g_status_callback)
		(*g_status_callback)(RK_BT_SPP_STATE_CONNECT);

	while(g_spp_server_running) {
		bytes_read = read(g_client_sk, buf,sizeof(buf));
		if(bytes_read > 0){
			printf("received:%s\n", buf);
			if (g_recv_callback)
				(*g_recv_callback)(buf, bytes_read);

			memset(buf, 0, bytes_read);
		} else {
			g_spp_server_status = RK_BT_SPP_STATE_DISCONNECT;
			report_spp_event(DeviceInput::SPP_CLIENT_DISCONNECT, NULL, 0);
			if (g_status_callback)
				(*g_status_callback)(RK_BT_SPP_STATE_DISCONNECT);
			close(g_client_sk);
			g_client_sk = 0;
			if (g_spp_server_running)
				goto REPEAT;
		}
	}

	g_spp_server_status = RK_BT_SPP_STATE_IDLE;
	close(g_server_sk);
}

int bt_spp_server_open()
{
	int ret = 0;
	char cmd[128] = {'\0'};
	pthread_t bt_spp_server_thread;

	sprintf(cmd, "sdptool add --channel=%d SP", g_spp_server_channel);
	system(cmd);

	ret = pthread_create(&bt_spp_server_thread, NULL, init_bt_spp_server, NULL);
	if (ret < 0) {
		g_spp_server_status = RK_BT_SPP_STATE_IDLE;
		printf("%s failed!\n", __func__);
		return -1;
	}

	return 0;
}

void bt_spp_register_recv_callback(RK_BT_SPP_RECV_CALLBACK cb)
{
	g_recv_callback = cb;
}

void bt_spp_register_status_callback(RK_BT_SPP_STATUS_CALLBACK cb)
{
	g_status_callback = cb;
}

void bt_spp_server_close()
{
	g_spp_server_running = false;

	if (g_client_sk > 0) {
		close(g_client_sk);
		g_client_sk = 0;
	}

	if (g_server_sk) {
		close(g_server_sk);
		g_server_sk = 0;
	}

	g_spp_server_status = RK_BT_SPP_STATE_IDLE;
}

int bt_spp_write(char *data, int len)
{
	int ret = 0;

	if (g_client_sk <= 0) {
		printf("%s write failed! ERROR:No connection is ready!\n", __func__);
		return -1;
	}

	ret = write(g_client_sk, data, len);
	if (ret <= 0) {
		printf("%s write failed! ERROR:%s\n", __func__, strerror(errno));
		return ret;
	}

	return ret;
}

int bt_spp_get_status()
{
	return g_spp_server_status;
}

int bt_spp_set_channel(int channel)
{
	if ((channel > 0) && (channel < 256))
		g_spp_server_channel = channel;
	else {
		printf("%s WARING channel is not valid, use default channel:1.\n", __func__);
		g_spp_server_channel = SPP_SERVER_CHANNEL;
	}

	return 0;
}

int bt_spp_get_channel()
{
	return g_spp_server_channel;
}
