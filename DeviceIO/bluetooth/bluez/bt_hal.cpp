#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>

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
#include "../utility/utility.h"

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

int rk_ble_setup(RkBleContent *ble_content)
{
	rk_bt_control(BtControl::BT_BLE_SETUP, NULL, 0);

	return 0;
}

int rk_ble_clean(void)
{
	rk_bt_control(BtControl::BT_BLE_CLEAN, NULL, 0);
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
	if (!bt_control.is_a2dp_source_open)
		return 0;

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
static int g_sink_volume_sockfd;
static RK_BT_SINK_VOLUME_CALLBACK g_sink_volume_cb;

/* Get volume event frome bluealsa thread */
void *thread_get_ba_volume(void *arg)
{
	int ret = 0;
	char buff[100] = {0};
	struct sockaddr_un clientAddr;
	struct sockaddr_un serverAddr;
	socklen_t addr_len;
	int value = 0;
	char *start = NULL;

	g_sink_volume_sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_sink_volume_sockfd < 0) {
		printf("Create socket failed!\n");
		return NULL;
	}

	serverAddr.sun_family = AF_UNIX;
	strcpy(serverAddr.sun_path, "/tmp/rk_deviceio_a2dp_volume");

	system("rm -rf /tmp/rk_deviceio_a2dp_volume");
	ret = bind(g_sink_volume_sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	if (ret < 0) {
		printf("Bind Local addr failed!\n");
		return NULL;
	}

	printf("###### FUCN:%s start!\n", __func__);
	while(1) {
		memset(buff, 0, sizeof(buff));
		ret = recvfrom(g_sink_volume_sockfd, buff, sizeof(buff), 0, (struct sockaddr *)&clientAddr, &addr_len);
		if (ret <= 0) {
			if (ret == 0)
				printf("###### FUCN:%s. socket closed!\n", __func__);
			break;
		}
		printf("###### FUCN:%s. Received a malformed message(%s)\n", __func__, buff);

		if (!bt_sink_is_open())
			break;

		start = strstr(buff, "a2dp volume:");
		if (!start) {
			printf("WARNING: %s recved unsupport msg:%s\n", __func__, buff);
			continue;
		}
		start += strlen("a2dp volume:");
		value = (*start - '0') * 100 + (*(start + 1) - '0') * 10 + (*(start +2) - '0');

		if (g_sink_volume_cb)
			g_sink_volume_cb(value);
		else
			break;
	}

	if (g_sink_volume_sockfd > 0) {
		close(g_sink_volume_sockfd);
		g_sink_volume_sockfd = 0;
	}

	printf("###### FUCN:%s exit!\n", __func__);
	return NULL;
}

static int a2dp_sink_listen_ba_volume_start()
{
	pthread_t tid;

	/* Create a thread to listen for Bluezalsa volume changes. */
	if (g_sink_volume_sockfd == 0)
		pthread_create(&tid, NULL, thread_get_ba_volume, NULL);

	return 0;
}

static int a2dp_sink_listen_ba_volume_stop()
{
	if (g_sink_volume_sockfd > 0) {
		close(g_sink_volume_sockfd);
		g_sink_volume_sockfd = 0;
	}
	/* wait for  thread_get_ba_msg exit */
	usleep(200);
	return 0;
}

int rk_bt_sink_register_callback(RK_BT_SINK_CALLBACK cb)
{
	a2dp_sink_register_cb(cb);
	return 0;
}

int rk_bt_sink_register_volume_callback(RK_BT_SINK_VOLUME_CALLBACK cb)
{
	g_sink_volume_cb = cb;
	a2dp_sink_listen_ba_volume_start();
	return 0;
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

	reconn_last_devices(BT_DEVICES_A2DP_SOURCE);

	bt_control.is_a2dp_sink_open = 1;
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
	bt_close_sink();
	a2dp_sink_listen_ba_volume_stop();
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
	return disconnect_current_devices();
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

/*****************************************************************
 *            Rockchip bluetooth spp api                         *
 *****************************************************************/
int rk_bt_spp_open()
{
	int ret = 0;

	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

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
	int wait_cnt = 3;

	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/var/run/dbus/system_bus_socket", 1);
	if (rk_bt_control(BtControl::BT_OPEN, p_bt_content, sizeof(RkBtContent)))
		return -1;

	return 0;
}

int rk_bt_deinit(void)
{
#if 1
	rk_bt_hfp_close();
	//rk_bt_sink_close();
	rk_bt_source_close();
	rk_bt_spp_close();
	rk_ble_stop();
	bt_close();

	bt_kill_task("bluealsa");
	bt_kill_task("bluealsa-aplay");
	bt_kill_task("bluetoothctl");
	bt_kill_task("bluetoothd");

	sleep(1);
	rk_ble_clean();

	return 0;
#else
	printf("bluez don't support bt deinit\n");
	return -1;
#endif
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

int rk_bt_hfp_open()
{
	/* Init bluetooth */
	if (!bt_control.is_bt_open) {
		printf("Please open bt!!!\n");
		return -1;
	}

	if (bt_hfp_is_open()) {
		RK_LOGI("bt hfp has already been opened!!!\n");
		return 0;
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

	reconn_last_devices(BT_DEVICES_HFP);

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
	rfcomm_listen_ba_msg_stop();
	if (g_ba_hfp_client > 0) {
		close(g_ba_hfp_client);
		g_ba_hfp_client = 0;
	}

	if (!bt_control.is_hfp_open)
		return 0;

	bt_control.is_hfp_open = 0;
	if (g_hfp_cb)
		g_hfp_cb(RK_BT_HFP_DISCONNECT_EVT, NULL);
	if (bt_control.type == BtControlType::BT_HFP_HF) {
		bt_control.type = BtControlType::BT_NONE;
		bt_control.last_type = BtControlType::BT_NONE;
	}

	if (bt_sink_is_open()) {
		bt_control.type = BtControlType::BT_SINK;
		bt_control.last_type = BtControlType::BT_SINK;
		return 0;
	}

	disconnect_current_devices();
	system("hciconfig hci0 noscan");
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
	if(rk_bt_hfp_hp_send_cmd("ATA")) {
		printf("%s: send ATA cmd error\n", __func__);
		return -1;
	}

	if (g_hfp_cb)
		g_hfp_cb(RK_BT_HFP_PICKUP_EVT, NULL);

	return 0;
}

int rk_bt_hfp_hangup(void)
{
	if(rk_bt_hfp_hp_send_cmd("AT+CHUP")) {
		printf("%s: send AT+CHUP cmd error\n", __func__);
		return -1;
	}

	if (g_hfp_cb)
		g_hfp_cb(RK_BT_HFP_HANGUP_EVT, NULL);

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

void rk_bt_hfp_enable_cvsd(void)
{
	//for compile
}

void rk_bt_hfp_disable_cvsd(void)
{
	//for compile
}

int rk_bt_hfp_disconnect()
{
	return disconnect_current_devices();
}

static pthread_t g_obex_thread;
int rk_bt_obex_init()
{
	char result_buf[256] = {0};
	int ret = 0;

	printf("[enter %s]\n", __func__);

	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/var/run/dbus/system_bus_socket", 1);
	usleep(6000);
	system("usr/libexec/bluetooth/obexd -d -n -l -a -r /data/ &");

	/* Create thread to do connect task. */
	ret = pthread_create(&g_obex_thread, NULL,
						 obex_main_thread, NULL);
	if (ret) {
		printf("obex_main_thread thread create failed!\n");
		return -1;
	} else
		printf("obex_main_thread thread create ok!\n");

	return 0;
}

int rk_bt_obex_pbap_connect(char *btaddr)
{
	printf("[enter %s]\n", __func__);
	obex_connect_pbap(btaddr);

	return 0;
}

int rk_bt_obex_pbap_get_vcf(char *dir_name, char *dir_file)
{
	printf("[enter %s]\n", __func__);
	obex_get_pbap_pb(dir_name, dir_file);

	return 0;
}

int rk_bt_obex_pbap_disconnect(char *btaddr)
{
	printf("[enter %s]\n", __func__);
	obex_disconnect(1, NULL);
	return 0;
}

int rk_bt_obex_close()
{
	char result_buf[256] = {0};

	printf("[enter %s]\n", __func__);
	obex_quit();
	bt_kill_task("obexd");

	return 0;
}
