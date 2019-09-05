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
#include "app_disc.h"
#include "app_manager.h"
#include "app_avk.h"
#include "app_av.h"
#include "app_dg.h"
#include "app_ble_rk_server.h"
#include "app_hs.h"
#include "../bluetooth.h"
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
    bool is_hfp_open;
    RK_BT_STATE_CALLBACK bt_state_cb;
    RK_BT_BOND_CALLBACK bt_bond_cb;
} bt_control_t;

volatile bt_control_t g_bt_control = {
    false, false, false, false, false, false, NULL, NULL,
};

static bool bt_is_open();
static bool ble_is_open();
static bool a2dp_sink_is_open();
static bool a2dp_source_is_open();
static bool spp_is_open();
static bool hfp_is_open();

static void bsa_bt_state_send(RK_BT_STATE state)
{
    if(g_bt_control.bt_state_cb)
        g_bt_control.bt_state_cb(state);
}

static void bsa_bt_bond_state_send(const char *address, const char *name, RK_BT_BOND_STATE state)
{
    if(g_bt_control.bt_bond_cb)
        g_bt_control.bt_bond_cb(address, name, state);
}

static void bt_mgr_notify_callback(BD_ADDR bd_addr, char *name, tBSA_MGR_EVT evt)
{
    char address[18];

    if(app_mgr_bd2str(bd_addr, address, 18) < 0)
        memcpy(address, "unknown", strlen("unknown"));

    switch(evt) {
        case BT_LINK_UP_EVT:
            APP_DEBUG0("BT_LINK_UP_EVT\n");
            break;
        case BT_LINK_DOWN_EVT:
            APP_DEBUG0("BT_LINK_DOWN_EVT\n");
            break;
        case BT_WAIT_PAIR_EVT:
            APP_DEBUG0("BT_WAIT_PAIR_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_BONDING);
            break;
        case BT_PAIR_SUCCESS_EVT:
            APP_DEBUG0("BT_PAIR_SUCCESS_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_BONDED);
            break;
        case BT_PAIR_FAILED_EVT:
            APP_DEBUG0("BT_PAIR_FAILED_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_NONE);
            break;
        case BT_UNPAIR_SUCCESS_EVT:
            APP_DEBUG0("BT_UNPAIR_SUCCESS_EVT\n");
            bsa_bt_bond_state_send(address, name, RK_BT_BOND_STATE_NONE);
            break;
    }
}

static void bsa_get_bt_mac(char *bt_mac, int len)
{
    BD_ADDR bd_addr;

    if(!bt_mac)
        return;

    app_mgr_get_bt_config(NULL, 0, (char *)bd_addr, BD_ADDR_LEN);
    app_mgr_bd2str(bd_addr, bt_mac, len);
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

static void check_bsa_server_exist()
{
    while(1) {
        if (get_ps_pid("bsa_server")) {
            APP_DEBUG0("bsa_server has been opened");
            break;
        }

        usleep(500 * 1000);
        APP_DEBUG0("wait bsa_server open");
    }
}

static void check_bsa_server_exit()
{
    while(1) {
        if (!get_ps_pid("bsa_server")) {
            APP_DEBUG0("bsa_server has been closed");
            break;
        }

        usleep(500 * 1000);
        APP_DEBUG0("wait bsa_server close");
    }
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

int rk_bt_is_connected()
{
    if(!bt_is_open())
        return 0;

    if(a2dp_sink_is_open()) {
        RK_BT_SINK_STATE sink_state;
        rk_bt_sink_get_state(&sink_state);
        if(sink_state != RK_BT_SINK_STATE_DISCONNECT && sink_state != RK_BT_SINK_STATE_IDLE)
            return 1;
    }

    if(a2dp_source_is_open()) {
        RK_BT_SOURCE_STATUS source_state;
        rk_bt_source_get_status(&source_state, NULL, 0, NULL, 0);
        if (source_state == BT_SOURCE_STATUS_CONNECTED)
            return 1;
    }

    if(ble_is_open()) {
        RK_BLE_STATE ble_state;
        rk_ble_get_state(&ble_state);
        if(ble_state == RK_BLE_STATE_CONNECT)
            return 1;
    }

    if(hfp_is_open()) {
        RK_BT_HFP_EVENT hfp_state;
        app_hs_get_state(&hfp_state);
        if(hfp_state == RK_BT_HFP_CONNECT_EVT)
            return 1;
    }

    if(ble_is_open()) {
        RK_BLE_STATE ble_state;
        rk_ble_get_state(&ble_state);
        if(ble_state == RK_BLE_STATE_CONNECT)
            return 1;
    }

    if(spp_is_open()) {
        RK_BT_SPP_STATE spp_state;
        rk_bt_spp_get_state(&spp_state);
        if(spp_state == RK_BT_SPP_STATE_CONNECT)
            return 1;
    }

    return 0;
}

void rk_bt_register_state_callback(RK_BT_STATE_CALLBACK cb)
{
    g_bt_control.bt_state_cb = cb;
}

void rk_bt_register_bond_callback(RK_BT_BOND_CALLBACK cb)
{
    g_bt_control.bt_bond_cb = cb;
}

int rk_bt_init(RkBtContent *p_bt_content)
{
    if(!p_bt_content) {
        APP_ERROR0("bt content is null");
        return -1;
    }

    if (bt_is_open()) {
        APP_DEBUG0("bluetooth has been opened.");
        return 0;
    }

    bsa_bt_state_send(RK_BT_STATE_TURNING_ON);

    /* start bsa_server */
    if(bt_bsa_server_open() < 0) {
        APP_DEBUG0("bsa server open failed.");
        return -1;
    }

    check_bsa_server_exist();

    APP_DEBUG1("p_bt_content->bt_name: %s", p_bt_content->bt_name);

    /* Init App manager */
    if(app_manager_init(p_bt_content->bt_name, bt_mgr_notify_callback) < 0) {
        APP_DEBUG0("app_manager init failed.");
        return -1;
    }

    g_bt_control.is_bt_open = true;
    bsa_bt_state_send(RK_BT_STATE_ON);
    //app_mgr_set_sleep_mode_param();
    return 0;
}

int rk_bt_deinit()
{
    if (!bt_is_open()) {
        APP_DEBUG0("bluetooth has been closed.");
        return -1;
    }

    bsa_bt_state_send(RK_BT_STATE_TURNING_OFF);

    rk_bt_sink_close();
    rk_ble_stop();
    rk_bt_source_auto_connect_stop();
    rk_bt_spp_close();
    rk_bt_hfp_close();

    /* Close BSA before exiting (to release resources) */
    app_manager_deinit();

    /* stop bsa_server */
    bt_bsa_server_close();
    check_bsa_server_exit();

    app_mgr_deregister_disc_cb();
    app_mgr_deregister_dev_found_cb();
    g_bt_control.bt_bond_cb = NULL;
    g_bt_control.is_bt_open = false;

    bsa_bt_state_send(RK_BT_STATE_OFF);
    g_bt_control.bt_state_cb = NULL;
    return 0;
}

int rk_bt_set_class(int value)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgt_set_cod(value);
}

int rk_bt_enable_reconnect(int enable)
{
    return app_mgr_set_auto_reconnect(enable);
}

void rk_bt_register_discovery_callback(RK_BT_DISCOVERY_CALLBACK cb)
{
    app_mgr_register_disc_cb(cb);
}

void rk_bt_register_dev_found_callback(RK_BT_DEV_FOUND_CALLBACK cb)
{
    app_mgr_register_dev_found_cb(cb);
}

int rk_bt_start_discovery(unsigned int mseconds)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_disc_complete() != APP_DISCOVERYING)
        return app_disc_start_regular(NULL, mseconds/1000 + mseconds%1000);
    else
        return -1;
}

int rk_bt_cancel_discovery()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_disc_complete() == APP_DISCOVERYING)
        return app_disc_abort();
    else
        return -1;
}

