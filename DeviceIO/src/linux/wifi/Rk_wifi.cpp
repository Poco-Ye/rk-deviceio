#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>

#include "Hostapd.h"
#include "ping.h"
#include "DeviceIo/Rk_wifi.h"

static RK_wifi_state_callback m_cb;

static char* exec1(const char* cmd)
{
	if (NULL == cmd || 0 == strlen(cmd))
		return NULL;

	FILE* fp = NULL;
	char buf[128];
	char* ret;
	static int SIZE_UNITE = 512;
	size_t size = SIZE_UNITE;

	fp = popen((const char *) cmd, "r");
	if (NULL == fp)
		return NULL;

	memset(buf, 0, sizeof(buf));
	ret = (char*) malloc(sizeof(char) * size);
	memset(ret, 0, sizeof(char) * size);
	while (NULL != fgets(buf, sizeof(buf)-1, fp)) {
		if (size <= (strlen(ret) + strlen(buf))) {
			size += SIZE_UNITE;
			ret = (char*) realloc(ret, sizeof(char) * size);
		}
		strcat(ret, buf);
	}

	pclose(fp);
	ret = (char*) realloc(ret, sizeof(char) * (strlen(ret) + 1));

	return ret;
}

static char *spec_char_convers(const char *buf, char *dst)
{
	char buf_temp[strlen(buf) + 1];
	int i = 0;
	unsigned long con;

	memset(buf_temp, 0, sizeof(buf_temp));
	while(*buf != '\0') {
		if(*buf == '\\') {
			strcpy(buf_temp, buf);
			*buf_temp = '0';
			*(buf_temp + 4) = '\0';
			con = strtoul(buf_temp, NULL, 16);
			dst[i] = con;
			buf += 3;
		} else {
			dst[i] = *buf;
		}
		i++;
		buf++;
	}
	dst[i] = '\0';
	return dst;
}

static int exec(const char* cmd, const char* ret)
{
	char* tmp;
	tmp = exec1(cmd);

	if (NULL == cmd)
		return -1;

	char convers[strlen(tmp) + 1];

	spec_char_convers(tmp, convers);
	strncpy(ret, convers, strlen(convers) + 1);
	free(tmp);

	return 0;
}

int RK_wifi_register_callback(RK_wifi_state_callback cb)
{
	m_cb = cb;
}

int RK_wifi_running_getState(RK_WIFI_RUNNING_State_e* pState)
{
	int ret;
	char str[128];

	// check wpa is running first
	memset(str, 0, sizeof(str));
	ret = exec("pidof wpa_supplicant", str);
	if (0 == strlen(str)) {
		*pState = RK_WIFI_State_IDLE;
		return ret;
	}

	// check whether wifi connected
	memset(str, 0, sizeof(str));
	ret = exec("wpa_cli -iwlan0 status | grep wpa_state | awk -F '=' '{printf $2}'", str);
	if (0 == strncmp(str, "COMPLETED", 9)) {
		*pState = RK_WIFI_State_CONNECTED;
		return ret;
	} else {
		*pState = RK_WIFI_State_DISCONNECTED;
		return ret;
	}
}

