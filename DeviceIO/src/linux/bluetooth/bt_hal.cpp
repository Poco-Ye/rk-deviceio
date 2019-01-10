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

#include "../Logger.h"
#include "../shell.h"

#include "DeviceIo/DeviceIo.h"
#include <DeviceIo/bt_hal.h>
#include <DeviceIo/Rk_wifi.h>

#include "avrcpctrl.h"
#include "bluez_ctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "a2dp_source/shell.h"
#include "spp_server/spp_server.h"

extern Bt_Content_t GBt_Content;
extern volatile bt_control_t bt_control;

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::wifi_config;

#define BT_CONFIG_FAILED 2
#define BT_CONFIG_OK 1

static pthread_t wificonfig_tid = 0;
static pthread_t wificonfig_scan_tid = 0;

static char wifi_ssid[256];
static char wifi_ssid_bk[256];
static char wifi_password[256];
static char wifi_password_bk[256];
static char wifi_security[256];
static char wifi_hide[256];
static char check_data[256];
static int priority = 0;
static struct wifi_config wifi_cfg;

#define HOSTNAME_MAX_LEN	250	/* 255 - 3 (FQDN) - 2 (DNS enc) */

RK_blewifi_state_callback ble_status_callback = NULL;
RK_ble_recv_data ble_recv_data = NULL;
RK_ble_audio_state_callback ble_audio_status_callback = NULL;
RK_ble_audio_recv_data ble_audio_recv_data = NULL;

RK_BLEWIFI_State_e gstate;
RK_BLE_State_e g_ble_audio_status;

/*
"{\"method\":\"ble\", \"magic\":\"KugouMusic\", \"params\":
	{\"wifilist\":
		[
		 {\"ssid\":\"aaa\",\"rssi\":-48},
		 {\"ssid\":\"bbbbbbb\",\"rssi\":-32},
		 {\"ssid\":\"ccccc\",\"rssi\":-39},
		 {\"ssid\":\"ddddd\",\"rssi\":-41}
		]
	}
 }"
*/
const char* MSG_BLE_WIFI_LIST_FORMAT = "{\"method\":\"ble\", \"magic\":\"KugouMusic\", \"params\":{\"wifilist\":%s}}";

#define BLE_UUID_SERVICE	"0000180A-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_WIFI_CHAR	"00009999-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_AUDIO_CHAR	"00006666-0000-1000-8000-00805F9B34FB"

static char kg_wifi_list_buf[20 * 1024];
static int scanr_len = 0, scanr_len_use = 0;
#define BLE_SEND_MAX_LEN (134) //(20) //(512)
#define UUID_MAX_LEN 36

typedef struct  {
	unsigned char start;    // 段， 1Byte， 固定为 0x01
	char magic[10]; // 段， 10Byte， 固定为"KugouMusic"
	char cmd[9];    // 段， 9Byte， 固定为"wifisetup"
	char ssid[32];  // 段， 32Byte， 需要连接的 WiFi ssid， 长度不足 32 的部分填充 0
	char psk[32];   // 段， 32Byte， 需要连接的 wifi password， 长度不足 32 的部分填充 0
	unsigned char datalen[4]; //段，4Byte，表示后面自定义数据长度值（按小端：例如长度 10，使用 0x0a,0x00,0x00,0x00）
	//unsigned char data[]段，datalen Byte，表示自定义数据
	//end 段， 1Byte， 固定位 0x04
} kugou_ble;

/******************************************/
/***************** BLE ********************/
/******************************************/
void rk_ble_request_data(char *uuid);

static int rk_blewifi_state_callback(RK_WIFI_RUNNING_State_e state)
{
	rk_ble_config kg_ble_cfg;

	printf("%s state: %d\n", __func__, state);
	if (state == RK_WIFI_State_CONNECTED) {
		if (ble_status_callback)
			ble_status_callback(RK_BLEWIFI_State_SUCCESS);
		gstate = RK_BLEWIFI_State_SUCCESS;
		kg_ble_cfg.data[0] = 0x1;
	} else if (state == RK_WIFI_State_CONNECTFAILED) {
		if (ble_status_callback)
			ble_status_callback(RK_BLEWIFI_State_FAIL);
		gstate = RK_BLEWIFI_State_FAIL;
		kg_ble_cfg.data[0] = 0x2;
	}

	kg_ble_cfg.len = 0x1;
	strcpy(kg_ble_cfg.uuid, BLE_UUID_WIFI_CHAR);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &kg_ble_cfg);

}