bool rk_bt_is_discovering()
{
    int disc = app_disc_complete();
    if(disc == APP_DISCOVERYING)
        return true;
    else
        return false;
}

void rk_bt_display_devices()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return;
    }

    app_disc_display_devices();
}

void rk_bt_display_paired_devices()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return;
    }

    app_mgr_xml_display_devices();
}

int rk_bt_pair_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("pair device(%s)failed", addr);
        return -1;
    }

    return app_mgr_sec_bond(bd_addr);
}

int rk_bt_unpair_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("unpair device(%s)failed", addr);
        return -1;
    }

    return app_mgr_sec_unpair(bd_addr);
}

int rk_bt_set_device_name(char *name)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgt_set_device_name(name);
}

int rk_bt_get_device_name(char *name, int len)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (!name || (len <= 0))
        return -1;

    memset(name, 0, len);
    app_mgr_get_bt_config(name, len, NULL, 0);

    return 0;
}

int rk_bt_get_device_addr(char *addr, int len)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (!addr || (len < 17))
        return -1;

    bsa_get_bt_mac(addr, len);
    return 0;
}

int rk_bt_get_paired_devices(RkBtPraiedDevice **dev_list,int *count)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgr_get_paired_devices(dev_list, count);
}

int rk_bt_free_paired_devices(RkBtPraiedDevice *dev_list)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    return app_mgr_free_paired_devices(dev_list);
}

