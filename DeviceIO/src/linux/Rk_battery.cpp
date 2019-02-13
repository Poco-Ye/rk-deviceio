#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "DeviceIo/Rk_battery.h"
#include <sys/prctl.h>

static void *m_userdata;
static RK_battery_callback m_cb;
static int m_running = 0;
static RK_Battery_Status_e m_status = RK_BATTERY_STATUS_UNKNOWN;

static int exec(const char *cmd, char *buf) {
	FILE *stream = NULL;
	char tmp[1024];

	if ((stream = popen(cmd,"r")) == NULL) {
		return -1;
	}

	while (fgets(tmp, sizeof(tmp) -1, stream)) {
		strcat(buf, tmp);
	}
	pclose(stream);

	return 0;
}

static void* thread_detect_battery_status(void *arg)
{
	int cur_level, last_level;
	RK_Battery_Status_e status = RK_BATTERY_STATUS_UNKNOWN;
	time_t cb_time = 0;

	prctl(PR_SET_NAME,"thread_detect_battery_status");

	status = RK_battery_get_status();
	last_level = RK_battery_get_cur_level();
	m_status = status;

	while (m_running) {
		sleep(1);
		cur_level = RK_battery_get_cur_level();
		status = RK_battery_get_status();
		if (m_status != status) {
			m_status = status;
			if (m_cb != NULL)
				m_cb(m_userdata, RK_BATTERY_STATUS, status);
		}

		if (cb_time != 0 && difftime(time(NULL), cb_time) < 60) {
			continue;
		}

		// Charging
		if ((cur_level == 100 && last_level != 100) ||
				(cur_level >= 80 && last_level < 80) ||
				(cur_level >= 50 && last_level < 50) ||
				(cur_level >= 20 && last_level < 20) ||
				(cur_level >= 10 && last_level < 10)) {
			cb_time = time(NULL);
			if (m_cb != NULL)
				m_cb(m_userdata, RK_BATTERY_LEVEL, cur_level);
		}

		// Discharging
		if ((cur_level <= 80 && last_level > 80) ||
				(cur_level <= 50 && last_level > 50) ||
				(cur_level <= 20 && last_level > 20) ||
				(cur_level <= 10 && last_level > 10)) {
			cb_time = time(NULL);
			if (m_cb != NULL)
				m_cb(m_userdata, RK_BATTERY_LEVEL, cur_level);
		}
		last_level = cur_level;
	}

	return NULL;
}

int RK_battery_init(void)
{
	int ret;
	pthread_t pth;

	if (m_running)
		return 0;

	m_running = 1;
	ret = pthread_create(&pth, NULL, thread_detect_battery_status, NULL);

	if (0 != ret)
		m_running = 0;

	return ret;
}

int RK_battery_register_callback(void *userdata, RK_battery_callback cb)
{
	m_userdata = userdata;
	m_cb = cb;

	return 0;
}

int RK_battery_get_cur_level()
{
	int ret, level;
	char buf[64];

	memset(buf, 0, sizeof(buf));
	ret = exec("cat /sys/class/power_supply/battery/capacity", buf);
	if (0 != ret)
		return 0;

	level = atoi(buf);

	return level;
}

RK_Battery_Status_e RK_battery_get_status()
{
	int ret;
	char buf[64];

	memset(buf, 0, sizeof(buf));
	ret = exec("cat /sys/class/power_supply/battery/status", buf);
	if (0 != ret)
		return RK_BATTERY_STATUS_UNKNOWN;

	if (0 == strncmp(buf, "Discharging", 11) || 0 == strncmp(buf, "Not charging", 12))
		return RK_BATTERY_STATUS_DISCHARGING;
	else if (0 == strncmp(buf, "Full", 4))
		return RK_BATTERY_STATUS_FULL;
	else if (0 == strncmp(buf, "Charging", 8))
		return RK_BATTERY_STATUS_CHARGING;
	return RK_BATTERY_STATUS_UNKNOWN;
}
