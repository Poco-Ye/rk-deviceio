/*****************************************************************************
 **
 **  Name:           bluetooth_bsa.c
 **
 **  Description:    Bluetooth API
 **
 **  Copyright (c) 2019, Rockchip Corp., All Rights Reserved.
 **  Rockchip Bluetooth Core. Proprietary and confidential.
 **
 *****************************************************************************/
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include "bsa_api.h"
#include "app_xml_utils.h"
#include "app_dm.h"
#include "app_mgt.h"
#include "app_utils.h"
#include "app_manager.h"
#include "app_avk.h"
#include "app_av.h"
#include "app_dg.h"
#include "app_ble_wifi_introducer.h"
#include "app_hs.h"
#include "bluetooth_bsa.h"

enum class BtControlType {
	BT_NONE = 0,
	BT_SINK,
	BT_SOURCE,
	BT_BLE_MODE,
	BLE_SINK_BLE_MODE,
	BLE_WIFI_INTRODUCER
};

typedef struct {
	bool is_bt_open;
	bool is_ble_open;
	bool is_a2dp_sink_open;
	bool is_a2dp_source_open;
    bool is_spp_open;
    bool is_handsfree_open;
} bt_control_t;

volatile bt_control_t g_bt_control = {
	false,
	false,
	false,
	false,
	false,
	false,
};

static bool bt_is_open();
static bool ble_is_open();
static bool a2dp_sink_is_open();
static bool a2dp_source_is_open();
static bool spp_is_open();
static bool handsfree_is_open();

static void bt_print_cmd(DeviceIOFramework::BtControl cmd)
{
    switch (cmd) {
        case DeviceIOFramework::BtControl::BT_OPEN:
            APP_DEBUG0("bluetooth receive command: BT_OPEN");
            break;
        case DeviceIOFramework::BtControl::BT_CLOSE:
            APP_DEBUG0("bluetooth receive command: BT_CLOSE");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_OPEN:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_OPEN");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_SCAN:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_SCAN");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_CONNECT:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_CONNECT");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_DISCONNECT:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_DISCONNECT");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_STATUS:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_STATUS");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_REMOVE:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_REMOVE");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_CLOSE:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_CLOSE");
            break;
        case DeviceIOFramework::BtControl::BT_SOURCE_IS_OPENED:
            APP_DEBUG0("bluetooth receive command: BT_SOURCE_IS_OPENED");
            break;
        case DeviceIOFramework::BtControl::BT_SINK_OPEN:
            APP_DEBUG0("bluetooth receive command: BT_SINK_OPEN");
            break;
        case DeviceIOFramework::BtControl::BT_SINK_CLOSE:
            APP_DEBUG0("bluetooth receive command: BT_SINK_CLOSE");
            break;
        case DeviceIOFramework::BtControl::BT_SINK_RECONNECT:
            APP_DEBUG0("bluetooth receive command: BT_SINK_RECONNECT");
            break;
        case DeviceIOFramework::BtControl::BT_SINK_IS_OPENED:
            APP_DEBUG0("bluetooth receive command: BT_SINK_IS_OPENED");
            break;
        case DeviceIOFramework::BtControl::BT_IS_CONNECTED:
            APP_DEBUG0("bluetooth receive command: BT_IS_CONNECTED");
            break;
        case DeviceIOFramework::BtControl::BT_UNPAIR:
            APP_DEBUG0("bluetooth receive command: BT_UNPAIR");
            break;
        case DeviceIOFramework::BtControl::BT_PLAY:
            APP_DEBUG0("bluetooth receive command: BT_PLAY");
            break;
        case DeviceIOFramework::BtControl::BT_PAUSE_PLAY:
            APP_DEBUG0("bluetooth receive command: BT_PAUSE_PLAY");
            break;
        case DeviceIOFramework::BtControl::BT_RESUME_PLAY:
            APP_DEBUG0("bluetooth receive command: BT_RESUME_PLAY");
            break;
        case DeviceIOFramework::BtControl::BT_VOLUME_UP:
            APP_DEBUG0("bluetooth receive command: BT_VOLUME_UP");
            break;
        case DeviceIOFramework::BtControl::BT_VOLUME_DOWN:
            APP_DEBUG0("bluetooth receive command: BT_VOLUME_DOWN");
            break;
        case DeviceIOFramework::BtControl::BT_AVRCP_FWD:
            APP_DEBUG0("bluetooth receive command: BT_AVRCP_FWD");
            break;
        case DeviceIOFramework::BtControl::BT_AVRCP_BWD:
            APP_DEBUG0("bluetooth receive command: BT_AVRCP_BWD");
            break;
        case DeviceIOFramework::BtControl::BT_AVRCP_STOP:
            APP_DEBUG0("bluetooth receive command: BT_AVRCP_STOP");
            break;
        case DeviceIOFramework::BtControl::BT_HFP_RECORD:
            APP_DEBUG0("bluetooth receive command: BT_HFP_RECORD");
            break;
        case DeviceIOFramework::BtControl::BT_BLE_OPEN:
            APP_DEBUG0("bluetooth receive command: BT_BLE_OPEN");
            break;
        case DeviceIOFramework::BtControl::BT_BLE_COLSE:
            APP_DEBUG0("bluetooth receive command: BT_BLE_COLSE");
            break;
        case DeviceIOFramework::BtControl::BT_BLE_IS_OPENED:
            APP_DEBUG0("bluetooth receive command: BT_BLE_IS_OPENED");
            break;
        case DeviceIOFramework::BtControl::BT_BLE_WRITE:
            APP_DEBUG0("bluetooth receive command: BT_BLE_WRITE");
            break;
        case DeviceIOFramework::BtControl::BT_BLE_READ:
            APP_DEBUG0("bluetooth receive command: BT_BLE_READ");
            break;
        case DeviceIOFramework::BtControl::BT_VISIBILITY:
            APP_DEBUG0("bluetooth receive command: BT_VISIBILITY");
            break;
        case DeviceIOFramework::BtControl::GET_BT_MAC:
            APP_DEBUG0("bluetooth receive command: GET_BT_MAC");
            break;
        default:
            APP_DEBUG0("bluetooth receive command: unknown");
            break;
    }
}