/******************************************/
/*               A2DP SINK                */
/******************************************/
static bool a2dp_sink_is_open()
{
    return g_bt_control.is_a2dp_sink_open;
}

int rk_bt_sink_register_callback(RK_BT_SINK_CALLBACK cb)
{
    app_avk_register_cb(cb);
    return 0;
}

int rk_bt_sink_register_volume_callback(RK_BT_SINK_VOLUME_CALLBACK cb)
{
    app_avk_register_volume_cb(cb);
    return 0;
}

int rk_bt_sink_register_track_callback(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
    app_avk_register_track_cb(cb);
    return 0;
}

int rk_bt_sink_register_position_callback(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
    app_avk_register_position_cb(cb);
    return 0;
}

int rk_bt_sink_get_default_dev_addr(char *addr, int len)
{
    return 0;
}

int rk_bt_sink_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened, close a2dp source");
        rk_bt_source_auto_connect_stop();
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened.");
        return 0;
    }

    if (app_avk_start() < 0) {
        APP_DEBUG0("app_avk_start failed");
        return -1;
    }

    //rk_bt_sink_set_visibility(1, 1);
    g_bt_control.is_a2dp_sink_open = true;
    return 0 ;
}

int rk_bt_sink_close()
{
    if(!a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been closed.");
        return 0;
    }

    app_avk_stop();

    //rk_bt_sink_set_visibility(0, 0);
    g_bt_control.is_a2dp_sink_open = false;
    return 0;
}

int rk_bt_sink_play()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_START);
    return 0;
}

int rk_bt_sink_pause()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_PAUSE);
    return 0;
}

int rk_bt_sink_stop()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_STOP);
    return 0;
}

int rk_bt_sink_prev()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_PREVIOUS_TRACK);
    return 0;
}

int rk_bt_sink_next()
{
    app_avk_rc_send_cmd((int)APP_AVK_PLAY_NEXT_TRACK);
    return 0;
}

int rk_bt_sink_volume_up()
{
    app_avk_rc_send_cmd((int)APP_AVK_VOLUME_UP);
    return 0;
}

int rk_bt_sink_volume_down()
{
    app_avk_rc_send_cmd((int)APP_AVK_VOLUME_DOWN);
    return 0;
}

int rk_bt_sink_set_volume(int volume)
{
    return app_avk_set_volume(volume);
}

int rk_bt_sink_set_visibility(const int visiable, const int connect)
{
    bool discoverable, connectable;

    discoverable = visiable == 0 ? false : true;
    connectable = connect == 0 ? false : true;
    return app_dm_set_visibility(discoverable, connectable);
}

int rk_bt_sink_get_state(RK_BT_SINK_STATE *pState)
{
    return app_avk_get_state(pState);
}

int rk_bt_sink_set_auto_reconnect(int enable)
{
    return rk_bt_enable_reconnect(enable);
}

int rk_bt_sink_get_play_status()
{
    return app_avk_get_play_status();
}

bool rk_bt_sink_get_poschange()
{
    return app_avk_get_pos_change();
}

int rk_bt_sink_disconnect()
{
    if(!a2dp_sink_is_open()) {
        APP_ERROR0("sink don't open, please open");
        return -1;
    }

    /* Close AVK connection (disconnect device) */
    app_avk_close_all();
    return 0;
}

int rk_bt_sink_connect_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!a2dp_sink_is_open()) {
        APP_ERROR0("sink don't open, please open");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("connect device(%s)failed", addr);
        return -1;
    }

    return app_avk_open(bd_addr, NULL);
}

