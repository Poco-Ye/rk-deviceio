#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

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
#include "obex_client.h"
#include "utility.h"
#include "slog.h"

extern RkBtContent GBt_Content;
extern volatile bt_control_t bt_control;

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::WifiControl;
using DeviceIOFramework::BtControl;
using DeviceIOFramework::wifi_config;

#define BT_CONFIG_FAILED 2
#define BT_CONFIG_OK 1

typedef struct {
	int sockfd;
	pthread_t tid;
	RK_BT_SINK_UNDERRUN_CB cb;
} underrun_handler_t;

static underrun_handler_t g_underrun_handler = {
	-1, 0, NULL,
};

/*****************************************************************
 *            Rockchip bluetooth LE api                      *
 *****************************************************************/
int rk_bt_ble_set_visibility(const int visiable, const int connect)
{
	pr_info("bluez don't support %s\n", __func__);
	return 0;
}

int rk_ble_start(RkBleContent *ble_content)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if(ble_is_open()) {
		pr_info("ble has been opened\n");
		return -1;
	}

	rk_bt_control(BtControl::BT_BLE_OPEN, NULL, 0);
	ble_state_send(RK_BLE_STATE_IDLE);
	return 0;
}

int rk_ble_stop(void)
{
	if (!ble_is_open()) {
		pr_info("ble has been closed\n");
		return -1;
	}

	rk_bt_control(BtControl::BT_BLE_COLSE, NULL, 0);
	return 0;
}

int rk_ble_setup(RkBleContent *ble_content)
{
	if (!ble_is_open()) {
		pr_info("ble isn't open, please open\n");
		return -1;
	}

	rk_bt_control(BtControl::BT_BLE_SETUP, NULL, 0);
	return 0;
}

int rk_ble_clean(void)
{
	if (!ble_is_open()) {
		pr_info("ble isn't open, please open\n");
		return -1;
	}

	rk_bt_control(BtControl::BT_BLE_CLEAN, NULL, 0);
	return 0;
}

int rk_ble_get_state(RK_BLE_STATE *p_state)
{
	ble_get_state(p_state);
	return 0;
}

#define BLE_SEND_MAX_LEN (134) //(20) //(512)
int rk_ble_write(const char *uuid, char *data, int len)
{
#if 1
	RkBleConfig ble_cfg;

	if (!ble_is_open()) {
		pr_info("ble isn't open, please open\n");
		return -1;
	}

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
			pr_info("rk_ble_write failed!\n");
			return ret;
		}
	}

	return ret;
#endif
}

int rk_ble_register_status_callback(RK_BLE_STATE_CALLBACK cb)
{
	ble_register_state_callback(cb);
	return 0;
}

int rk_ble_register_recv_callback(RK_BLE_RECV_CALLBACK cb)
{
	if (cb) {
		pr_info("BlueZ does not support this interface."
			"Please set the callback function when initializing BT.\n");
	}

	return 0;
}

int rk_ble_disconnect()
{
    pr_info("bluez don't support %s\n", __func__);
    return 0;
}

void rk_ble_set_local_privacy(bool local_privacy)
{
	pr_info("bluez don't support %s\n", __func__);
}

/*****************************************************************
 *            Rockchip bluetooth master api                      *
 *****************************************************************/
static pthread_t g_btmaster_thread = 0;

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
	pr_info("=== BT_SOURCE_SCAN ===\n");
	ret = rk_bt_source_scan(&scan_param);
	if (ret && (scan_cnt--)) {
		sleep(1);
		goto scan_retry;
	} else if (ret) {
		pr_info("ERROR: Scan error!\n");
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED, "", "");
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
			pr_info("#%02d Name:%s\n", i, start->name);
			pr_info("\tAddress:%s\n", start->address);
			pr_info("\tRSSI:%d\n", start->rssi);
			pr_info("\tPlayrole:%s\n", start->playrole);
			max_rssi = start->rssi;

			memcpy(target_address, start->address, 17);
			target_vaild = true;
		}
	}

	if (!target_vaild) {
		pr_info("=== Cannot find audio Sink devices. ===\n");
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED, "", "");
		return NULL;
	} else if (max_rssi < -80) {
		pr_info("=== BT SOURCE RSSI is is too weak !!! ===\n");
		a2dp_master_event_send(BT_SOURCE_EVENT_CONNECT_FAILED, "", "");
		return NULL;
	}

	/* Connect target device */
	if (!a2dp_master_status(NULL, 0, NULL, 0))
		a2dp_master_connect(target_address);

	pr_info("%s: Exit _btmaster_autoscan_and_connect thread!\n", __func__);
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

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (g_btmaster_thread) {
		pr_info("The last operation is still in progress, please stop then start\n");
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
		pr_info("_btmaster_autoscan_and_connect thread create failed!\n");
		return -1;
	}

	return 0;
}

