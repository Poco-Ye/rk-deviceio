/*
 * Copyright (c) 2018 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <fcntl.h>
#include <linux/input.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include "Logger.h"
#include "DeviceIo/DeviceIo.h"
#include "shell.h"
#include "power.h"

#define pm_dbg(fmt, ...)	APP_DEBUG("PowerManager debug: " fmt, ##__VA_ARGS__)
#define pm_info(fmt, ...)	APP_INFO("PowerManager info: " fmt, ##__VA_ARGS__)
#define pm_err(fmt, ...)	APP_ERROR("PowerManager error: " fmt, ##__VA_ARGS__)

#define	CMD_SIZE	2048

using DeviceIOFramework::DevicePowerSupply;

struct SysfsItem {
    const char* str;
    DevicePowerSupply val;
};

struct power_manager {
	unsigned int low_power_threshold;
	unsigned int capacity_detect_period;

	pthread_t tid;
};

static struct power_manager pm_g;

static const char* mapSysfsString(DevicePowerSupply cmd,
                          struct SysfsItem map[]) {
    for (int i = 0; map[i].str; i++)
        if (cmd == map[i].val)
            return map[i].str;

    return NULL;
}

static int get_item_value(const char *path, char *buffer)
{
    int ret = -1;
	char read_cmd[CMD_SIZE] = { 0 };

	sprintf(read_cmd, "cat %s ", path);
    ret = Shell::exec(read_cmd, buffer);

    return 0;
}

static int usb_get_value(const char *cmd, char *data)
{
	char full_path[CMD_SIZE] = { 0 };
	int ret = -1;

	memset(full_path, 0, sizeof(full_path));
	sprintf(full_path, " /%s/%s/%s ", DEVICEIO_POWER_DIRECTORY_PATH,
			DEVICEIO_POWER_TYPE_USB, cmd);

	ret = get_item_value(full_path, data);
    if (ret) {
		pm_err("[%s:%d] Get item [%s] value error.\n",
				__func__, __LINE__, full_path);
        return -1;
    }

    return 0;
}

static int ac_get_value(const char *cmd, char *data)
{
	char full_path[CMD_SIZE] = { 0 };
	int ret = -1;

	memset(full_path, 0, sizeof(full_path));
	sprintf(full_path, " /%s/%s/%s ", DEVICEIO_POWER_DIRECTORY_PATH,
			DEVICEIO_POWER_TYPE_AC, cmd);

	ret = get_item_value(full_path, data);
    if (ret) {
		pm_err("[%s:%d] Get item [%s] value error.\n",
				__func__, __LINE__, full_path);
        return -1;
    }

    return 0;
}

static int battery_get_value(const char *cmd, char *data)
{
	char full_path[CMD_SIZE] = { 0 };
	int ret = -1;

	memset(full_path, 0, sizeof(full_path));
	sprintf(full_path, " %s/%s/%s ", DEVICEIO_POWER_DIRECTORY_PATH,
			DEVICEIO_POWER_TYPE_BATTERY, cmd);

	ret = get_item_value(full_path, data);
    if (ret){
		pm_err("[%s:%d] Get item [%s] value error.\n",
				__func__, __LINE__, full_path);
        return -1;
    }

    return 0;
}

int str2int(const char *buf)
{
	int count = 0;
	for (count=0; count<strlen(buf)-1; count++) {
		if (!isdigit(buf[count])) {
			pm_info("found is string\n");
			return -1;
		}
	}

	return atoi(buf);
}

static void* pm_poweroff_thread(void* arg)
{
	char buffer[CMD_SIZE] = {0};
    sync();
	pm_info("PowerOff!\n");
    Shell::exec("poweroff", buffer);
    pthread_exit(NULL);
}

void pm_poweroff(void)
{
    pthread_t tid;

    if (pthread_create(&tid, NULL, pm_poweroff_thread, NULL) != 0)
		pm_err("Create poweroff thread error!\n");
}

int power_set_capacity_period_detect(unsigned int period)
{
	if (period) {
		pm_g.capacity_detect_period = period;
	} else {
		pm_err("Power Capacity Detect Period Is Invalid.\n");
		return -1;
	}
	return 0;
}

int power_set_low_power_threshold(unsigned int threshold_val)
{
	if (threshold_val) {
		pm_g.low_power_threshold = threshold_val;
	} else {
		pm_err("Power Low Threshold Is Invalid.\n");
		return -1;
	}
	return 0;
}		/* -----  end of function power_set_LOW_POWER_THRESHOLD  ----- */