void *rk_config_wifi_thread(void)
{
	printf("config_wifi_thread\n");
	if (ble_status_callback)
		ble_status_callback(RK_BLEWIFI_State_CONNECTTING);

	gstate = RK_BLEWIFI_State_CONNECTTING;
	printf("controlWifi connect ...\n");
	//DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_CONNECT, &wifi_cfg);

	RK_wifi_ble_register_callback(rk_blewifi_state_callback);
	RK_wifi_connect(wifi_cfg.ssid, wifi_cfg.psk);
}

static void wifi_status_callback(int status, int reason)
{
	rk_ble_config kg_ble_cfg;

	printf("%s status: %d\n", __func__, status);
	if (status == 2) {
		if (ble_status_callback)
			ble_status_callback(RK_BLEWIFI_State_SUCCESS);
		gstate = RK_BLEWIFI_State_SUCCESS;
		kg_ble_cfg.data[0] = 0x1;
	} else if (status == 3) {
		if (ble_status_callback)
			ble_status_callback(RK_BLEWIFI_State_FAIL);
		gstate = RK_BLEWIFI_State_FAIL;
		kg_ble_cfg.data[0] = 0x2;
	}

	kg_ble_cfg.len = 0x1;
	strcpy(kg_ble_cfg.uuid, BLE_UUID_WIFI_CHAR);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &kg_ble_cfg);
}

void rk_ble_send_data(void)
{
	rk_ble_config kg_ble_cfg;

	if (scanr_len == 0) {
		scanr_len_use = 0;
		printf("[KG] NO WIFI SCAN_R OR READ END!!!\n");
		return;
	}

	while (scanr_len) {
		printf("%s: wifi use: %d, remain len: %d\n", __func__, scanr_len_use, scanr_len);
		kg_ble_cfg.len = (scanr_len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : scanr_len;
		memset(kg_ble_cfg.data, 0, BLE_SEND_MAX_LEN);
		memcpy(kg_ble_cfg.data, kg_wifi_list_buf + scanr_len_use, kg_ble_cfg.len);
		strcpy(kg_ble_cfg.uuid, BLE_UUID_WIFI_CHAR);
		usleep(100000);
		DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &kg_ble_cfg);
		scanr_len -= kg_ble_cfg.len;
		scanr_len_use += kg_ble_cfg.len;
	}
}

void ble_callback(char *uuid, unsigned char *data, int len)
{
	unsigned char str[512];
	memset(str, 0, 512);

	memcpy(str, data, len);
	str[len] = '\0';
	printf("chr_write_value: %p, %d, data: \n", data, len);

	for (int i = 0; i < len; i++) {
		   if (!( i % 8))
				   printf("\n");
		   printf("0x%02x ", str[i]);
	}
	printf("\n");

	if (!strcmp(BLE_UUID_AUDIO_CHAR, uuid)) {
		if (ble_audio_recv_data)
			ble_audio_recv_data(BLE_UUID_AUDIO_CHAR, str, len);
	}

	if (!strcmp(BLE_UUID_WIFI_CHAR, uuid)) {
		kugou_ble kg_ble;
		char *wifilist;

		memcpy(&kg_ble, str, len);
		printf("kg_ble.cmd: %s, kg_ble.start: %d [%d]\n", kg_ble.cmd, kg_ble.start, strncmp(kg_ble.cmd, "wifilists", 9));
		if (kg_ble.start != 0x1)
				printf("BLE RECV DATA ERROR !!!\n");

		if (strncmp(kg_ble.cmd, "wifilists", 9) == 0) {

scan_retry:
			printf("RK_wifi_scan ...\n");
			RK_wifi_scan();
			usleep(800000);

			printf("RK_wifi_scan_r_sec ...\n");
			wifilist = RK_wifi_scan_r_sec(0x14);
			printf("RK_wifi_scan_r_sec end wifilist: %p\n", wifilist);

			if (wifilist == NULL)
				goto scan_retry;
			if (wifilist && (strlen(wifilist) < 3)) {
				free(wifilist);
				goto scan_retry;
			}

			memset(kg_wifi_list_buf, 0, sizeof(kg_wifi_list_buf));
			snprintf(kg_wifi_list_buf, sizeof(kg_wifi_list_buf), MSG_BLE_WIFI_LIST_FORMAT, wifilist);
			scanr_len = strlen(kg_wifi_list_buf);
			scanr_len_use = 0;
			printf("[KG] wifi scan_r: %s, len: %d\n", kg_wifi_list_buf, scanr_len);

			free(wifilist);
			pthread_create(&wificonfig_scan_tid, NULL, rk_ble_send_data, NULL);
		} else if (strncmp(kg_ble.cmd, "wifisetup", 9) == 0) {
			strcpy(wifi_ssid, kg_ble.ssid); // str + 20);
			strcpy(wifi_password, kg_ble.psk); // str + 52);
			printf("wifi ssid is %s\n", wifi_ssid);
			printf("wifi psk is %s\n", wifi_password);

			strcpy(wifi_cfg.ssid, wifi_ssid);
			strcpy(wifi_cfg.psk, wifi_password);
			wifi_cfg.wifi_status_callback = wifi_status_callback;
			pthread_create(&wificonfig_tid, NULL, rk_config_wifi_thread, NULL);
		}
	}
}