int rk_bt_sink_disconnect_by_addr(char *addr)
{
    BD_ADDR bd_addr;

    if(!addr || (strlen(addr) < 17)) {
        APP_ERROR0("invalid address");
        return -1;
    }

    if(!a2dp_sink_is_open()) {
        APP_ERROR0("sink don't open, please open");
        return -1;
    }

    if(app_mgr_str2bd(addr, bd_addr) < 0) {
        APP_ERROR1("disconnect device(%s)failed", addr);
        return -1;
    }

    return app_avk_close(bd_addr);
}

/******************************************/
/***************** BLE ********************/
/******************************************/
static bool ble_is_open()
{
    return g_bt_control.is_ble_open;
}

int rk_ble_register_status_callback(RK_BLE_STATE_CALLBACK cb)
{
    app_ble_rk_server_register_cb(cb);
    return 0;
}

int rk_ble_register_recv_callback(RK_BLE_RECV_CALLBACK cb)
{
    app_ble_rk_server_recv_data_callback(cb);
    return 0;
}

int rk_ble_get_state(RK_BLE_STATE *p_state)
{
    app_ble_rk_server_get_state(p_state);
    return 0;
}

int rk_ble_start(RkBleContent *ble_content)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(ble_is_open()) {
        APP_DEBUG0("ble has been opened.");
        return 0;
    }

    if(app_ble_rk_server_open(ble_content) < 0) {
        APP_DEBUG0("ble open failed");
        return -1;
    }

    g_bt_control.is_ble_open = true;
    return 0;
}

int rk_ble_stop()
{
    if(!ble_is_open()) {
        APP_DEBUG0("ble has been closed.");
        return 0;
    }

    app_ble_rk_server_close();
    g_bt_control.is_ble_open = false;
    return 0;
}

int rk_ble_write(const char *uuid, char *data, int len)
{
    app_ble_rk_server_send_message(uuid, data, len);
    return 0;
}

int rk_ble_setup(RkBleContent *ble_content)
{
    return 0;
}

int rk_ble_clean(void)
{
    return 0;
}

/******************************************/
/*              A2DP SOURCE               */
/******************************************/
static bool a2dp_source_is_open()
{
    return g_bt_control.is_a2dp_source_open;
}

int rk_bt_source_register_status_cb(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
    app_av_register_cb(userdata, cb);
    return 0;
}

int rk_bt_source_get_status(RK_BT_SOURCE_STATUS *pstatus, char *name, int name_len,
                                    char *address, int addr_len)
{
    app_av_get_status(pstatus, name, name_len, address, addr_len);
    return 0;
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
int rk_bt_source_auto_connect_start(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened, close a2dp sink");
        rk_bt_sink_close();
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened.");
        return 0;
    }

    if(app_av_auto_connect_start(userdata, cb) < 0) {
        APP_DEBUG0("app_av_auto_connect_start failed");
        app_av_auto_connect_stop();
        return -1;
    }

    g_bt_control.is_a2dp_source_open = true;
    return 0;
}

int rk_bt_source_auto_connect_stop()
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    app_av_auto_connect_stop();

    g_bt_control.is_a2dp_source_open = false;
    return 0;
}

int rk_bt_source_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if (a2dp_sink_is_open()) {
        APP_DEBUG0("a2dp sink has been opened, close a2dp sink");
        rk_bt_sink_close();
    }

    if (a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been opened.");
        return 0;
    }

    if(app_av_initialize() < 0) {
        APP_DEBUG0("app_av_initialize failed");
        app_av_deinitialize();
        return -1;
    }

    g_bt_control.is_a2dp_source_open = true;
    return 0;
}

int rk_bt_source_close()
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    app_av_deinitialize();

    g_bt_control.is_a2dp_source_open = false;
    return 0;
}

int rk_bt_source_scan(BtScanParam *data)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source is not inited, please init");
        return -1;
    }

    if(app_av_scan(data) < 0) {
        APP_DEBUG0("app_av_scan failed");
        return -1;
    }

    return 0;
}

int rk_bt_source_connect(char *address)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source is not inited, please init");
        return -1;
    }

    if(app_av_connect(address) < 0) {
        APP_ERROR1("app_av_connect failed, address: %s", address);
        return -1;
    }

    return 0;
}