int power_supply_control(DevicePowerSupply cmd, void *data, int len)
{
    int ret = -1;

	struct SysfsItem usb_item_map[] = {
		{"type", DevicePowerSupply::USB_TYPE},
		{"online", DevicePowerSupply::USB_ONLINE},
		{"voltage_max", DevicePowerSupply::USB_VOLTAGE_MAX},
		{"current_max", DevicePowerSupply::USB_CURRENT_MAX},
		{ NULL, DevicePowerSupply::NULL_DEVICEPOWERSUPPLY },
	};

	struct SysfsItem ac_item_map[] = {
		{"type", DevicePowerSupply::AC_TYPE},
		{"online", DevicePowerSupply::AC_ONLINE},
		{"voltage_max", DevicePowerSupply::AC_VOLTAGE_MAX},
		{"current_max", DevicePowerSupply::AC_CURRENT_MAX},
		{ NULL, DevicePowerSupply::NULL_DEVICEPOWERSUPPLY },
	};

	struct SysfsItem battery_item_map[] = {
		{"capacity", DevicePowerSupply::BATTERY_CAPACITY},
		{"status", DevicePowerSupply::BATTERY_STATUS},
		{"type", DevicePowerSupply::BATTERY_TYPE},
		{"health", DevicePowerSupply::BATTERY_HEALTH},
		{"temp", DevicePowerSupply::BATTERY_TEMP},
		{"present", DevicePowerSupply::BATTERY_PRESENT},
		{"voltage_now", DevicePowerSupply::BATTERY_VOLTAGE_NOW},
		{"current_now", DevicePowerSupply::BATTERY_CURRENT_NOW},
		{"charge_counter", DevicePowerSupply::BATTERY_CHARGE_COUNTER},
		{ NULL, DevicePowerSupply::NULL_DEVICEPOWERSUPPLY },
	};

	switch (cmd) {
    case DevicePowerSupply::BATTERY_CAPACITY:
    case DevicePowerSupply::BATTERY_STATUS:
    case DevicePowerSupply::BATTERY_TYPE:
    case DevicePowerSupply::BATTERY_HEALTH:
    case DevicePowerSupply::BATTERY_TEMP:
    case DevicePowerSupply::BATTERY_PRESENT:
    case DevicePowerSupply::BATTERY_VOLTAGE_NOW:
    case DevicePowerSupply::BATTERY_CURRENT_NOW:
    case DevicePowerSupply::BATTERY_CHARGE_COUNTER:
		ret = battery_get_value(mapSysfsString(cmd, battery_item_map), (char *)data);
		pm_info("%s:%d data %s\n", __func__, __LINE__, data);
        break;

	case DevicePowerSupply::USB_TYPE:
	case DevicePowerSupply::USB_VOLTAGE_MAX:
	case DevicePowerSupply::USB_CURRENT_MAX:
	case DevicePowerSupply::USB_ONLINE:
		ret = usb_get_value(mapSysfsString(cmd, usb_item_map), (char *)data);
		pm_info("%s:%d data %s\n", __func__, __LINE__, data);
		break;

	case DevicePowerSupply::AC_TYPE:
	case DevicePowerSupply::AC_VOLTAGE_MAX:
	case DevicePowerSupply::AC_CURRENT_MAX:
	case DevicePowerSupply::AC_ONLINE:
		ret = ac_get_value(mapSysfsString(cmd, ac_item_map), (char *)data);
		pm_info("%s:%d data %s\n", __func__, __LINE__, data);
		break;

	case DevicePowerSupply::POWER_CFG_CAPACITY_DETECT_PERIOD:
		ret = power_set_capacity_period_detect(*((unsigned int *)data));
		break;
	case DevicePowerSupply::POWER_CFG_LOW_POWER_THRESHOLD:
		ret = power_set_low_power_threshold(*((unsigned int *)data));
		break;

		default:
			pm_info("[%s:%d] cmd [%d] Not Found.\n", __func__, __LINE__, cmd);
			break;
	}

	return ret;
}

static void* power_task(void *param) {
#define	CAPACITY_BUFFER_SIZE	(8)
#define	STATUS_BUFFER_SIZE	(32)
	char capacity_buf[CAPACITY_BUFFER_SIZE] = {0};
	char status_buf[STATUS_BUFFER_SIZE] = {0};
	unsigned int capacity_level = 0;

    while (1) {
		int ret = 0;
		struct timeval timeout;

		timeout.tv_sec  = pm_g.capacity_detect_period;
		timeout.tv_usec = 0;

		ret = select(1, NULL, NULL, NULL, &timeout);
		if (ret == 0) {
			ret = power_supply_control(DevicePowerSupply::BATTERY_CAPACITY,
					capacity_buf, sizeof(capacity_buf));
			if (ret) {
				pm_err("[%s:%d] read capacity error.\n", __func__, __LINE__);
				continue;
			}
			ret = power_supply_control(DevicePowerSupply::BATTERY_STATUS,
					status_buf, sizeof(status_buf));
			if (ret) {
				pm_err("%s:%d read battery status error.\n", __func__, __LINE__);
				continue;
			}
			// pm_info("capacity_buf [%s]\n", capacity_buf);
			capacity_level = str2int(capacity_buf);
			// pm_info("Battery capacity is %d.\n", capacity_level);

			if (capacity_level < pm_g.low_power_threshold) {
				pm_info("Battery capacity is lower then %d\n",
						pm_g.low_power_threshold);
				if (strncmp(BATTERY_PROP_STATUS_CHARGING, status_buf,
						strlen(BATTERY_PROP_STATUS_CHARGING))) {
					pm_poweroff();
				} else {
					pm_info("But Battery Status is %s\n",
							status_buf);
				}
			} else {
				pm_info("Battery capacity is %d and status is %s\n",
						capacity_level,
						status_buf);
			}

			memset(capacity_buf, 0, CAPACITY_BUFFER_SIZE);
		}
    }
}

int power_init(void)
{
    int ret = 0;

    memset(&pm_g, 0x00, sizeof(pm_g));

	pm_g.low_power_threshold = DEFAULT_LOW_POWER_THRESHOLD;
	pm_g.capacity_detect_period = DEFAULT_CAPACITY_DETECT_PERIOD;

    ret = pthread_create(&pm_g.tid, NULL, power_task, NULL);
    if (ret) {
		pm_err("[%s:%d] create thread fail.\n", __func__, __LINE__);
        return -1;
    }

    return 0;
}

int power_deinit(void)
{
	if (pm_g.tid) {
		pthread_cancel(pm_g.tid);
		pthread_join(pm_g.tid, NULL);
		pm_g.tid = 0;
	}
	return 0;
}