void rk_ble_request_data(char *uuid)
{
	rk_ble_config kg_ble_cfg;
	printf("%s: wifi use: %d, remain len: %d\n", __func__, scanr_len_use, scanr_len);

	if (scanr_len == 0) {
		printf("[KG] NO WIFI SCAN_R OR READ END!!!\n");
		return;
	}

	kg_ble_cfg.len = (scanr_len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : scanr_len;
	memset(kg_ble_cfg.data, 0, BLE_SEND_MAX_LEN);
	memcpy(kg_ble_cfg.data, kg_wifi_list_buf + scanr_len_use, kg_ble_cfg.len);
	memcpy(kg_ble_cfg.uuid, BLE_UUID_WIFI_CHAR, UUID_MAX_LEN);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &kg_ble_cfg);
	usleep(100000);
	scanr_len -= kg_ble_cfg.len;
	scanr_len_use += kg_ble_cfg.len;
}

int RK_blewifi_start(char *name)
{
	//DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN);
	RK_wifi_enable(1);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_OPEN);
	if (ble_status_callback)
		ble_status_callback(RK_BLEWIFI_State_IDLE);
	gstate = RK_BLEWIFI_State_IDLE;

	return 1;
}

int RK_bleaudio_start(char *name)
{
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_OPEN);
	ble_audio_status_callback(RK_BLE_State_IDLE);
	g_ble_audio_status = RK_BLE_State_IDLE;

	return 1;
}

int RK_blewifi_register_callback(RK_blewifi_state_callback cb)
{
	ble_status_callback = cb;
	return 1;
}

int RK_ble_recv_data_callback(RK_ble_recv_data cb)
{
	ble_recv_data = cb;
	return 1;
}

int RK_ble_audio_register_callback(RK_ble_audio_state_callback cb)
{
	ble_audio_status_callback = cb;
	return 1;
}

int RK_ble_audio_recv_data_callback(RK_ble_audio_recv_data cb)
{
	ble_audio_recv_data = cb;
	return 1;
}

int RK_blewifi_stop(void)
{
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_COLSE);
	return 1;
}

int RK_blewifi_getState(RK_BLEWIFI_State_e *pState)
{
	if (pState)
		*pState = gstate;

	return 1;
}

int RK_bleaudio_getState(RK_BLE_State_e *pState)
{
	if (pState)
		*pState = g_ble_audio_status;

	return 1;
}

int RK_blewifi_get_exdata(char *buffer, int *length)
{
	return 1;
}

#define BLE_SEND_MAX_LEN (134) //(20) //(512)
int RK_ble_write(const char *uuid, unsigned char *data, int len)
{
	rk_ble_config ble_cfg;

	ble_cfg.len = (len > BLE_SEND_MAX_LEN) ? BLE_SEND_MAX_LEN : len;
	memcpy(ble_cfg.data, data, ble_cfg.len);
	strcpy(ble_cfg.uuid, uuid);
	DeviceIo::getInstance()->controlBt(BtControl::BT_BLE_WRITE, &ble_cfg);
}