int rk_bt_source_auto_connect_stop(void)
{
	if (g_btmaster_thread) {
		pthread_join(g_btmaster_thread, NULL);
		g_btmaster_thread = 0;
	}

	return rk_bt_source_close();
}

int rk_bt_source_open(void)
{
	if (!bt_is_open()) {
		pr_err("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (g_btmaster_thread) {
		pr_err("The last operation is still in progress\n");
		return -1;
	}

	/* Set bluetooth to master mode */
	if (!bt_source_is_open()) {
		pr_info("=== BtControl::BT_SOURCE_OPEN ===\n");
		bt_control.type = BtControlType::BT_SOURCE;
		if (bt_sink_is_open()) {
			RK_LOGE("bt sink isn't coexist with source!!!\n");
			bt_close_sink();
		}

		if (bt_interface(BtControl::BT_SOURCE_OPEN, NULL) < 0) {
			bt_control.is_a2dp_source_open = false;
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
	if (!bt_source_is_open()) {
		pr_info("bt source has benn closed\n");
		return -1;
	}

	bt_close_source();
	a2dp_master_deregister_cb();
	return 0;
}

int rk_bt_source_scan(BtScanParam *data)
{
	if (!bt_source_is_open()) {
		pr_info("bt source isn't open, please open\n");
		return -1;
	}

	return a2dp_master_scan(data, sizeof(BtScanParam));
}

int rk_bt_source_connect(char *address)
{
	if (!bt_source_is_open()) {
		pr_info("bt source isn't open, please open\n");
		return -1;
	}

	return a2dp_master_connect(address);
}

int rk_bt_source_disconnect(char *address)
{
	if (!bt_source_is_open()) {
		pr_info("bt source isn't open, please open\n");
		return -1;
	}

	return a2dp_master_disconnect(address);
}

int rk_bt_source_remove(char *address)
{
	if (!bt_source_is_open()) {
		pr_info("bt source isn't open, please open\n");
		return -1;
	}

	return a2dp_master_remove(address);
}

int rk_bt_source_register_status_cb(void *userdata, RK_BT_SOURCE_CALLBACK cb)
{
	a2dp_master_register_cb(userdata,  cb);
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
void *sink_underrun_listen(void *arg)
{
	int ret = 0;
	char buff[100] = {0};
	struct sockaddr_un clientAddr;
	struct sockaddr_un serverAddr;
	socklen_t addr_len;

	g_underrun_handler.sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_underrun_handler.sockfd < 0) {
		pr_err("%s: Create socket failed!\n", __func__);
		return NULL;
	}

	serverAddr.sun_family = AF_UNIX;
	strcpy(serverAddr.sun_path, "/tmp/rk_deviceio_a2dp_underrun");

	system("rm -rf /tmp/rk_deviceio_a2dp_underrun");
	ret = bind(g_underrun_handler.sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	if (ret < 0) {
		pr_err("%s: Bind Local addr failed!\n", __func__);
		return NULL;
	}

	pr_info("%s: underrun listen...\n", __func__);
	while(1) {
		memset(buff, 0, sizeof(buff));
		ret = recvfrom(g_underrun_handler.sockfd, buff, sizeof(buff), 0, (struct sockaddr *)&clientAddr, &addr_len);
		if (ret <= 0) {
			if (ret == 0)
				pr_info("%s: socket closed!\n", __func__);
			break;
		}
		pr_info("%s: recv a message(%s)\n", __func__, buff);

		if (!bt_sink_is_open())
			break;

		if (!strstr(buff, "a2dp underrun;")) {
			pr_warning("%s: recv a unsupport msg:%s\n", __func__, buff);
			continue;
		}

		if (g_underrun_handler.cb)
			g_underrun_handler.cb();
		else
			break;
	}

	pr_info("%s: Exit underrun listen thread!\n", __func__);
	return NULL;
}

static int underrun_listen_thread_create(RK_BT_SINK_UNDERRUN_CB cb)
{
	pr_info("underrun_listen_thread_create\n");

	g_underrun_handler.cb = cb;

	/* Create a thread to listen for bluez-alsa sink underrun. */
	if (!g_underrun_handler.tid) {
		if (pthread_create(&g_underrun_handler.tid, NULL, sink_underrun_listen, NULL)) {
			pr_err("Create underrun listen pthread failed\n");
			return -1;
		}

		pthread_setname_np(g_underrun_handler.tid, "underrun_listen");
	}

	return 0;
}

static void underrun_listen_thread_delete()
{
	pr_debug("%s enter\n", __func__);
	if (g_underrun_handler.sockfd >= 0) {
		shutdown(g_underrun_handler.sockfd, SHUT_RDWR);
		g_underrun_handler.sockfd = -1;
	}

	if(g_underrun_handler.tid) {
		pthread_join(g_underrun_handler.tid, NULL);
		g_underrun_handler.tid = 0;
	}

	g_underrun_handler.cb = NULL;
	pr_debug("%s exit\n", __func__);
}
int rk_bt_sink_register_callback(RK_BT_SINK_CALLBACK cb)
{
	a2dp_sink_register_cb(cb);
	return 0;
}

void rk_bt_sink_register_underurn_callback(RK_BT_SINK_UNDERRUN_CB cb)
{
	underrun_listen_thread_create(cb);
}

int rk_bt_sink_register_volume_callback(RK_BT_SINK_VOLUME_CALLBACK cb)
{
	a2dp_sink_register_volume_cb(cb);
	return 0;
}

int rk_bt_sink_register_track_callback(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
	a2dp_sink_register_track_cb(cb);
	return 0;
}

int rk_bt_sink_register_position_callback(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
	a2dp_sink_register_position_cb(cb);
	return 0;
}

int rk_bt_sink_get_default_dev_addr(char *addr, int len)
{
	return bt_get_default_dev_addr(addr, len);
}

int rk_bt_sink_open()
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (bt_sink_is_open()) {
		pr_info("bt sink has been opened\n");
		return -1;
	}

	if (bt_source_is_open()) {
		RK_LOGE("bt sink isn't coexist with source!!!\n");
		bt_close_source();
	}

	if (bt_interface(BtControl::BT_SINK_OPEN, NULL) < 0) {
		bt_control.is_a2dp_sink_open = false;
		bt_control.type = BtControlType::BT_NONE;
		return -1;
	}

	reconn_last_devices(BT_DEVICES_A2DP_SOURCE);

	bt_control.is_a2dp_sink_open = true;
	/* Set bluetooth control current type */
	bt_control.type = BtControlType::BT_SINK;
	bt_control.last_type = BtControlType::BT_SINK;

	return 0;
}

int rk_bt_sink_set_visibility(const int visiable, const int connectable)
{
	if (visiable && connectable) {
		RK_shell_system("hciconfig hci0 piscan");
		return 0;
	}

	RK_shell_system("hciconfig hci0 noscan");
	usleep(20000);//20ms
	if (visiable)
		RK_shell_system("hciconfig hci0 iscan");
	if (connectable)
		RK_shell_system("hciconfig hci0 pscan");

	return 0;
}

int rk_bt_sink_close(void)
{
	if (!bt_sink_is_open()) {
		pr_info("bt sink has been closed\n");
		return -1;
	}

	bt_close_sink();
	underrun_listen_thread_delete();
	a2dp_sink_clear_cb();
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

int rk_bt_sink_get_play_status()
{
	if (!bt_sink_is_open()) {
		pr_info("bt sink isn't open, please open\n");
		return -1;
	}

	return get_play_status_avrcp();
}

bool rk_bt_sink_get_poschange()
{
	if (!bt_sink_is_open()) {
		pr_info("bt sink isn't open, please open\n");
		return false;
	}

	return get_poschange_avrcp();
}

int rk_bt_sink_disconnect()
{
	if (!bt_sink_is_open()) {
		pr_info("bt sink isn't open, please open\n");
		return -1;
	}

	return disconnect_current_devices();
}

int rk_bt_sink_connect_by_addr(char *addr)
{
	if(bt_sink_is_open()) {
		//bt_sink_state_send(RK_BT_SINK_STATE_CONNECTING);
		return connect_by_address(addr);
	}

	return -1;
}

int rk_bt_sink_disconnect_by_addr(char *addr)
{
	if(bt_sink_is_open()) {
		//bt_sink_state_send(RK_BT_SINK_STATE_DISCONNECTING);
		return disconnect_by_address(addr);
	}

	return -1;
}

static int _get_bluealsa_plugin_volume_ctrl_info(char *name, int *value)
{
	char buff[1024] = {0};
	char ctrl_name[128] = {0};
	int ctrl_value = 0;
	char *start = NULL;
	char *end = NULL;

	if (!name && !value)
		return -1;

	if (name) {
		RK_shell_exec("amixer -D bluealsa scontents", buff, sizeof(buff));
		start = strstr(buff, "Simple mixer control ");
		end = strstr(buff, "A2DP'");
		if (!start || (!strstr(start, "A2DP")))
			return -1;

		start += strlen("Simple mixer control '");
		end += strlen("A2DP");
		if ((end - start) < strlen(" - A2DP"))
			return -1;

		memcpy(ctrl_name, start, end-start);
		memcpy(name, ctrl_name, strlen(ctrl_name));
	}

	if (value) {
		start = strstr(buff, "Front Left: Capture ");
		if (!start)
			return -1;

		start += strlen("Front Left: Capture ");
		if ((*start < '0') || (*start > '9'))
			return -1;

		/* Max volume value:127, the length of volume value string must be <= 3 */
		ctrl_value += (*start - '0');
		start++;
		if ((*start >= '0') && (*start <= '9'))
			ctrl_value = 10 * ctrl_value + (*start - '0');
		start++;
		if ((*start >= '0') && (*start <= '9'))
			ctrl_value = 10 * ctrl_value + (*start - '0');

		*value = ctrl_value;
	}

	return 0;
}

static int _set_bluealsa_plugin_volume_ctrl_info(char *name, int value)
{
	char buff[1024] = {0};
	char cmd[256] = {0};
	char ctrl_name[128] = {0};
	int new_volume = 0;

	if (!name)
		return -1;

	sprintf(cmd, "amixer -D bluealsa sset \"%s\" %d", name, value);
	RK_shell_exec(cmd, buff, sizeof(buff));

	if (_get_bluealsa_plugin_volume_ctrl_info(ctrl_name, &new_volume) == -1)
		return -1;
	if (new_volume != value)
		return -1;

	return 0;
}

int rk_bt_sink_volume_up(void)
{
	char ctrl_name[128] = {0};
	int current_volume = 0;
	int ret = 0;

	if (!bt_sink_is_open()) {
		pr_info("bt sink isn't open, please open\n");
		return -1;
	}

	ret = _get_bluealsa_plugin_volume_ctrl_info(ctrl_name, &current_volume);
	if (ret)
		return ret;

	ret = _set_bluealsa_plugin_volume_ctrl_info(ctrl_name, current_volume + 8);
	return ret;
}

int rk_bt_sink_volume_down(void)
{
	char ctrl_name[128] = {0};
	int current_volume = 0;
	int ret = 0;

	if (!bt_sink_is_open()) {
		pr_info("bt sink isn't open, please open\n");
		return -1;
	}

	ret = _get_bluealsa_plugin_volume_ctrl_info(ctrl_name, &current_volume);
	if (ret)
		return ret;

	if (current_volume < 8)
		current_volume = 0;
	else
		current_volume -= 8;

	ret = _set_bluealsa_plugin_volume_ctrl_info(ctrl_name, current_volume);
	return ret;
}

int rk_bt_sink_set_volume(int volume)
{
	char ctrl_name[128] = {0};
	int new_volume = 0;
	int ret = 0;

	if (!bt_sink_is_open()) {
		pr_info("bt sink isn't open, please open\n");
		return -1;
	}

	if (volume < 0)
		new_volume = 0;
	else if (volume > 127)
		new_volume = 127;
	else
		new_volume = volume;

	ret = _get_bluealsa_plugin_volume_ctrl_info(ctrl_name, NULL);
	if (ret)
		return ret;

	ret = _set_bluealsa_plugin_volume_ctrl_info(ctrl_name, new_volume);
	return ret;
}

void rk_bt_sink_set_alsa_device(char *alsa_dev)
{
	pr_info("bluez don't support %s\n", __func__);
}

/*****************************************************************
 *            Rockchip bluetooth spp api                         *
 *****************************************************************/
int rk_bt_spp_open()
{
	int ret = 0;

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}


	ret = bt_spp_server_open();
	return ret;
}

int rk_bt_spp_register_status_cb(RK_BT_SPP_STATUS_CALLBACK cb)
{
	bt_spp_register_status_callback(cb);
	return 0;
}

int rk_bt_spp_register_recv_cb(RK_BT_SPP_RECV_CALLBACK cb)
{
	bt_spp_register_recv_callback(cb);
	return 0;
}

int rk_bt_spp_close(void)
{
	bt_spp_server_close();
	return 0;
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
	if (bt_is_open()) {
		pr_info("bluetooth has been opened!\n");
		return -1;
	}

	bt_state_send(RK_BT_STATE_TURNING_ON);
	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/var/run/dbus/system_bus_socket", 1);
	if (rk_bt_control(BtControl::BT_OPEN, p_bt_content, sizeof(RkBtContent))) {
		bt_state_send(RK_BT_STATE_OFF);
		return -1;
	}

	return 0;
}

int rk_bt_deinit(void)
{
	if (!bt_is_open()) {
		pr_info("bluetooth has been closed!\n");
		return -1;
	}

	bt_state_send(RK_BT_STATE_TURNING_OFF);
	rk_bt_hfp_close();
	rk_bt_sink_close();
	rk_bt_source_close();
	rk_bt_spp_close();
	rk_ble_stop();
	rk_bt_obex_close();
	bt_close();

	bt_kill_task("bluealsa");
	bt_kill_task("bluealsa-aplay");
	bt_kill_task("bluetoothctl");
	bt_kill_task("bluetoothd");
	bt_exec_command_system("hciconfig hci0 down");
	bt_kill_task("rtk_hciattach");

	sleep(1);
	rk_ble_clean();

	bt_deregister_bond_callback();
	bt_deregister_discovery_callback();
	bt_deregister_dev_found_callback();
	bt_control.is_bt_open = false;

	bt_state_send(RK_BT_STATE_OFF);
	bt_deregister_state_callback();
	return 0;
}

void rk_bt_register_state_callback(RK_BT_STATE_CALLBACK cb)
{
	bt_register_state_callback(cb);
}

void rk_bt_register_bond_callback(RK_BT_BOND_CALLBACK cb)
{
	bt_register_bond_callback(cb);
}

void rk_bt_register_discovery_callback(RK_BT_DISCOVERY_CALLBACK cb)
{
	bt_register_discovery_callback(cb);
}

void rk_bt_register_dev_found_callback(RK_BT_DEV_FOUND_CALLBACK cb)
{
	bt_register_dev_found_callback(cb);
}

/*
 * / # hcitool con
 * Connections:
 *      > ACL 64:A2:F9:68:1E:7E handle 1 state 1 lm SLAVE AUTH ENCRYPT
 *      > LE 60:9C:59:31:7F:B9 handle 16 state 1 lm SLAVE
 */
int rk_bt_is_connected(void)
{
	return bt_is_connected();
}

int rk_bt_set_class(int value)
{
	char cmd[100] = {0};

	pr_info("#%s value:0x%x\n", __func__, value);
	sprintf(cmd, "hciconfig hci0 class 0x%x", value);
	RK_shell_system(cmd);
	msleep(100);

	return 0;
}

int rk_bt_set_sleep_mode()
{
	pr_info("bluez don't support %s\n", __func__);
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

int rk_bt_start_discovery(unsigned int mseconds)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_start_discovery(mseconds);
}

int rk_bt_cancel_discovery()
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_cancel_discovery(RK_BT_DISC_STOPPED_BY_USER);
}

bool rk_bt_is_discovering()
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return false;
	}

	return bt_is_discovering();
}

