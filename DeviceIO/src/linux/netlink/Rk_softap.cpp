#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "DeviceIo/Rk_softap.h"
#include "DeviceIo/WifiManager.h"
#include "UdpServer.h"

typedef struct {
	char* name;
	char* exdata;
	RK_softap_state_callback callback;
	RK_softAP_State_e state;
} RkSoftAp;

RkSoftAp m_softap;

static int callback(FW_softAP_State_e state, const char* data) {
	if (m_softap.callback != NULL) {
		RK_softAP_State_e rk_state;
		switch (state) {
			case FW_softAP_State_IDLE:
				rk_state = RK_softAP_State_IDLE;
				break;
			case FW_softAP_State_CONNECTTING:
				rk_state = RK_softAP_State_CONNECTTING;
				if (m_softap.exdata != NULL)
					free(m_softap.exdata);
				m_softap.exdata = strdup(data);
				break;
			case FW_softAP_State_SUCCESS:
				rk_state = RK_softAP_State_SUCCESS;
				break;
			case FW_softAP_State_FAIL:
				rk_state = RK_softAP_State_FAIL;
				break;
			case FW_softAP_State_DISCONNECT:
				rk_state = RK_softAP_State_DISCONNECT;
				break;
		}
		m_softap.state = rk_state;
		m_softap.callback(rk_state);
	}
	return 0;
}

int RK_softap_register_callback(RK_softap_state_callback cb) {
	m_softap.callback = cb;
	return 0;
}

int RK_softap_start(char* name) {
	int ret;
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	// check whether wifi enabled, if not enable first
	if (!wifiManager->isWifiEnabled()) {
		wifiManager->setWifiEnabled(true);
	}
	// start ap mode
	wifiManager->enableWifiAp(name);

	// start udp server
	DeviceIOFramework::UdpServer* server = DeviceIOFramework::UdpServer::getInstance();
	server->registerCallback(callback);
	ret = server->startUdpServer();

	return ret;
}

int RK_softap_stop(void) {
	int ret;
	// stop udp server
	DeviceIOFramework::UdpServer* server = DeviceIOFramework::UdpServer::getInstance();
	ret = server->stopUdpServer();
	// stop ap mode
	DeviceIOFramework::WifiManager* wifiManager = DeviceIOFramework::WifiManager::getInstance();
	ret = wifiManager->disableWifiAp();

	return ret;
}

int RK_softap_getState(RK_softAP_State_e* pState) {
	*pState = m_softap.state;
	return 0;
}

int RK_softap_get_exdata(char* buffer, int* length) {
	if (m_softap.exdata == NULL) {
		*length = 0;
		return;
	}
	*length = strlen(m_softap.exdata);

	snprintf(buffer, *length + 1, m_softap.exdata);
	return 0;
}
