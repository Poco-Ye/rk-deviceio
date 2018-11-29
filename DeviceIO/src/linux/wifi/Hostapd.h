#ifndef DEVICEIO_FRAMEWORK_HOSTAPD_H_
#define DEVICEIO_FRAMEWORK_HOSTAPD_H_

int wifi_rtl_start_hostapd(const char* ssid, const char* psk, const char* ip);
int wifi_rtl_stop_hostapd();

#endif // DEVICEIO_FRAMEWORK_HOSTAPD_H_
