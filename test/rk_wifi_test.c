#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>

#include <DeviceIo/Rk_wifi.h>

struct wifi_info {
	char ssid[512];
	char psk[512];
};

/* rk wifi airkiss */
static RK_WIFI_RUNNING_State_e airkiss_wifi_state = 0;
static int rk_wifi_airkiss_state_callback(RK_WIFI_RUNNING_State_e state)
{
	printf("[RK_AIRKISS] %s state: %d\n", __func__, state);
	airkiss_wifi_state = state;
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

	info = (struct wifi_info *) arg;
	RK_wifi_register_callback(rk_wifi_airkiss_state_callback);
	RK_wifi_connect(info->ssid, info->psk);

	return NULL;
}

void rk_wifi_airkiss()
{
	int err  = 0;
	struct wifi_info info;
	pthread_t tid = 0;

	memset(&info, 0, sizeof(struct wifi_info));

	printf("===== %s =====\n", __func__);

	if(RK_wifi_airkiss_config(info.ssid, info.psk) < 0)
		return;

	err = pthread_create(&tid, NULL, rk_wifi_config_thread, &info);
	if (err) {
		printf("Error - pthread_create() return code: %d\n", err);
		return;
	}

	while (!airkiss_wifi_state)
		sleep(1);
}
