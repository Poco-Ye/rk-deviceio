#include <string>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <csignal>
#include <errno.h>
#include <paths.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#include <DeviceIo/DeviceIo.h>
#include <DeviceIo/Rk_wifi.h>
#include <DeviceIo/RK_log.h>
#include <DeviceIo/Rk_shell.h>
#include <DeviceIo/RkBtBase.h>
#include <DeviceIo/RkBle.h>

#include "avrcpctrl.h"
#include "bluez_ctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/shell.h"
#include "spp_server/spp_server.h"

extern RkBtContent GBt_Content;
extern volatile bt_control_t bt_control;

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::wifi_config;

#define BT_CONFIG_FAILED 2
#define BT_CONFIG_OK 1

#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */

/*****************************************************************
 *            Rockchip bluetooth LE api                      *
 *****************************************************************/

RK_BLE_STATE_CALLBACK ble_status_callback = NULL;
RK_BLE_STATE g_ble_status;

int rk_ble_start(RkBleContent *ble_content)
{
	rk_bt_control(BtControl::BT_BLE_OPEN, NULL, 0);
	if (ble_status_callback)
		ble_status_callback(RK_BLE_STATE_IDLE);
	g_ble_status = RK_BLE_STATE_IDLE;

	return 0;
}

int rk_ble_stop(void)
{
	rk_bt_control(BtControl::BT_BLE_COLSE, NULL, 0);
	return 0;
}

int rk_ble_get_state(RK_BLE_STATE *p_state)
{
	if (p_state)
		*p_state = g_ble_status;

	return 0;
}