int RK_wifi_running_getConnectionInfo(RK_WIFI_INFO_Connection_s* pInfo)
{
	FILE *fp = NULL;
	char line[64];
	char *value;

	if (pInfo == NULL)
		return -1;

	remove("/tmp/status.tmp");
	system("wpa_cli -iwlan0 status > /tmp/status.tmp");
	fp = fopen("/tmp/status.tmp", "r");
	if (!fp)
		return -1;

	memset(line, 0, sizeof(line));
	while (fgets(line, sizeof(line) - 1, fp)) {
		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = '\0';

		if (0 == strncmp(line, "bssid", 5)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 0) {
				strncpy(pInfo->bssid, value + 1, sizeof(pInfo->bssid));
			}
		} else if (0 == strncmp(line, "freq", 4)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 1) {
				pInfo->freq = atoi(value + 1);
			}
		} else if (0 == strncmp(line, "ssid", 4)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 0) {
				strncpy(pInfo->ssid, value + 1, sizeof(pInfo->ssid));
			}
		} else if (0 == strncmp(line, "id", 2)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 1) {
				pInfo->freq = atoi(value + 1);
			}
		} else if (0 == strncmp(line, "mode", 4)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 0) {
				strncpy(pInfo->mode, value + 1, sizeof(pInfo->mode));
			}
		} else if (0 == strncmp(line, "wpa_state", 9)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 0) {
				strncpy(pInfo->wpa_state, value + 1, sizeof(pInfo->wpa_state));
			}
		} else if (0 == strncmp(line, "ip_address", 10)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 0) {
				strncpy(pInfo->ip_address, value + 1, sizeof(pInfo->ip_address));
			}
		} else if (0 == strncmp(line, "address", 7)) {
			value = strchr(line, '=');
			if (value && strlen(value) > 0) {
				strncpy(pInfo->mac_address, value + 1, sizeof(pInfo->mac_address));
			}
		}
	}
	fclose(fp);
	return 0;
}

int RK_wifi_enable_ap(const char* ssid, const char* psk, const char* ip)
{
	return wifi_rtl_start_hostapd(ssid, psk, ip);
}

int RK_wifi_disable_ap()
{
	return wifi_rtl_stop_hostapd();
}

static int is_wifi_enable()
{
	char str[8];
	int ret;

	memset(str, 0, sizeof(str));
	ret = exec("pidof dhcpcd", str);
	if (0 != ret || 0 == strlen(str))
		return 0;

	memset(str, 0, sizeof(str));
	ret = exec("pidof wpa_supplicant", str);
	if (0 != ret || 0 == strlen(str))
		return 0;

	return 1;
}

static int dhcpcd();

int RK_wifi_enable(const int enable)
{
	if (enable) {
		if (!is_wifi_enable()) {
			system("ifconfig wlan0 down");
			system("ifconfig wlan0 up");
			system("ifconfig wlan0 0.0.0.0");
			system("killall dhcpcd");
			system("killall wpa_supplicant");
			sleep(1);
			system("wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf");
			dhcpcd();
		}
	} else {
		system("ifconfig wlan0 down");
		system("killall wpa_supplicant");
	}

	return 0;
}

int RK_wifi_scan(void)
{
	int ret;
	char str[32];

	memset(str, 0, sizeof(str));
	ret = exec("wpa_cli -iwlan0 scan", str);

	if (0 != ret)
		return -1;

	if (0 != strncmp(str, "OK", 2) &&  0 != strncmp(str, "ok", 2))
		return -2;

	return 0;
}

char* RK_wifi_scan_r(void)
{
	return RK_wifi_scan_r_sec(0x1F);
}