/*****************************************************************
 *            Rockchip bluetooth master api                      *
 *****************************************************************/
static RK_btmaster_callback g_btmaster_user_cb;
static void *g_btmaster_user_data;
static pthread_t g_btmaster_thread;

static void _btmaster_send_event(RK_BtMasterEvent_e event)
{
	if (g_btmaster_user_cb)
		(*g_btmaster_user_cb)(g_btmaster_user_data, event);
}

static void* _btmaster_autoscan_and_connect(void *data)
{
	BtScanParam scan_param;
	BtDeviceInfo *start, *tmp;
	int max_rssi = -100;
	int ret = 0;
	char target_address[17] = {0};
	bool target_vaild = false;
	int scan_cnt;

	/* Scan bluetooth devices */
	scan_param.mseconds = 10000; /* 10s for default */
	scan_param.item_cnt = 100;
	scan_param.device_list = NULL;
	scan_cnt = 3;

scan_retry:
	printf("=== BT_SOURCE_SCAN ===\n");
	ret = a2dp_master_scan((void *)&scan_param, sizeof(scan_param));
	if (ret && (scan_cnt--)) {
		sleep(1);
		goto scan_retry;
	} else if (ret) {
		printf("ERROR: Scan error!\n");
		_btmaster_send_event(RK_BtMasterEvent_Connect_Failed);
		g_btmaster_thread = 0;
		return NULL;
	}

	/*
	 * Find the audioSink device from the device list,
	 * which has the largest rssi value.
	 */
	max_rssi = -100;
	tmp = NULL;
	start = scan_param.device_list;
	while (start) {
		if (start->rssi_valid && (start->rssi > max_rssi) &&
			(!strcmp(start->playrole, "AudioSink"))) {
			printf("Name:%s\n", start->name);
			printf("\tAddress:%s\n", start->address);
			printf("\tRSSI:%d\n", start->rssi);
			printf("\tPlayrole:%s\n", start->playrole);
			max_rssi = start->rssi;

			memcpy(target_address, start->address, 17);
			target_vaild = true;
		}
		tmp = start;
		start = start->next;
		/* Free DeviceInfo */
		free(tmp);
	}

	if (!target_vaild) {
		printf("=== Cannot find audioSink devices. ===\n");
		_btmaster_send_event(RK_BtMasterEvent_Connect_Failed);
		g_btmaster_thread = 0;
		return;
	} else if (max_rssi < -80) {
		printf("=== BT SOURCE RSSI is is too weak !!! ===\n");
		_btmaster_send_event(RK_BtMasterEvent_Connect_Failed);
		g_btmaster_thread = 0;
		return NULL;
	}
	/* Connect target device */
	if (!a2dp_master_status(NULL, NULL))
		a2dp_master_connect(target_address);

	g_btmaster_thread = 0;
	return NULL;
}
/*
 * Turn on Bluetooth and scan SINK devices.
 * Features:
 *     1. turn on Bluetooth
 *     2. enter the master mode
 *     3. Scan surrounding SINK type devices
 *     4. If the SINK device is found, the device with the strongest
 *        signal is automatically connected.
 * Return:
 *     0: The function is executed successfully and needs to listen
 *        for Bluetooth connection events.
 *    -1: Function execution failed.
 */
int RK_btmaster_connect_start(void *userdata, RK_btmaster_callback cb)
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
			APP_ERROR("bt sink isn't coexist with source!!!\n");
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

int RK_btmaster_stop(void)
{
	bt_close_source();
	a2dp_master_clear_cb();
	return 0;
}

int RK_btmaster_getDeviceName(char *name, int len)
{
	if (!name || (len <= 0))
		return -1;

	if (a2dp_master_status(NULL, name))
		return 0;

	return -1;
}

int RK_btmaster_getDeviceAddr(char *addr, int len)
{
	if (!addr || (len < 17))
		return -1;

	if (a2dp_master_status(addr, NULL))
		return 0;

	return -1;
}