static void bt_mgr_notify_callback(tBSA_MGR_EVT evt)
{
	switch(evt) {
		case BT_LINK_UP_EVT:
			APP_DEBUG0("BT_LINK_UP_EVT\n");
			break;
		case BT_LINK_DOWN_EVT:
			APP_DEBUG0("BT_LINK_DOWN_EVT\n");
			break;
		case BT_WAIT_PAIR_EVT:
			APP_DEBUG0("BT_WAIT_PAIR_EVT\n");
			break;
		case BT_PAIR_SUCCESS_EVT:
			APP_DEBUG0("BT_PAIR_SUCCESS_EVT\n");
			break;
		case BT_PAIR_FAILED_EVT:
			APP_DEBUG0("BT_PAIR_FAILED_EVT\n");
			break;
	}
}

static int get_ps_pid(const char Name[])
{
	int len, pid = 0;
	char name[20] = {0};
	char cmdresult[256] = {0};
	char cmd[20] = {0};
	FILE *pFile = NULL;

	len = strlen(Name);
	strncpy(name,Name,len);
	name[len] ='\0';

	sprintf(cmd, "pidof %s", name);
	pFile = popen(cmd, "r");
	if (pFile != NULL)  {
		while (fgets(cmdresult, sizeof(cmdresult), pFile)) {
			pid = atoi(cmdresult);
			break;
		}
	}
	pclose(pFile);
	return pid;
}

typedef void (*sighandler_t)(int);
static int rk_system(const char *cmd_line)
{
   int ret = 0;
   sighandler_t old_handler;

   old_handler = signal(SIGCHLD, SIG_DFL);
   ret = system(cmd_line);
   signal(SIGCHLD, old_handler);

   return ret;
}

static void check_bsa_server()
{
    while(1) {
        if (get_ps_pid("bsa_server")) {
            APP_DEBUG0("bsa_server has been opened.");
            break;
        }

        sleep(1);
        APP_DEBUG0("wait bsa_server open.");
    }
    return;
}

static bool bt_is_open()
{
    return g_bt_control.is_bt_open;
}