char* RK_wifi_scan_r_sec(const unsigned int cols)
{
	char line[256], utf[256];
	char item[384];
	char col[128];
	char *scan_r, *p_strtok;
	size_t size = 0, index = 0;
	static size_t UNIT_SIZE = 512;
	FILE *fp = NULL;

	if (!(cols & 0x1F)) {
		scan_r = (char*) malloc(3 * sizeof(char));
		memset(scan_r, 0, 3);
		strcpy(scan_r, "[]");
		return scan_r;
	}

	remove("/tmp/scan_r.tmp");
	system("wpa_cli -iwlan0 scan_r > /tmp/scan_r.tmp");

	fp = fopen("/tmp/scan_r.tmp", "r");
	if (!fp)
		return NULL;

	memset(line, 0, sizeof(line));
	fgets(line, sizeof(line), fp);

	size += UNIT_SIZE;
	scan_r = (char*) malloc(size * sizeof(char));
	memset(scan_r, 0, size);
	strcat(scan_r, "[");

	while (fgets(line, sizeof(line) - 1, fp)) {
		memset(utf, 0, sizeof(utf));
		spec_char_convers(line, utf);

		index = 0;
		p_strtok = strtok(utf, "\t");
		memset(item, 0, sizeof(item));
		strcat(item, "{");
		while (p_strtok) {
			if (p_strtok[strlen(p_strtok) - 1] == '\n')
				p_strtok[strlen(p_strtok) - 1] = '\0';
			if (cols & (1 << index)) {
				memset(col, 0, sizeof(col));
				if (0 == index) {
					snprintf(col, sizeof(col), "\"bssid\":\"%s\",", p_strtok);
				} else if (1 == index) {
					snprintf(col, sizeof(col), "\"frequency\":%d,", atoi(p_strtok));
				} else if (2 == index) {
					snprintf(col, sizeof(col), "\"rssi\":%d,", atoi(p_strtok));
				} else if (3 == index) {
					snprintf(col, sizeof(col), "\"flags\":\"%s\",", p_strtok);
				} else if (4 == index) {
					snprintf(col, sizeof(col), "\"ssid\":\"%s\",", p_strtok);
				}
				strcat(item, col);
			}
			p_strtok = strtok(NULL, "\t");
			index++;
		}
		if (item[strlen(item) - 1] == ',') {
			item[strlen(item) - 1] = '\0';
		}
		strcat(item, "},");

		if (size <= (strlen(scan_r) + strlen(item)) + 3) {
			size += UNIT_SIZE;
			scan_r = (char*) realloc(scan_r, sizeof(char) * size);
		}
		strcat(scan_r, item);
	}
	if (scan_r[strlen(scan_r) - 1] == ',') {
		scan_r[strlen(scan_r) - 1] = '\0';
	}
	strcat(scan_r, "]");

	fclose(fp);
	return scan_r;
}

static int add_network()
{
	char ret[8];
	int i;

	memset(ret, 0, sizeof(ret));
	exec("wpa_cli -iwlan0 add_network", ret);

	if (0 == strlen(ret))
		return -1;

	return atoi(ret);
}

static int set_network(const int id, const char* ssid, const char* psk, const RK_WIFI_CONNECTION_Encryp_e encryp)
{
	int ret;
	char str[8];
	char cmd[128];

	// 1. set network ssid
	memset(str, 0, sizeof(str));
	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d ssid \\\"%s\\\"", id, ssid);
	ret = exec(cmd, str);
	if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
		return -1;

	// 2. set network psk
	memset(str, 0, sizeof(str));
	memset(cmd, 0, sizeof(cmd));
	if (encryp == NONE) {
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d key_mgmt NONE", id);
		ret = exec(cmd, str);
		if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
			return -2;
	} else if (encryp == WPA) {
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d psk \\\"%s\\\"", id, psk);
		ret = exec(cmd, str);
		if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
			return -3;
	} else if (encryp == WEP) {
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d key_mgmt NONE", id);
		ret = exec(cmd, str);
		if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
			return -41;

		memset(str, 0, sizeof(str));
		memset(cmd, 0, sizeof(cmd));
		snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 set_network %d wep_key0 \\\"%s\\\"", id, psk);
		ret = exec(cmd, str);
		if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
			return -42;
	}

	return 0;
}

static int select_network(const int id)
{
	int ret;
	char str[8];
	char cmd[128];

	memset(str, 0, sizeof(str));
	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 select_network %d", id);
	ret = exec(cmd, str);

	if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
		return -1;

	return 0;
}

static int enable_network(const int id)
{
	int ret;
	char str[8];
	char cmd[128];

	memset(str, 0, sizeof(str));
	memset(cmd, 0, sizeof(cmd));
	snprintf(cmd, sizeof(cmd), "wpa_cli -iwlan0 enable_network %d", id);
	ret = exec(cmd, str);

	if (0 != ret || 0 == strlen(str) || 0 != strncmp(str, "OK", 2))
		return -1;

	return 0;
}