int RK_btmaster_getStatus(RK_BtMasterStatus *pstatus)
{
	if (!pstatus)
		return 0;

	if (a2dp_master_status(NULL, NULL))
		*pstatus = RK_BtMasterStatus_Connected;
	else
		*pstatus = RK_BtMasterStatus_Disconnected;

	return 0;
}

/*****************************************************************
 *            Rockchip bluetooth sink api                        *
 *****************************************************************/
int RK_bta2dp_register_callback(RK_bta2dp_callback cb)
{
	a2dp_sink_register_cb(cb);
	return 0;
}

int RK_bta2dp_open(char* name)
{
	char set_hostname_cmd[HOSTNAME_MAX_LEN + 64] = {'\0'};

	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	if (name && GBt_Content.bt_name && strcmp(GBt_Content.bt_name, name)) {
		/* Set bluetooth device name */
		sprintf(set_hostname_cmd, "hciconfig hci0 name \'%s\'", name);
		Shell::system(set_hostname_cmd);
		msleep(10);
		/* Restart the device to make the new name take effect */
		Shell::system("hciconfig hci0 down");
		msleep(10);
		Shell::system("hciconfig hci0 up");
	}

	/* Already in sink mode? */
	if (bt_sink_is_open())
		return 0;

	if (bt_source_is_open()) {
		APP_ERROR("bt sink isn't coexist with source!!!\n");
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

int RK_bta2dp_setVisibility(const int visiable, const int connectal)
{
	Shell::system("hciconfig hci0 noscan");
	usleep(2000);//2ms
	if (visiable)
		Shell::system("hciconfig hci0 iscan");
	if (connectal)
		Shell::system("hciconfig hci0 pscan");

	return 0;
}

int RK_bta2dp_close(void)
{
	a2dp_sink_clear_cb();
	bt_close_sink();

	return 1;
}

int RK_bta2dp_getState(RK_BTA2DP_State_e *pState)
{
	return a2dp_sink_status(pState);
}

int RK_bta2dp_play(void)
{
	if (bt_control_cmd_send(BtControl::BT_RESUME_PLAY) < 0)
		return -1;

	return 0;
}

int RK_bta2dp_pause(void)
{
	if (bt_control_cmd_send(BtControl::BT_PAUSE_PLAY) < 0)
		return -1;

	return 0;
}

int RK_bta2dp_prev(void)
{
	if (bt_control_cmd_send(BtControl::BT_AVRCP_FWD) < 0)
		return -1;

	return 0;
}

int RK_bta2dp_next(void)
{
	if (bt_control_cmd_send(BtControl::BT_AVRCP_BWD) < 0)
		return -1;

	return 0;
}

int RK_bta2dp_stop(void)
{
	if (bt_control_cmd_send(BtControl::BT_AVRCP_STOP) < 0)
		return -1;

	return 0;
}

int RK_bta2dp_set_auto_reconnect(int enable)
{
	a2dp_sink_set_auto_reconnect(enable);
	return 0;
}

int RK_bta2dp_disconnect()
{
	disconn_device();
	return 0;
}

/*****************************************************************
 *            Rockchip bluetooth spp api                         *
 *****************************************************************/
int RK_btspp_open(RK_btspp_callback cb)
{
	int ret = 0;

	ret = RK_bta2dp_open(NULL);
	if (ret)
		return ret;

	ret = bt_spp_server_open(cb);
	return ret;
}

int RK_btspp_close(void)
{
	bt_spp_server_close();
	RK_bta2dp_close();
}

int RK_btspp_getState(RK_BTSPP_State *pState)
{
	if (pState)
		*pState = bt_spp_get_status();

	return 0;
}

int RK_btspp_write(char *data, int len)
{
	return bt_spp_write(data, len);
}

//====================================================//
int RK_bt_init(Bt_Content_t *p_bt_content)
{
	if (p_bt_content->ble_content.server_uuid) {
		p_bt_content->ble_content.cb_ble_recv_fun = ble_callback;
		p_bt_content->ble_content.cb_ble_request_data = rk_ble_request_data;
	}

	DeviceIo::getInstance()->controlBt(BtControl::BT_OPEN, p_bt_content);
	sleep(1);

	return 1;
}
