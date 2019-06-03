#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#include <DeviceIo/DeviceIo.h>
#include <DeviceIo/Rk_wifi.h>
#include <DeviceIo/RK_log.h>
#include <DeviceIo/Rk_shell.h>
#include <DeviceIo/RkBtBase.h>
#include <DeviceIo/RkBle.h>
#include <DeviceIo/RkBtSource.h>
#include <DeviceIo/RkBtHfp.h>

#include "avrcpctrl.h"
#include "bluez_ctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/shell.h"
#include "spp_server/spp_server.h"
#include "bluez_alsa_client/ctl-client.h"
#include "bluez_alsa_client/rfcomm_msg.h"

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
extern RK_BT_SOURCE_CALLBACK g_btmaster_cb;
extern void *g_btmaster_userdata;
static pthread_t g_btmaster_thread;

static void _btmaster_send_event(RK_BT_SOURCE_EVENT event)
{
	if (g_btmaster_cb)
		(*g_btmaster_cb)(g_btmaster_userdata, event);
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
	ret = rk_bt_source_scan(&scan_param);
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
			(!strcmp(start->playrole, "Audio Sink"))) {
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
		printf("=== Cannot find audio Sink devices. ===\n");
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
	if (!a2dp_master_status(NULL, 0, NULL, 0))
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
	rk_bt_source_register_status_cb(userdata, cb);

	ret = rk_bt_source_open();
	if (ret < 0)
		return ret;

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

int rk_bt_source_open(void)
{
	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	if (g_btmaster_thread) {
		printf("The last operation is still in progress\n");
		return -1;
	}

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

	return 0;
}

int rk_bt_source_close(void)
{
	bt_close_source();
	a2dp_master_clear_cb();
	return 0;
}

int rk_bt_source_scan(BtScanParam *data)
{
	return a2dp_master_scan(data, sizeof(BtScanParam));
}

int rk_bt_source_connect(char *address)
{
	return a2dp_master_connect(address);
}

int rk_bt_source_disconnect(char *address)
{
	return a2dp_master_disconnect(address);
}

int rk_bt_source_remove(char *address)
{
	return a2dp_master_remove(address);
}

int rk_bt_source_register_status_cb(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
	a2dp_master_register_cb(userdata,  cb);
	return 0;
}

int rk_bt_source_get_device_name(char *name, int len)
{
	if (!name || (len <= 0))
		return -1;

	if (a2dp_master_status(NULL, 0,  name, len))
		return 0;

	return -1;
}

int rk_bt_source_get_device_addr(char *addr, int len)
{
	if (!addr || (len < 17))
		return -1;

	if (a2dp_master_status(addr, len, NULL, 0))
		return 0;

	return -1;
}

int rk_bt_source_get_status(RK_BT_SOURCE_STATUS *pstatus, char *name, int name_len,
                                    char *address, int addr_len)
{
	if (!pstatus)
		return 0;

	if (a2dp_master_status(address, addr_len, name, name_len))
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

int rk_bt_sink_register_volume_callback(RK_BT_SINK_VOLUME_CALLBACK cb)
{
	return;
}

int rk_bt_sink_open()
{
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

	return 0;
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

int rk_bt_sink_disconnect()
{
	disconn_device();
	return 0;
}

int rk_bt_sink_set_volume(int volume)
{
	return 0;
}

/*****************************************************************
 *            Rockchip bluetooth spp api                         *
 *****************************************************************/
int rk_bt_spp_open()
{
	int ret = 0;

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

	return 0;
}

int rk_bt_deinit()
{
#if 0
	char ret_buff[1024] = {0};
	int retry_cnt;
	int ret = 0;

	rk_bt_hfp_close();
	//rk_bt_sink_close();
	rk_bt_source_close();
	rk_bt_spp_close();
	rk_ble_stop();
	bt_close();

	//printf("bluez don't support bt deinit\n");
	RK_shell_system("killall bluealsa");
	RK_shell_system("killall bluealsa-aplay");
	RK_shell_system("killall bluetoothctl");
	RK_shell_system("killall bluetoothd");

	msleep(100);
	retry_cnt = 3;
	RK_shell_exec("pidof bluetoothd", ret_buff, 1024);
	while (ret_buff[0]) {
		msleep(10);
		RK_shell_system("killall bluetoothd");
		msleep(100);
		RK_shell_exec("pidof bluetoothd", ret_buff, 1024);
		if ((retry_cnt--) <= 0) {
			RK_LOGE("bluetoothd server can't be killed!");
			ret = -1;
		}
	}

	retry_cnt = 3;
	RK_shell_system("killall rtk_hciattach");
	msleep(800);
	RK_shell_exec("pidof rtk_hciattach", ret_buff, 1024);
	while (ret_buff[0]) {
		msleep(10);
		RK_shell_system("killall rtk_hciattach");
		msleep(800);
		RK_shell_exec("pidof rtk_hciattach", ret_buff, 1024);
		if ((retry_cnt--) <= 0) {
			RK_LOGE("rtk_hciattach can't be killed!");
			ret = -1;
		}
	}

	return ret;
#endif
	printf("bluez don't support bt deinit\n");
	return -1;
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

int rk_bt_set_class(int value)
{
	char cmd[100] = {0};

	printf("#%s value:0x%x\n", __func__, value);
	sprintf(cmd, "hciconfig hci0 class 0x%x", value);
	RK_shell_system(cmd);
	msleep(100);

	return 0;
}

int rk_bt_enable_reconnect(int value)
{
	int ret = 0;
	int fd = 0;

	fd = open("/userdata/cfg/bt_reconnect", O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		RK_LOGE("open /userdata/cfg/bt_reconnect failed!\n");
		return -1;
	}

	if (value)
		ret = write(fd, "bluez-reconnect:enable", strlen("bluez-reconnect:enable"));
	else
		ret = write(fd, "bluez-reconnect:disable", strlen("bluez-reconnect:disable"));

	close(fd);
	return (ret < 0) ? -1 : 0;
}

/*****************************************************************
 *            Rockchip bluetooth hfp-hf api                        *
 *****************************************************************/

static int g_ba_hfp_client;
RK_BT_HFP_CALLBACK g_hfp_cb;

void rk_bt_hfp_register_callback(RK_BT_HFP_CALLBACK cb)
{
	g_hfp_cb = cb;
	rfcomm_hfp_hf_regist_cb(cb);
}

int rk_bt_hfp_open(void)
{
	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	if (bt_source_is_open()) {
		RK_LOGE("bt hfp isn't coexist with source!!!\n");
		bt_close_source();
	}

	if (bt_interface(BtControl::BT_HFP_OPEN, NULL) < 0) {
		bt_control.is_hfp_open = 0;
		bt_control.type = BtControlType::BT_NONE;
		return -1;
	}

	g_ba_hfp_client = bluealsa_open("hci0");
	if (g_ba_hfp_client < 0) {
		RK_LOGE("bt hfp connect to bluealsa server failed!");
		return -1;
	}

	rfcomm_listen_ba_msg_start();

	bt_control.is_hfp_open = 1;
	/* Set bluetooth control current type */
	bt_control.type = BtControlType::BT_HFP_HF;
	bt_control.last_type = BtControlType::BT_HFP_HF;

	reconn_last();

	return 0;
}

int rk_bt_hfp_sink_open(void)
{
	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	/* Already in sink mode or hfp mode? */
	if (bt_sink_is_open() || bt_hfp_is_open()) {
		RK_LOGE("\"rk_bt_sink_open\" or \"rk_bt_hfp_open\" is called before calling this interface."
			"This situation is not allowed.");
		return -1;
	}

	if (bt_source_is_open()) {
		RK_LOGE("bt sink isn't coexist with source!!!\n");
		bt_close_source();
	}

	if (bt_interface(BtControl::BT_HFP_SINK_OPEN, NULL) < 0) {
		bt_control.is_a2dp_sink_open = 0;
		bt_control.is_hfp_open = 0;
		bt_control.type = BtControlType::BT_NONE;
		return -1;
	}

	g_ba_hfp_client = bluealsa_open("hci0");
	if (g_ba_hfp_client < 0) {
		RK_LOGE("bt hfp connect to bluealsa server failed!");
		return -1;
	}

	rfcomm_listen_ba_msg_start();

	bt_control.is_a2dp_sink_open = 1;
	bt_control.is_hfp_open = 1;
	/* Set bluetooth control current type */
	bt_control.type = BtControlType::BT_SINK_HFP_MODE;
	bt_control.last_type = BtControlType::BT_SINK_HFP_MODE;

	return 0;
}

int rk_bt_hfp_close(void)
{
	if (!bt_hfp_is_open())
		return 0;

	if (g_ba_hfp_client >= 0) {
		close(g_ba_hfp_client);
		g_ba_hfp_client = 0;
	}

	bt_control.is_hfp_open = 0;
	rfcomm_listen_ba_msg_stop();

	if (bt_control.type == BtControlType::BT_HFP_HF) {
		bt_control.type = BtControlType::BT_NONE;
		bt_control.last_type = BtControlType::BT_NONE;
	}

	if (bt_sink_is_open()) {
		bt_control.type = BtControlType::BT_SINK;
		bt_control.last_type = BtControlType::BT_SINK;
		return 0;
	}

	system("killall bluealsa-aplay");
	system("killall bluealsa");

	return 0;
}

static char *build_rfcomm_command(const char *cmd)
{

	static char command[512];
	bool at;

	command[0] = '\0';
	if (!(at = strncmp(cmd, "AT", 2) == 0))
		strcpy(command, "\r\n");

	strcat(command, cmd);
	strcat(command, "\r");
	if (!at)
		strcat(command, "\n");

	return command;
}

static int rk_bt_hfp_hp_send_cmd(char *cmd)
{
	char dev_addr[18] = {0};
	char result_buf[256] = {0};
	int ret = 0;
	bdaddr_t ba_addr;
	char *start = NULL;

	if (g_ba_hfp_client <= 0) {
		RK_LOGE("%s ba hfp client is not valid!", __func__);
		return -1;
	}

	RK_shell_exec("hcitool con", result_buf, sizeof(result_buf));
	if (start = strstr(result_buf, "ACL ")) {
		start += 4; /* skip space */
		memcpy(dev_addr, start, 17);
	}

	RK_LOGD("%s send cmd:%s to addr:%s", __func__, cmd, dev_addr);
	ret = str2ba(dev_addr, &ba_addr);
	if (ret) {
		RK_LOGE("%s no valid hfp connection!", __func__);
		return -1;
	}

	ret = bluealsa_send_rfcomm_command(g_ba_hfp_client, ba_addr, build_rfcomm_command(cmd));
	if (ret)
		RK_LOGE("%s ba hfp client cmd:/'%s/' failed!", __func__, cmd);

	return ret;
}

int rk_bt_hfp_pickup(void)
{
	int ret = 0;

	ret = rk_bt_hfp_hp_send_cmd("ATA");
	if (ret)
		return ret;

	if (g_hfp_cb)
		g_hfp_cb(RK_BT_HFP_PICKUP_EVT, NULL);

	return rfcomm_hfp_open_audio_path();
}

int rk_bt_hfp_hangup(void)
{
	int ret = 0;

	ret = rfcomm_hfp_close_audio_path();
	if (ret)
		return ret;

	if (g_hfp_cb)
		g_hfp_cb(RK_BT_HFP_HANGUP_EVT, NULL);

	return rk_bt_hfp_hp_send_cmd("AT+CHUP");
}

int rk_bt_hfp_redial(void)
{
	return rk_bt_hfp_hp_send_cmd("AT+BLDN");
}

int rk_bt_hfp_report_battery(int value)
{
	int ret = 0;
	char at_cmd[100] = {0};
	static int done = 0;

	if ((value < 0) || (value > 9)) {
		printf("ERROR: Invalid value, should within [0, 9]\n");
		return -1;
	}

	if (done == 0) {
		ret = rk_bt_hfp_hp_send_cmd("AT+XAPL=ABCD-1234-0100,2");
		done = 1;
	}

	if (ret == 0) {
		sprintf(at_cmd, "AT+IPHONEACCEV=1,1,%d", value);
		ret =  rk_bt_hfp_hp_send_cmd(at_cmd);
	}

	return ret;
}

int rk_bt_hfp_set_volume(int volume)
{
	int ret = 0;
	char at_cmd[100] = {0};

	if (volume > 15)
		volume = 15;
	else if (volume < 0)
		volume = 0;

	sprintf(at_cmd, "AT+VGS=%d", volume);
	ret =  rk_bt_hfp_hp_send_cmd(at_cmd);

	return ret;
}

