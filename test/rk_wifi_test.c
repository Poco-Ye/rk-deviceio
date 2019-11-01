#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>

#include "DeviceIo/Rk_wifi.h"
#include "DeviceIo/Rk_softap.h"

struct wifi_info {
	int ssid_len;
	char ssid[512];
	int psk_len;
	char psk[512];
};

/*****************************************************************
 *                     wifi config                               *
 *****************************************************************/
static RK_WIFI_RUNNING_State_e wifi_state = 0;
static int rk_wifi_state_callback(RK_WIFI_RUNNING_State_e state)
{
	printf("%s state: %d\n", __func__, state);
	wifi_state = state;
	if (state == RK_WIFI_State_CONNECTED) {
		printf("RK_WIFI_State_CONNECTED\n");
	} else if (state == RK_WIFI_State_CONNECTFAILED) {
		printf("RK_WIFI_State_CONNECTFAILED\n");
	} else if (state == RK_WIFI_State_CONNECTFAILED_WRONG_KEY) {
		printf("RK_WIFI_State_CONNECTFAILED_WRONG_KEY\n");
	} else if (state == RK_WIFI_State_OPEN) {
		printf("RK_WIFI_State_OPEN\n");
	} else if (state == RK_WIFI_State_OFF) {
		printf("RK_WIFI_State_OFF\n");
	} else if (state == RK_WIFI_State_DISCONNECTED) {
		printf("RK_WIFI_State_DISCONNECTED\n");
	} else if (state == RK_WIFI_State_SCAN_RESULTS) {
		char *scan_r;
		printf("RK_WIFI_State_SCAN_RESULTS\n");
		scan_r = RK_wifi_scan_r();
		printf("%s\n", scan_r);
	}

	return 0;
}

static void *rk_wifi_config_thread(void *arg)
{
	struct wifi_info *info;

	printf("rk_config_wifi_thread\n");

	prctl(PR_SET_NAME,"rk_config_wifi_thread");

	wifi_state = 0;

	info = (struct wifi_info *) arg;
	RK_wifi_register_callback(rk_wifi_state_callback);
	RK_wifi_connect(info->ssid, info->psk);

	printf("Exit wifi config thread\n");
	return NULL;
}

/*****************************************************************
 *                     airkiss wifi config test                  *
 *****************************************************************/
void rk_wifi_airkiss_start(void *data)
{
	int err  = 0;
	struct wifi_info info;
	pthread_t tid = 0;

	memset(&info, 0, sizeof(struct wifi_info));

	printf("===== %s =====\n", __func__);

	if(RK_wifi_airkiss_start(info.ssid, info.psk) < 0)
		return;

	wifi_state = 0;

	err = pthread_create(&tid, NULL, rk_wifi_config_thread, &info);
	if (err) {
		printf("Error - pthread_create() return code: %d\n", err);
		return;
	}

	while (!wifi_state)
		sleep(1);
}

void rk_wifi_airkiss_stop(void *data)
{
	RK_wifi_airkiss_stop();
}

/*****************************************************************
 *                     softap wifi config test                   *
 *****************************************************************/
static int rk_wifi_softap_state_callback(RK_SOFTAP_STATE state, const char* data)
{
	switch (state) {
	case RK_SOFTAP_STATE_CONNECTTING:
		printf("RK_SOFTAP_STATE_CONNECTTING\n");
		break;
	case RK_SOFTAP_STATE_DISCONNECT:
		printf("RK_SOFTAP_STATE_DISCONNECT\n");
		break;
	case RK_SOFTAP_STATE_FAIL:
		printf("RK_SOFTAP_STATE_FAIL\n");
		break;
	case RK_SOFTAP_STATE_SUCCESS:
		printf("RK_SOFTAP_STATE_SUCCESS\n");
		break;
	default:
		break;
	}

	return 0;
}

void rk_wifi_softap_start(void *data)
{
	RK_softap_register_callback(rk_wifi_softap_state_callback);
	if (0 != RK_softap_start("Rockchip-SoftAp", RK_SOFTAP_TCP_SERVER)) {
		return;
	}
}

void rk_wifi_softap_stop(void *data)
{
	RK_softap_stop();
}

void rk_wifi_open(void *data)
{
	RK_wifi_register_callback(rk_wifi_state_callback);
	if (RK_wifi_enable(1) < 0) {
		printf("RK_wifi_enable 1 fail!\n");
	}
}

void rk_wifi_close(void *data)
{
	if (RK_wifi_enable(0) < 0) {
		printf("RK_wifi_enable 0 fail!\n");
	}
}

void rk_wifi_connect(void *data)
{
	if (RK_wifi_connect("NETGEAR75", "huskymint860") < 0) {
		printf("RK_wifi_connect1 fail!\n");
	}
}

void rk_wifi_connect1(void *data)
{
	if (RK_wifi_connect("fish1", "rk12345678") < 0) {
		printf("RK_wifi_connect1 fail!\n");
	}
}

void rk_wifi_ping(void *data)
{
	if (RK_wifi_ping("www.baidu.com") < 0) {
		printf("RK_wifi_ping fail!\n");
	}
}

void rk_wifi_scan(void *data)
{
	if (RK_wifi_scan() < 0) {
		printf("RK_wifi_scan fail!\n");
	}
}

void rk_wifi_getSavedInfo(void *data)
{
	RK_WIFI_SAVED_INFO wsi;

	RK_wifi_getSavedInfo(&wsi);

	for (int i = 0; i < wsi.count; i++) {
		printf("id: %d, name: %s, bssid: %s, state: %s\n",
					wsi.save_info[i].id,
					wsi.save_info[i].ssid,
					wsi.save_info[i].bssid,
					wsi.save_info[i].state);

	}
}

void rk_wifi_connect_with_bssid(void *data)
{
	if (RK_wifi_connect_with_bssid("dc:ef:09:a7:77:53") < 0) {
		printf("RK_wifi_connect_with_bssid fail!\n");
	}
}

void rk_wifi_cancel(void *data)
{
	if (RK_wifi_cancel() < 0) {
		printf("RK_wifi_cancel fail!\n");
	}
}

void rk_wifi_forget_with_bssid(void *data)
{
	if (RK_wifi_forget_with_bssid("dc:ef:09:a7:77:53") < 0) {
		printf("rk_wifi_forget_with_bssid fail!\n");
	}
}
