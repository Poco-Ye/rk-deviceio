#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "DeviceIo/Rk_led.h"


#define LED_PWD_R			"/sys/devices/platform/pwmleds/leds/PWM-R/brightness"
#define LED_PWD_G			"/sys/devices/platform/pwmleds/leds/PWM-G/brightness"
#define LED_PWD_B			"/sys/devices/platform/pwmleds/leds/PWM-B/brightness"

int RK_set_all_led_status(const int Rval, const int Gval, const int Bval)
{
	char cmd[64];

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "echo %d > %s", Rval, LED_PWD_R);
	system(cmd);

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "echo %d > %s", Gval, LED_PWD_G);
	system(cmd);

	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "echo %d > %s", Bval, LED_PWD_B);
	system(cmd);

	return 0;
}

int RK_set_all_led_off()
{
	return RK_set_all_led_status(0x00, 0x00, 0x00);
}