static int dhcpcd() {
	char str[8];
	int ret;

	memset(str, 0, sizeof(str));
	ret = exec("pidof dhcpcd", str);

	if (0 != ret || 0 == strlen(str))
		system("/sbin/dhcpcd -f /etc/dhcpcd.conf");

	return 0;
}

static int save_configuration()
{
	system("wpa_cli -iwlan0 enable_network all");
	system("wpa_cli -iwlan0 save_config");

	return 0;
}

static void* wifi_connect_state_check(void *arg)
{
	RK_WIFI_RUNNING_State_e state;
	int i;

	state = RK_WIFI_State_IDLE;
	for(i = 0; i < 30; i++) {
		RK_wifi_running_getState(&state);
		if (RK_WIFI_State_CONNECTED == state) {
			save_configuration();
			if (m_cb != NULL)
				m_cb(state);
			return NULL;
		}
		sleep(1);
	}
	if (m_cb != NULL)
		m_cb(RK_WIFI_State_CONNECTFAILED);

	return NULL;
}

int RK_wifi_connect(const char* ssid, const char* psk)
{
	return RK_wifi_connect1(ssid, psk, WPA, 0);
}

int RK_wifi_connect1(const char* ssid, const char* psk, const RK_WIFI_CONNECTION_Encryp_e encryp, const int hide)
{
	int id, ret;

	id = add_network();
	if (id < 0)
		return -1;

	ret = set_network(id, ssid, psk, encryp);
	if (0 != ret)
		return -2;

	ret = select_network(id);
	if (0 != ret)
		return -3;

	ret = enable_network(id);
	if (0 != ret)
		return -4;

	ret = dhcpcd();
	if (0 != ret)
		return -5;

	pthread_t pth;
	pthread_create(&pth, NULL, wifi_connect_state_check, NULL);

	return 0;
}

int RK_wifi_disconnect_network(void)
{
	system("wpa_cli -iwlan0 disconnect");
	return 0;
}

int RK_wifi_restart_network(void)
{
	return 0;
}

int RK_wifi_set_hostname(const char* name)
{
	sethostname(name, strlen(name));
	return 0;
}

int RK_wifi_get_hostname(char* name, int len)
{
	if (name)
		gethostname(name, len);

	return 0;
}

int RK_wifi_get_mac(char *wifi_mac)
{
    int sock_mac;
    struct ifreq ifr_mac;
    char mac_addr[18] = {0};

    sock_mac = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_mac == -1) {
        printf("create mac socket failed.");
        return -1;
    }

    memset(&ifr_mac, 0, sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, "wlan0", sizeof(ifr_mac.ifr_name) - 1);

    if ((ioctl(sock_mac, SIOCGIFHWADDR, &ifr_mac)) < 0) {
        printf("Mac socket ioctl failed.");
        return -1;
    }

    sprintf(mac_addr, "%02X:%02X:%02X:%02X:%02X:%02X",
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[0],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[1],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[2],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[3],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[4],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[5]);

    close(sock_mac);
    strncpy(wifi_mac, mac_addr, 18);
    printf("the wifi mac : %s\r\n", wifi_mac);

    return 0;
}

int RK_wifi_has_config()
{
	FILE *fd;
	fd = fopen("/data/cfg/wpa_supplicant.conf", "r");
	if (fd == NULL)
		return 0;

	fseek(fd, 0L, SEEK_END);
	int len = ftell(fd);
	char *buf = (char *)malloc(len + 1);
	fseek(fd, 0L, SEEK_SET);
	fread(buf, 1, len, fd);
	fclose(fd);

	if (strstr(buf, "network") && strstr(buf, "ssid"))
		return 1;

	free(buf);
	return 0;
}

int RK_wifi_ping(void)
{
	return rk_ping();
}
