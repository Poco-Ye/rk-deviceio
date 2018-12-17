#include <stdio.h>
#include <string.h>
#include "DeviceIo/Rk_system.h"


static int exec(const char *cmd, char *buf, const size_t size) {
	FILE *stream = NULL;
	char tmp[1024];

	if ((stream = popen(cmd,"r")) == NULL) {
		return -1;
	}

	if (buf == NULL) {
		pclose(stream);
		return -2;
	}

	buf[0] = '\0';
	while (fgets(tmp, sizeof(tmp) -1, stream)) {
		if (strlen(buf) + strlen(tmp) >= size) {
			pclose(stream);
			return -3;
		}
		strcat(buf, tmp);
	}
	pclose(stream);

	return 0;
}

int RK_read_chip_id(char *buffer, const int size)
{
	int ret;

	ret = exec("cat /proc/cpuinfo | grep Serial | awk -F ': ' '{printf $2}'", buffer, size);

	return ret;
}