int rk_bt_source_disconnect(char *address)
{
    if(!a2dp_source_is_open()) {
        APP_DEBUG0("a2dp source has been closed.");
        return 0;
    }

    if (app_av_disconnect(address) < 0) {
        APP_ERROR1("app_av_disconnect failed, address: %s", address);
        return -1;
    }

    return 0;
}

int rk_bt_source_remove(char *address)
{
    if (app_av_remove(address) < 0) {
        APP_ERROR1("app_av_remvoe failed, address: %s", address);
        return -1;
    }
    return 0;
}

int rk_bt_source_get_device_name(char *name, int len)
{
    return rk_bt_get_device_name(name, len);
}

int rk_bt_source_get_device_addr(char *addr, int len)
{
    return rk_bt_get_device_addr(addr, len);
}

int rk_bt_source_resume()
{
    return app_av_resume();
}

int rk_bt_source_stop()
{
    return app_av_stop();
}

int rk_bt_source_pause()
{
    return app_av_pause();
}

int rk_bt_source_vol_up()
{
    return app_av_vol_up();
}

int rk_bt_source_vol_down()
{
    return app_av_vol_down();
}

/*****************************************************************
 *                     BLUETOOTH SPP API                         *
 *****************************************************************/
static bool spp_is_open()
{
    return g_bt_control.is_spp_open;
}

int rk_bt_spp_register_status_cb(RK_BT_SPP_STATUS_CALLBACK cb)
{
    app_dg_register_cb(cb);
    return 0;
}

int rk_bt_spp_register_recv_cb(RK_BT_SPP_RECV_CALLBACK cb)
{
    app_dg_register_recv_cb(cb);
    return 0;
}

int rk_bt_spp_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(spp_is_open()) {
        APP_DEBUG0("bt spp has been opened.");
        return 0;
    }

    if(app_dg_spp_open() < 0) {
        APP_DEBUG0("app_dg_spp_open failed");
        return -1;
    }

    g_bt_control.is_spp_open = true;
    return 0;
}

int rk_bt_spp_close()
{
    if(!spp_is_open()) {
        APP_DEBUG0("bt spp has been closed.");
        return 0;
    }

    app_dg_spp_close();

    g_bt_control.is_spp_open = false;
    return 0;
}

int rk_bt_spp_get_state(RK_BT_SPP_STATE *pState)
{
    *pState = app_dg_get_status();
    return 0;
}

int rk_bt_spp_write(char *data, int len)
{
    if(app_dg_write_data(data, len) < 0) {
        APP_DEBUG0("rk_bt_spp_write failed");
        return -1;
    }

    return 0;
}

/*****************************************************************
 *                     BLUETOOTH HEADSET API                     *
 *****************************************************************/
static bool hfp_is_open()
{
    return g_bt_control.is_hfp_open;
}

void rk_bt_hfp_register_callback(RK_BT_HFP_CALLBACK cb)
{
    app_hs_register_cb(cb);
}

int rk_bt_hfp_sink_open()
{
    rk_bt_sink_open();
    rk_bt_hfp_open();

    return 0 ;
}

int rk_bt_hfp_open()
{
    if(!bt_is_open()) {
        APP_DEBUG0("bluetooth is not inited, please init");
        return -1;
    }

    if(hfp_is_open()) {
        APP_DEBUG0("bt hfp has been opened.");
        return 0;
    }

    if(app_hs_initialize() < 0) {
        APP_DEBUG0("app_hs_initialize failed");
        return -1;
    }

    g_bt_control.is_hfp_open = true;
    return 0;
}

int rk_bt_hfp_close()
{
    if(!hfp_is_open()) {
        APP_DEBUG0("bt hfp has been closed.");
        return 0;
    }

    app_hs_deinitialize();

    g_bt_control.is_hfp_open = false;
    return 0;
}

int rk_bt_hfp_pickup()
{
    return app_hs_pick_up();
}

int rk_bt_hfp_hangup()
{
    return app_hs_hang_up();
}

int rk_bt_hfp_set_volume(int volume)
{
    return app_hs_set_vol(volume);
}

int rk_bt_hfp_redial(void)
{
    return app_hs_redial();
}

int rk_bt_hfp_report_battery(int value)
{
    return app_hs_report_battery(value);
}

void rk_bt_hfp_enable_cvsd()
{
    app_hs_set_cvsd(true);
}

