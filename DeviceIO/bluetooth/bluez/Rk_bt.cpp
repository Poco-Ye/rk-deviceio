#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "DeviceIo/Rk_bt.h"
#include "DeviceIo/RK_log.h"
#include "DeviceIo/Rk_shell.h"

int RK_bt_is_connected(void)
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