void rk_bt_display_devices()
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return;
	}

	bt_display_devices();
}

void rk_bt_display_paired_devices()
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return;
	}

	bt_display_paired_devices();
}

int rk_bt_pair_by_addr(char *addr)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return pair_by_addr(addr);
}

int rk_bt_unpair_by_addr(char *addr)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return unpair_by_addr(addr);
}

int rk_bt_set_device_name(char *name)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_set_device_name(name);
}

int rk_bt_get_device_name(char *name, int len)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_get_device_name(name, len);
}

int rk_bt_get_device_addr(char *addr, int len)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_get_device_addr(addr, len);
}

int rk_bt_get_paired_devices(RkBtPraiedDevice **dev_list, int *count)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_get_paired_devices(dev_list, count);
}

int rk_bt_free_paired_devices(RkBtPraiedDevice *dev_list)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_free_paired_devices(dev_list);
}

int rk_bt_get_playrole_by_addr(char *addr)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	return bt_get_playrole_by_addr(addr);
}
/*****************************************************************
 *            Rockchip bluetooth hfp-hf api                        *
 *****************************************************************/
static int g_ba_hfp_client = -1;

void rk_bt_hfp_register_callback(RK_BT_HFP_CALLBACK cb)
{
	rfcomm_hfp_hf_regist_cb(cb);
}