void rk_bt_hfp_disable_cvsd()
{
    app_hs_set_cvsd(false);
}

int rk_bt_hfp_disconnect()
{
    if(!hfp_is_open()) {
        APP_ERROR0("hfp don't open, please open");
        return -1;
    }

    /* release mono headset connections */
    return app_hs_close_all();
}

/* OBEX FOR PBAP */
int rk_bt_obex_init()
{
    return 0;
}

int rk_bt_obex_pbap_connect(char *btaddr)
{
    return 0;
}

int rk_bt_obex_pbap_get_vcf(char *dir_name, char *dir_file)
{
    return 0;
}

int rk_bt_obex_pbap_disconnect(char *btaddr)
{
    return 0;
}

int rk_bt_obex_close()
{
    return 0;
}

int rk_bt_control(DeviceIOFramework::BtControl cmd, void *data, int len)
{
    using BtControl_rep_type = std::underlying_type<DeviceIOFramework::BtControl>::type;
    RkBleConfig *ble_cfg;
    RkBtContent bt_content;
    RkBleContent ble_content;
    int ret = 0;
    bool scan;

    APP_DEBUG1("cmd: %d", cmd);

    switch (cmd) {
    case DeviceIOFramework::BtControl::BT_OPEN:
        bt_content = *((RkBtContent *)data);
        ret = rk_bt_init(&bt_content);
        break;

    case DeviceIOFramework::BtControl::BT_CLOSE:
        rk_bt_deinit();
        break;

    case DeviceIOFramework::BtControl::BT_SINK_OPEN:
        ret = rk_bt_sink_open();
        break;

    case DeviceIOFramework::BtControl::BT_SINK_CLOSE:
        ret = rk_bt_sink_close();
        break;

    case DeviceIOFramework::BtControl::BT_SINK_IS_OPENED:
        ret = (int)a2dp_sink_is_open();
        break;

    case DeviceIOFramework::BtControl::BT_BLE_OPEN:
        ble_content = *((RkBleContent *)data);
        ret = rk_ble_start(&ble_content);
        break;

    case DeviceIOFramework::BtControl::BT_BLE_COLSE:
        ret = rk_ble_stop();
        break;

    case DeviceIOFramework::BtControl::BT_BLE_IS_OPENED:
        ret = (int)ble_is_open();
        break;

    case DeviceIOFramework::BtControl::BT_BLE_WRITE:
        ble_cfg = (RkBleConfig *)data;
        rk_ble_write(ble_cfg->uuid, (char *)ble_cfg->data, ble_cfg->len);
        break;

    case DeviceIOFramework::BtControl::BT_SOURCE_OPEN:
        ret = rk_bt_source_auto_connect_start(data, NULL);
        break;

    case DeviceIOFramework::BtControl::BT_SOURCE_CLOSE:
        rk_bt_source_auto_connect_stop();
        break;

    case DeviceIOFramework::BtControl::BT_SOURCE_IS_OPENED:
        ret = a2dp_source_is_open();
        break;

    case DeviceIOFramework::BtControl::BT_VOLUME_UP:
        ret = rk_bt_sink_volume_up();
        break;

    case DeviceIOFramework::BtControl::BT_VOLUME_DOWN:
        ret = rk_bt_sink_volume_down();
        break;

    case DeviceIOFramework::BtControl::BT_PLAY:
    case DeviceIOFramework::BtControl::BT_RESUME_PLAY:
        ret = rk_bt_sink_play();
        break;

    case DeviceIOFramework::BtControl::BT_PAUSE_PLAY:
        ret = rk_bt_sink_pause();
        break;

    case DeviceIOFramework::BtControl::BT_AVRCP_FWD:
        ret = rk_bt_sink_prev();
        break;

    case DeviceIOFramework::BtControl::BT_AVRCP_BWD:
        ret = rk_bt_sink_next();
        break;

    case DeviceIOFramework::BtControl::BT_VISIBILITY:
        scan = (*(bool *)data);
        if(scan)
            rk_bt_sink_set_visibility(1, 1);
        else
            rk_bt_sink_set_visibility(0, 0);
        break;

    default:
        APP_DEBUG1("cmd <%d> is not implemented.", static_cast<BtControl_rep_type>(cmd));
        break;
    }

    return ret;
}