static int bt_bsa_server_open()
{
    if(0 != rk_system("/usr/bin/bsa_server.sh start &")) {
        APP_DEBUG1("Start bsa_server failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int bt_bsa_server_close()
{
    if(0 != rk_system("/usr/bin/bsa_server.sh stop &")) {
        APP_DEBUG1("Stop bsa_server failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

// add default interface to compile dui. keep untill bluze remove the interface
int gatt_open(void)
{
	return 0;
}

int RK_bt_is_connected(void)
{
	return 0;
}

int RK_bt_init(Bt_Content_t *p_bt_content)
{
    RK_bt_open("KUGOU W2 ");
	return 0;
}

void RK_ble_test(void *data)
{

}

int RK_bt_open(const char *bt_name)
{
    if (bt_is_open()) {
        APP_DEBUG0("bluetooth has been opened.");
        return 0;
    }

    /* start bsa_server */
    if(bt_bsa_server_open() < 0) {
        APP_DEBUG0("bsa server open failed.");
        return -1;
    }

    check_bsa_server();

    /* Init App manager */
    if(app_manager_init(bt_name, bt_mgr_notify_callback) < 0) {
        APP_DEBUG0("app_manager init failed.");
        return -1;
    }

    g_bt_control.is_bt_open = true;
    return 0;
}

void RK_bt_close() {
    if (!bt_is_open()) {
        APP_DEBUG0("bluetooth has been closed.");
        return;
    }

    /* Close BSA before exiting (to release resources) */
    app_mgt_close();

    /* stop bsa_server */
    bt_bsa_server_close();

    g_bt_control.is_bt_open = false;
}

/******************************************/
/*               A2DP SINK                */
/******************************************/
static int a2dp_sink_notify_callback(RK_BTA2DP_State_e state)
{
	switch(state) {
		case RK_BTA2DP_State_IDLE:
			APP_DEBUG0("++++++++++++ BT SINK EVENT: idle ++++++++++");
			break;
		case RK_BTA2DP_State_CONNECT:
			APP_DEBUG0("++++++++++++ BT SINK EVENT: connect sucess ++++++++++");
			break;
		case RK_BTA2DP_State_PLAY:
			APP_DEBUG0("++++++++++++ BT SINK EVENT: playing ++++++++++");
			break;
		case RK_BTA2DP_State_PAUSE:
			APP_DEBUG0("++++++++++++ BT SINK EVENT: paused ++++++++++");
			break;
		case RK_BTA2DP_State_STOP:
			APP_DEBUG0("++++++++++++ BT SINK EVENT: stoped ++++++++++");
			break;
		case RK_BTA2DP_State_DISCONNECT:
			APP_DEBUG0("++++++++++++ BT SINK EVENT: disconnected ++++++++++");
			break;
	}

    return 0;
}

static bool a2dp_sink_is_open()
{
    return g_bt_control.is_a2dp_sink_open;
}

int RK_bta2dp_register_callback(RK_bta2dp_callback cb)
{
    app_avk_register_cb(a2dp_sink_notify_callback);
    return 0;
}

int RK_bta2dp_open(char* name)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened, close a2dp source");
        RK_btmaster_stop();
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened.");
        return 0;
    }

    if (app_avk_start() < 0) {
        APP_DEBUG0("app_avk_start failed");
        return -1;
    }

    //RK_bta2dp_setVisibility(1, 1);
    g_bt_control.is_a2dp_sink_open = true;
    return 0 ;
}

int RK_bta2dp_close()
{
    if(!a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been closed.");
        return 0;
    }

    app_avk_stop();

    //RK_bta2dp_setVisibility(0, 0);
    g_bt_control.is_a2dp_sink_open = false;
    return 0;
}

int RK_bta2dp_play()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_START);
    return 0;
}

int RK_bta2dp_pause()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_PAUSE);
    return 0;
}

int RK_bta2dp_stop()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_STOP);
    return 0;
}

int RK_bta2dp_prev()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_PREVIOUS_TRACK);
    return 0;
}

int RK_bta2dp_next()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_NEXT_TRACK);
    return 0;
}

int RK_bta2dp_volume_up()
{
    app_avk_rc_send_cmd((int)APP_AVK_VOLUME_UP);
    return 0;
}

int RK_bta2dp_volume_down()
{
    app_avk_rc_send_cmd((int)APP_AVK_VOLUME_DOWN);
    return 0;
}

int RK_bta2dp_setVisibility(const int visiable, const int connect)
{
    bool discoverable, connectable;

    discoverable = visiable == 0 ? false : true;
    connectable = connect == 0 ? false : true;
    return app_dm_set_visibility(discoverable, connectable);
}

int RK_bta2dp_getState(RK_BTA2DP_State_e *pState)
{
    return app_avk_get_status(pState);
}

int RK_bta2dp_set_auto_reconnect(int enable)
{
    APP_DEBUG0("auto reconnect not support");
    return 0;
}

int RK_bta2dp_disconnect()
{
    /* Close AVK connection (disconnect device) */
    app_avk_close_all();
    return 0;
}