int rk_bt_hfp_open()
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if (bt_hfp_is_open()) {
		RK_LOGI("bt hfp has already been opened!!!\n");
		return -1;
	}

	if (bt_source_is_open()) {
		RK_LOGE("bt hfp isn't coexist with source!!!\n");
		bt_close_source();
	}

	if (bt_interface(BtControl::BT_HFP_OPEN, NULL) < 0) {
		bt_control.is_hfp_open = false;
		bt_control.type = BtControlType::BT_NONE;
		return -1;
	}

	g_ba_hfp_client = bluealsa_open("hci0");
	if (g_ba_hfp_client < 0) {
		RK_LOGE("bt hfp connect to bluealsa server failed!");
		return -1;
	}

	rfcomm_listen_ba_msg_start();
	bt_control.is_hfp_open = true;
	/* Set bluetooth control current type */
	bt_control.type = BtControlType::BT_HFP_HF;
	bt_control.last_type = BtControlType::BT_HFP_HF;

	reconn_last_devices(BT_DEVICES_HFP);

	return 0;
}

int rk_bt_hfp_sink_open(void)
{
	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
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
		bt_control.is_a2dp_sink_open = false;
		bt_control.is_hfp_open = false;
		bt_control.type = BtControlType::BT_NONE;
		return -1;
	}

	g_ba_hfp_client = bluealsa_open("hci0");
	if (g_ba_hfp_client < 0) {
		RK_LOGE("bt hfp connect to bluealsa server failed!");
		return -1;
	}

	rfcomm_listen_ba_msg_start();

	bt_control.is_a2dp_sink_open = true;
	bt_control.is_hfp_open = true;
	/* Set bluetooth control current type */
	bt_control.type = BtControlType::BT_SINK_HFP_MODE;
	bt_control.last_type = BtControlType::BT_SINK_HFP_MODE;

	return 0;
}

