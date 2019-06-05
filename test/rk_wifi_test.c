#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>

#include "DeviceIo/Rk_wifi.h"
#include "DeviceIo/Rk_softap.h"
#include "DeviceIo/Rk_voice_print.h"

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
	printf("[RK_AIRKISS] %s state: %d\n", __func__, state);
	wifi_state = state;
	if (state == RK_WIFI_State_CONNECTED) {
		printf("[RK_AIRKISS] RK_WIFI_State_CONNECTED\n");
	} else if (state == RK_WIFI_State_CONNECTFAILED) {
		printf("[RK_AIRKISS] RK_WIFI_State_CONNECTFAILED\n");
	} else if (state == RK_WIFI_State_CONNECTFAILED_WRONG_KEY) {
		printf("[RK_AIRKISS] RK_WIFI_State_CONNECTFAILED_WRONG_KEY\n");
	}

	return 0;
}

static void *rk_wifi_config_thread(void *arg)
{
	struct wifi_info *info;

	printf("[RK_AIRKISS] rk_config_wifi_thread\n");

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

/*****************************************************************
 *                     voiceprint wifi config test               *
 *****************************************************************/
static void rk_wifi_vp_ssid_psk_callback(char* ssid, char* psk)
{
	int ret;
	pthread_t tid = 0;
	struct wifi_info info;

	memset(&info, 0, sizeof(struct wifi_info));

	info.ssid_len = strlen(ssid);
	info.psk_len = strlen(psk);
	strncpy(info.ssid, ssid, info.ssid_len);
	strncpy(info.psk, psk, info.psk_len);
	printf("%s: ssid_len: %d, ssid: %s, psk_len: %d, psk: %s\n", __func__, info.ssid_len, ssid, info.psk_len, psk);

	ret = pthread_create(&tid, NULL, rk_wifi_config_thread, &info);
	if (ret) {
		printf("Error - pthread_create() return code: %d\n", ret);
		return;
	}
}