/******************************************/
/***************** BLE ********************/
/******************************************/
static bool ble_is_open()
{
    return g_bt_control.is_ble_open;
}

int RK_blewifi_register_callback(RK_blewifi_state_callback cb)
{
    APP_DEBUG0("is wifi connect status register");
    APP_DEBUG0("Please implement wifi connection in the application layer and register the interface");
    return 0;
}

int RK_ble_recv_data_callback(RK_ble_recv_data cb)
{
    app_ble_wifi_introducer_recv_data_callback(cb);
    return 0;
}

int RK_ble_audio_register_callback(RK_ble_audio_state_callback cb)
{
	return 0;
}

int RK_ble_audio_recv_data_callback(RK_ble_audio_recv_data cb)
{
	return 0;
}

int RK_blewifi_start(char *name)
{
	static const char* BLE_UUID_SERVICE = "0000180A-0000-1000-8000-00805F9B34FB";
	static const char* BLE_UUID_WIFI_CHAR = "00009999-0000-1000-8000-00805F9B34FB";
	static const char* BLE_UUID_PROXIMITY = "7B931104-1810-4CBC-94DA-875C8067F845";
	static const char* BLE_UUID_SEND = "dfd4416e-1810-47f7-8248-eb8be3dc47f9";
	static const char* BLE_UUID_RECV = "9884d812-1810-4a24-94d3-b2c11a851fac";
	Ble_Gatt_Content_t ble_content;
	ble_content.ble_name = "KUGOU W2 ";
	ble_content.server_uuid.uuid = BLE_UUID_SERVICE;
	ble_content.server_uuid.len = UUID_128;
	ble_content.chr_uuid[0].uuid = BLE_UUID_WIFI_CHAR;
	ble_content.chr_uuid[0].len = UUID_128;
	ble_content.chr_uuid[1].uuid = BLE_UUID_SEND;
	ble_content.chr_uuid[1].len = UUID_128;
	ble_content.chr_uuid[2].uuid = BLE_UUID_RECV;
	ble_content.chr_uuid[2].len = UUID_128;
	ble_content.chr_cnt = 3;

	ble_content.adv_kg.flag = 0x1;
	ble_content.adv_kg.flag_value = 0x06;
	ble_content.adv_kg.Company_id = 0x00a5;
	ble_content.adv_kg.iBeacon = 0x1502;
	ble_content.adv_kg.iCompany_id = 0x004c;
	ble_content.adv_kg.local_name_flag = 0x09;
	ble_content.adv_kg.local_name_value = NULL;
	ble_content.adv_kg.Major_id = 0x0049;
	ble_content.adv_kg.Minor_id = 0x000a;
	ble_content.adv_kg.ManufacturerData_flag = 0xff;
	ble_content.adv_kg.Measured_Power = 0xc5;
	ble_content.adv_kg.pid = 0x0102;
	ble_content.adv_kg.Proximity_uuid = BLE_UUID_PROXIMITY;
	ble_content.adv_kg.service_uuid_flag = 0x16;
	ble_content.adv_kg.service_uuid_value = 0x180a;
	ble_content.adv_kg.version = 0x1;

	RK_bsa_ble_start(ble_content);
	return 0;
}

int RK_blewifi_stop(void)
{
	RK_bsa_ble_stop();
	return 0;
}

int RK_bsa_ble_start(Ble_Gatt_Content_t ble_content)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(ble_is_open()) {
        APP_DEBUG0("ble has been opened.");
        return 0;
    }

    if(app_ble_wifi_introducer_open(ble_content) < 0) {
        APP_DEBUG0("ble open failed");
        return -1;
    }

    g_bt_control.is_ble_open = true;
    return 0;
}

int RK_bsa_ble_stop()
{
    if(!ble_is_open()) {
        APP_DEBUG0("ble has been closed.");
        return 0;
    }

    app_ble_wifi_introducer_close();
    g_bt_control.is_ble_open = false;
    return 0;
}

int RK_bleaudio_start(char *name)
{
	return 0;
}

int RK_bleaduio_stop(void)
{
	return 0;
}

int RK_blewifi_getState(RK_BLEWIFI_State_e *pState)
{
    APP_DEBUG0("get wifi connect status");
    APP_DEBUG0("Please implement wifi connection in the application layer and register the interface");
    return 0;
}

int RK_bleaudio_getState(RK_BLE_State_e *pState)
{
	return 0;
}

int RK_blewifi_get_exdata(char *buffer, int *length)
{
    APP_DEBUG0("not support");
    return 0;
}