int rk_bt_hfp_close(void)
{
	rfcomm_listen_ba_msg_stop();
	if (g_ba_hfp_client >= 0) {
		shutdown(g_ba_hfp_client, SHUT_RDWR);
		g_ba_hfp_client = -1;
	}

	if (!bt_hfp_is_open())
		return -1;

	bt_control.is_hfp_open = false;
	rfcomm_hfp_send_event(RK_BT_HFP_DISCONNECT_EVT, NULL);
	if (bt_control.type == BtControlType::BT_HFP_HF) {
		bt_control.type = BtControlType::BT_NONE;
		bt_control.last_type = BtControlType::BT_NONE;
	}

	if (bt_sink_is_open()) {
		bt_control.type = BtControlType::BT_SINK;
		bt_control.last_type = BtControlType::BT_SINK;
		return 0;
	}

	if(!disconnect_current_devices())
		sleep(3);

	system("hciconfig hci0 noscan");
	system("killall bluealsa-aplay");
	system("killall bluealsa");

	rfcomm_hfp_hf_regist_cb(NULL);
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
	if ((start = strstr(result_buf, "ACL ")) != NULL) {
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
	if(rk_bt_hfp_hp_send_cmd("ATA")) {
		pr_info("%s: send ATA cmd error\n", __func__);
		return -1;
	}

	return 0;
}

int rk_bt_hfp_hangup(void)
{
	if(rk_bt_hfp_hp_send_cmd("AT+CHUP")) {
		pr_info("%s: send AT+CHUP cmd error\n", __func__);
		return -1;
	}

	return 0;
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
		pr_info("ERROR: Invalid value, should within [0, 9]\n");
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

void rk_bt_hfp_enable_cvsd(void)
{
	//for compile
	pr_info("bluez don't support %s\n", __func__);
}

void rk_bt_hfp_disable_cvsd(void)
{
	//for compile
	pr_info("bluez don't support %s\n", __func__);
}

int rk_bt_hfp_disconnect()
{
	return disconnect_current_devices();
}

static pthread_t g_obex_thread = 0;
int rk_bt_obex_init()
{
	char result_buf[256] = {0};
	int ret = 0;

	if (!bt_is_open()) {
		pr_info("%s: Please open bt!!!\n", __func__);
		return -1;
	}

	if(g_obex_thread) {
		pr_info("obex has been initialized\n");
		return -1;
	}

	pr_info("[enter %s]\n", __func__);

	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/var/run/dbus/system_bus_socket", 1);
	usleep(6000);
	system("usr/libexec/bluetooth/obexd -d -n -l -a -r /data/ &");

	/* Create thread to do connect task. */
	if (pthread_create(&g_obex_thread, NULL, obex_main_thread, NULL)) {
		pr_info("obex_main_thread thread create failed!\n");
		return -1;
	}

	pthread_setname_np(g_obex_thread, "obex_main_thread");
	return 0;
}

int rk_bt_obex_pbap_connect(char *btaddr)
{
	if(!g_obex_thread) {
		pr_err("obex don't inited, please init\n");
		return -1;
	}

	if (!btaddr || (strlen(btaddr) < 17)) {
		pr_err("%s: Invalid address\n", __func__);
		return -1;
	}

	pr_info("[enter %s]\n", __func__);
	obex_connect_pbap(btaddr);

	return 0;
}

int rk_bt_obex_pbap_get_vcf(char *dir_name, char *dir_file)
{
	if(!g_obex_thread) {
		pr_err("obex don't inited, please init\n");
		return -1;
	}

	if (!dir_name || !dir_file) {
		pr_err("%s: Invalid dir_name or dir_file\n", __func__);
		return -1;
	}

	pr_info("[enter %s]\n", __func__);
	obex_get_pbap_pb(dir_name, dir_file);

	return 0;
}

int rk_bt_obex_pbap_disconnect(char *btaddr)
{
	if(!g_obex_thread) {
		pr_err("obex don't inited, please init\n");
		return -1;
	}

	pr_info("[enter %s]\n", __func__);
	obex_disconnect(1, NULL);
	return 0;
}

int rk_bt_obex_close()
{
	if(!g_obex_thread) {
		pr_info("obex has been closed\n");
		return -1;
	}

	pr_info("[enter %s]\n", __func__);
	obex_quit();

	pthread_join(g_obex_thread, NULL);
	g_obex_thread = 0;

	bt_kill_task("obexd");
	pr_info("[exit %s]\n", __func__);

	return 0;
}