#define BLE_SEND_MAX_LEN (134) //(20) //(512)
int rk_ble_write(const char *uuid, char *data, int len)
{
#if 1
	RkBleConfig ble_cfg;

	ble_cfg.len = (len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : len;
	memcpy(ble_cfg.data, data, ble_cfg.len);
	strcpy(ble_cfg.uuid, uuid);
	rk_bt_control(BtControl::BT_BLE_WRITE, &ble_cfg, sizeof(RkBleConfig));

	return 0;
#else
	/*
	 * The following code is pseudo code, which is used to illustrate
	 * another implementation of the interface.
	 */
	RkBleConfig ble_cfg;
	int tmp = 0;
	int mtu = 0;
	int ret = 0;

	mtu = RK_ble_get_mtu(uuid);
	if (mtu == 0) {
		/* Use recommended values, which are compatible with higher values */
		mtu = BLE_SEND_MAX_LEN;
	}

	while (len) {
		if (len > mtu) {
			tmp = mtu;
			len -= mtu;
		} else {
			tmp = len;
			len = 0;
		}
		ret = rk_bt_control(BtControl::BT_BLE_WRITE, &ble_cfg, sizeof(RkBleConfig));
		if (ret < 0) {
			printf("rk_ble_write failed!\n");
			return ret;
		}
	}

	return ret;
#endif
}

int rk_ble_register_status_callback(RK_BLE_STATE_CALLBACK cb)
{
	if (cb)
		ble_status_callback = cb;

	return 0;
}

int rk_ble_register_recv_callback(RK_BLE_RECV_CALLBACK cb)
{
	if (cb) {
		printf("BlueZ does not support this interface."
			"Please set the callback function when initializing BT.\n");
	}

	return 0;
}

/*****************************************************************
 *            Rockchip bluetooth master api                      *
 *****************************************************************/
static RK_BT_SOURCE_CALLBACK g_btmaster_user_cb;
static void *g_btmaster_user_data;
static pthread_t g_btmaster_thread;

static void _btmaster_send_event(RK_BT_SOURCE_EVENT event)
{
	if (g_btmaster_user_cb)
		(*g_btmaster_user_cb)(g_btmaster_user_data, event);
}

static void* _btmaster_autoscan_and_connect(void *data)
{
	BtScanParam scan_param;
	BtDeviceInfo *start;
	int max_rssi = -100;
	int ret = 0;
	char target_address[17] = {0};
	bool target_vaild = false;
	int scan_cnt, i;

	/* Scan bluetooth devices */
	scan_param.mseconds = 10000; /* 10s for default */
	scan_param.item_cnt = 0;
	scan_cnt = 3;

	prctl(PR_SET_NAME,"_btmaster_autoscan_and_connect");

scan_retry:
	printf("=== BT_SOURCE_SCAN ===\n");
	ret = a2dp_master_scan((void *)&scan_param, sizeof(scan_param));
	if (ret && (scan_cnt--)) {
		sleep(1);
		goto scan_retry;
	} else if (ret) {
		printf("ERROR: Scan error!\n");
		_btmaster_send_event(BT_SOURCE_EVENT_CONNECT_FAILED);
		g_btmaster_thread = 0;
		return NULL;
	}

	/*
	 * Find the audioSink device from the device list,
	 * which has the largest rssi value.
	 */
	max_rssi = -100;	
	for (i = 0; i < scan_param.item_cnt; i++) {
		start = &scan_param.devices[i];
		if (start->rssi_valid && (start->rssi > max_rssi) &&
			(!strcmp(start->playrole, "AudioSink"))) {
			printf("#%02d Name:%s\n", i, start->name);
			printf("\tAddress:%s\n", start->address);
			printf("\tRSSI:%d\n", start->rssi);
			printf("\tPlayrole:%s\n", start->playrole);
			max_rssi = start->rssi;

			memcpy(target_address, start->address, 17);
			target_vaild = true;
		}
	}

	if (!target_vaild) {
		printf("=== Cannot find audioSink devices. ===\n");
		_btmaster_send_event(BT_SOURCE_EVENT_CONNECT_FAILED);
		g_btmaster_thread = 0;
		return;
	} else if (max_rssi < -80) {
		printf("=== BT SOURCE RSSI is is too weak !!! ===\n");
		_btmaster_send_event(BT_SOURCE_EVENT_CONNECT_FAILED);
		g_btmaster_thread = 0;
		return NULL;
	}

	/* Connect target device */
	if (!a2dp_master_status(NULL, NULL))
		a2dp_master_connect(target_address);

	return NULL;
}

/*
 * Turn on Bluetooth and scan SINK devices.
 * Features:
 *     1. turn on Bluetooth
 *     2. enter the bt source mode
 *     3. Scan surrounding SINK type devices
 *     4. If the SINK device is found, the device with the strongest
 *        signal is automatically connected.
 * Return:
 *     0: The function is executed successfully and needs to listen
 *        for Bluetooth connection events.
 *    -1: Function execution failed.
 */
int rk_bt_source_auto_connect_start(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
	int ret;

	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	if (g_btmaster_thread) {
		printf("The last operation is still in progress\n");
		return -1;
	}

	/* Register callback and userdata */
	a2dp_master_register_cb(userdata, cb);
	g_btmaster_user_data = userdata;
	g_btmaster_user_cb = cb;

	/* Set bluetooth to master mode */
	printf("=== BtControl::BT_SOURCE_OPEN ===\n");
	bt_control.type = BtControlType::BT_SOURCE;
	if (!bt_source_is_open()) {
		if (bt_sink_is_open()) {
			RK_LOGE("bt sink isn't coexist with source!!!\n");
			bt_close_sink();
		}

		if (bt_interface(BtControl::BT_SOURCE_OPEN, NULL) < 0) {
			bt_control.is_a2dp_source_open = 0;
			bt_control.type = BtControlType::BT_NONE;
			return -1;
		}

		bt_control.is_a2dp_source_open = true;
		bt_control.type = BtControlType::BT_SOURCE;
		bt_control.last_type = BtControlType::BT_SOURCE;
	}
	/* Already connected? */
	if (a2dp_master_status(NULL, NULL)) {
		printf("=== BT SOURCE is connected!!! ===\n");
		return 0;
	}
	/* Create thread to do connect task. */
	ret = pthread_create(&g_btmaster_thread, NULL,
						 _btmaster_autoscan_and_connect, NULL);
	if (ret) {
		printf("_btmaster_autoscan_and_connect thread create failed!\n");
		return -1;
	}

	return 0;
}

int rk_bt_source_auto_connect_stop(void)
{
	if (g_btmaster_thread)
		pthread_join(g_btmaster_thread, NULL);

	g_btmaster_thread = 0;
	return rk_bt_source_close();
}

int rk_bt_source_close(void)
{
	bt_close_source();
	a2dp_master_clear_cb();
	return 0;
}

int rk_bt_source_get_device_name(char *name, int len)
{
	if (!name || (len <= 0))
		return -1;

	if (a2dp_master_status(NULL, name))
		return 0;

	return -1;
}

int rk_bt_source_get_device_addr(char *addr, int len)
{
	if (!addr || (len < 17))
		return -1;

	if (a2dp_master_status(addr, NULL))
		return 0;

	return -1;
}

int rk_bt_source_get_status(RK_BT_SOURCE_STATUS *pstatus, char *name, char *address)
{
	if (!pstatus)
		return 0;

	if (a2dp_master_status(name, address))
		*pstatus = BT_SOURCE_STATUS_CONNECTED;
	else
		*pstatus = BT_SOURCE_STATUS_DISCONNECTED;

	return 0;
}

/*****************************************************************
 *            Rockchip bluetooth sink api                        *
 *****************************************************************/
int rk_bt_sink_register_callback(RK_BT_SINK_CALLBACK cb)
{
	a2dp_sink_register_cb(cb);
	return 0;
}

int rk_bt_sink_open()
{
	char set_hostname_cmd[HOSTNAME_MAX_LEN + 64] = {'\0'};

	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	/* Already in sink mode? */
	if (bt_sink_is_open())
		return 0;

	if (bt_source_is_open()) {
		RK_LOGE("bt sink isn't coexist with source!!!\n");
		bt_close_source();
	}

	if (bt_interface(BtControl::BT_SINK_OPEN, NULL) < 0) {
		bt_control.is_a2dp_sink_open = 0;
		bt_control.type = BtControlType::BT_NONE;
		return -1;
	}

	bt_control.is_a2dp_sink_open = 1;
	/* Set bluetooth control current type */
	bt_control.type = BtControlType::BT_SINK;
	bt_control.last_type = BtControlType::BT_SINK;

	return 0;
}

int rk_bt_sink_set_visibility(const int visiable, const int connectal)
{
	RK_shell_system("hciconfig hci0 noscan");
	usleep(2000);//2ms
	if (visiable)
		RK_shell_system("hciconfig hci0 iscan");
	if (connectal)
		RK_shell_system("hciconfig hci0 pscan");

	return 0;
}

int rk_bt_sink_close(void)
{
	bt_close_sink();

	return 1;
}

int rk_bt_sink_get_state(RK_BT_SINK_STATE *pState)
{
	return a2dp_sink_status(pState);
}

int rk_bt_sink_play(void)
{
	if (bt_control_cmd_send(BtControl::BT_RESUME_PLAY) < 0)
		return -1;

	return 0;
}

int rk_bt_sink_pause(void)
{
	if (bt_control_cmd_send(BtControl::BT_PAUSE_PLAY) < 0)
		return -1;

	return 0;
}

int rk_bt_sink_prev(void)
{
	if (bt_control_cmd_send(BtControl::BT_AVRCP_BWD) < 0)
		return -1;

	return 0;
}

int rk_bt_sink_next(void)
{
	if (bt_control_cmd_send(BtControl::BT_AVRCP_FWD) < 0)
		return -1;

	return 0;
}

int rk_bt_sink_stop(void)
{
	if (bt_control_cmd_send(BtControl::BT_AVRCP_STOP) < 0)
		return -1;

	return 0;
}

int rk_bt_sink_set_auto_reconnect(int enable)
{
	a2dp_sink_set_auto_reconnect(enable);
	return 0;
}

int rk_bt_sink_disconnect()
{
	disconn_device();
	return 0;
}

/*****************************************************************
 *            Rockchip bluetooth spp api                         *
 *****************************************************************/
int rk_bt_spp_open()
{
	int ret = 0;

	ret = rk_bt_sink_open();
	if (ret)
		return ret;

	ret = bt_spp_server_open();
	return ret;
}

int rk_bt_spp_register_status_cb(RK_BT_SPP_STATUS_CALLBACK cb)
{
	bt_spp_register_status_callback(cb);
}

int rk_bt_spp_register_recv_cb(RK_BT_SPP_RECV_CALLBACK cb)
{
	bt_spp_register_recv_callback(cb);
}

int rk_bt_spp_close(void)
{
	bt_spp_server_close();
}

int rk_bt_spp_get_state(RK_BT_SPP_STATE *pState)
{
	if (pState)
		*pState = bt_spp_get_status();

	return 0;
}

int rk_bt_spp_write(char *data, int len)
{
	return bt_spp_write(data, len);
}

//====================================================//
int rk_bt_init(RkBtContent *p_bt_content)
{
	rk_bt_control(BtControl::BT_OPEN, p_bt_content, sizeof(RkBtContent));
	sleep(1);

	return 1;
}

/*
 * / # hcitool con
 * Connections:
 *      > ACL 64:A2:F9:68:1E:7E handle 1 state 1 lm SLAVE AUTH ENCRYPT
 *      > LE 60:9C:59:31:7F:B9 handle 16 state 1 lm SLAVE
 */
int rk_bt_is_connected(void)
{
	int ret;
	char buf[1024];

	memset(buf, 0, 1024);
	RK_shell_exec("hcitool con", buf, 1024);
	usleep(300000);

	if (strstr(buf, "ACL") || strstr(buf, "LE")) {
		ret = 1;
	} else {
		ret = 0;
	}

	return ret;
}