int RK_ble_write(const char *uuid, unsigned char *data, int len)
{
    app_ble_wifi_introducer_send_message(uuid, data, len);
    return 0;
}

/******************************************/
/*              A2DP SOURCE               */
/******************************************/
static bool a2dp_source_is_open()
{
    return g_bt_control.is_a2dp_source_open;
}

static void a2dp_source_callback(void *userdata, const RK_BtMasterEvent_e enEvent)
{
    switch(enEvent) {
        case RK_BtMasterEvent_Connect_Failed:
            APP_DEBUG0("++++++++++++ BT SOURCE EVENT: connect failed ++++++++++");
            break;
        case RK_BtMasterEvent_Connected:
            APP_DEBUG0("++++++++++++ BT SOURCE EVENT: Connected ++++++++++");
            break;
        case RK_BtMasterEvent_Disconnected:
            APP_DEBUG0("++++++++++++ BT SOURCE EVENT: Disconnected ++++++++++");
            break;
    }
}

/*
 * Turn on Bluetooth and scan SINK devices.
 * Features:
 *     1. enter the master mode
 *     2. Scan surrounding SINK type devices
 *     3. If the SINK device is found, the device with the strongest
 *        signal is automatically connected.(自动连接信号最强的设备)
 * Return:
 *     0: The function is executed successfully and needs to listen
 *        for Bluetooth connection events.
 *    -1: Function execution failed.
 */
int RK_btmaster_connect_start(void *userdata, RK_btmaster_callback cb)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened, close a2dp sink");
        RK_bta2dp_close();
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened.");
        return 0;
    }

    if(app_av_connect_start(userdata, cb)) {
        APP_DEBUG0("app_av_connect_start failed");
        return -1;
    }

    g_bt_control.is_a2dp_source_open = true;
    return 0;
}

int RK_btmaster_stop(void)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    app_av_disconnect_stop();

    g_bt_control.is_a2dp_source_open = false;
    return 0;
}

int RK_btmaster_getDeviceName(char *name, int len)
{
	if (!name || (len <= 0))
		return -1;

    memset(name, 0, len);
    app_mgr_get_bt_config(name, len, NULL, 0);

    return 0;
}

int RK_btmaster_getDeviceAddr(char *addr, int len)
{
	if (!addr || (len < 17))
		return -1;

    memset(addr, 0, len);
    RK_get_bt_mac(addr);

    return 0;
}

int RK_btmaster_getStatus(RK_BtMasterStatus *pstatus)
{
	return 0;
}

void RK_get_bt_mac(char *bt_mac)
{
    BD_ADDR bd_addr;

    if(!bt_mac)
        return;

    app_mgr_get_bt_config(NULL, 0, (char *)bd_addr, BD_ADDR_LEN);
    sprintf(bt_mac, "%02x:%02x:%02x:%02x:%02x:%02x",
             bd_addr[0], bd_addr[1], bd_addr[2],
             bd_addr[3], bd_addr[4], bd_addr[5]);
}

/*****************************************************************
 *                     BLUETOOTH SPP API                         *
 *****************************************************************/
static bool spp_is_open()
{
    return g_bt_control.is_spp_open;
}

int RK_btspp_open(RK_btspp_callback cb)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(spp_is_open()) {
        APP_DEBUG0("bt spp has been opened.");
        return 0;
    }

    if(app_dg_spp_open(cb) < 0) {
        APP_DEBUG0("app_dg_spp_open failed");
        return -1;
    }

    g_bt_control.is_spp_open = true;
    return 0;
}

int RK_btspp_close(void)
{
    if(!spp_is_open()) {
        APP_DEBUG0("bt spp has been closed.");
        return 0;
    }

    app_dg_spp_close();

    g_bt_control.is_spp_open = false;
    return 0;
}

int RK_btspp_getState(RK_BTSPP_State *pState)
{
    return app_dg_get_status();
}

int RK_btspp_write(char *data, int len)
{
    if(app_dg_write_data(data, len) < 0) {
        APP_DEBUG0("RK_btspp_write failed");
        return -1;
    }

    return 0;
}

/*****************************************************************
 *                     BLUETOOTH HEADSET API                     *
 *****************************************************************/
static bool handsfree_is_open()
{
    return g_bt_control.is_handsfree_open;
}

void RK_bt_handsfree_register_callback(RK_bt_handsfree_callback cb)
{
    app_hs_register_cb(cb);
}

int RK_bt_handsfree_open(void)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(handsfree_is_open()) {
        APP_DEBUG0("bt handsfree has been opened.");
        return 0;
    }

    if(app_hs_initialize() < 0) {
        APP_DEBUG0("app_hs_initialize failed");
        return -1;
    }

    g_bt_control.is_handsfree_open = true;
    return 0;
}

int RK_bt_handsfree_close(void)
{
    if(!handsfree_is_open()) {
        APP_DEBUG0("bt handsfree has been closed.");
        return 0;
    }

    app_hs_deinitialize();

    g_bt_control.is_handsfree_open = false;
    return 0;
}

void RK_bt_handsfree_pickup(void)
{
    app_hs_pick_up();
}

void RK_bt_handsfree_hangup(void)
{
    app_hs_hang_up();
}

int rk_bt_control(DeviceIOFramework::BtControl cmd, void *data, int len)
{
	using BtControl_rep_type = std::underlying_type<DeviceIOFramework::BtControl>::type;
	rk_ble_config *ble_cfg;
    Bt_Content_t bt_content;
    Ble_Gatt_Content_t ble_content;
	int ret = 0;
    bool scan;

    bt_print_cmd(cmd);

	switch (cmd) {
	case DeviceIOFramework::BtControl::BT_OPEN:
        bt_content = *((Bt_Content_t *)data);
        ret = RK_bt_open(bt_content.bt_name);
		break;

    case DeviceIOFramework::BtControl::BT_CLOSE:
        RK_bt_close();
        break;

	case DeviceIOFramework::BtControl::BT_SINK_OPEN:
        ret = RK_bta2dp_open(NULL);
		break;

	case DeviceIOFramework::BtControl::BT_SINK_CLOSE:
        ret = RK_bta2dp_close();
		break;

	case DeviceIOFramework::BtControl::BT_SINK_IS_OPENED:
        ret = (int)a2dp_sink_is_open();
		break;

	case DeviceIOFramework::BtControl::BT_BLE_OPEN:
        ble_content = *((Ble_Gatt_Content_t *)data);
        ret = RK_bsa_ble_start(ble_content);
		break;

	case DeviceIOFramework::BtControl::BT_BLE_COLSE:
        ret = RK_bsa_ble_stop();
		break;

	case DeviceIOFramework::BtControl::BT_BLE_IS_OPENED:
        ret = (int)ble_is_open();
		break;

	case DeviceIOFramework::BtControl::BT_BLE_WRITE:
        ble_cfg = (rk_ble_config *)data;
        RK_ble_write(ble_cfg->uuid, (unsigned char *)ble_cfg->data, ble_cfg->len);
		break;

	case DeviceIOFramework::BtControl::BT_SOURCE_OPEN:
        ret = RK_btmaster_connect_start(data, a2dp_source_callback);
		break;

	case DeviceIOFramework::BtControl::BT_SOURCE_CLOSE:
        RK_btmaster_stop();
		break;

	case DeviceIOFramework::BtControl::BT_SOURCE_IS_OPENED:
        ret = a2dp_source_is_open();
		break;

	case DeviceIOFramework::BtControl::GET_BT_MAC:
        RK_get_bt_mac((char *)data);
		break;

	case DeviceIOFramework::BtControl::BT_VOLUME_UP:
        ret = RK_bta2dp_volume_up();
		break;

	case DeviceIOFramework::BtControl::BT_VOLUME_DOWN:
        ret = RK_bta2dp_volume_down();
		break;

	case DeviceIOFramework::BtControl::BT_PLAY:
	case DeviceIOFramework::BtControl::BT_RESUME_PLAY:
        ret = RK_bta2dp_play();
		break;

	case DeviceIOFramework::BtControl::BT_PAUSE_PLAY:
        ret = RK_bta2dp_pause();
		break;

	case DeviceIOFramework::BtControl::BT_AVRCP_FWD:
        ret = RK_bta2dp_prev();
		break;

	case DeviceIOFramework::BtControl::BT_AVRCP_BWD:
        ret = RK_bta2dp_next();
		break;

	case DeviceIOFramework::BtControl::BT_VISIBILITY:
        scan = (*(bool *)data);
        if(scan)
            RK_bta2dp_setVisibility(1, 1);
        else
            RK_bta2dp_setVisibility(0, 0);
		break;

	default:
		APP_DEBUG1("cmd <%d> is not implemented.", static_cast<BtControl_rep_type>(cmd));
		break;
	}

	return ret;
}
