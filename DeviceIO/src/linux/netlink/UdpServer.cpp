/*
 * Copyright (c) 2014 Fredy Wijaya
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <mutex>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include "UdpServer.h"

const char* MSG_BROADCAST_AP_MODE = "{\"method\":\"softAP\", \"magic\":\"Rockchip\",\"params\":\"ap_wifi_mode\"}";
const char* MSG_WIFI_CONNECTING = "{\"method\":\"softAP\", \"magic\":\"Rockchip\",\"params\":\"wifi_connecting\"}";
const char* MSG_WIFI_CONNECTED = "{\"method\":\"softAP\", \"magic\":\"Rockchip\",\"params\":\"wifi_connected\"}";
const char* MSG_WIFI_FAILED = "{\"method\":\"softAP\", \"magic\":\"Rockchip\",\"params\":\"wifi_failed\"}";

namespace DeviceIOFramework {

static std::string m_broadcastMsg = "";
static bool m_isConnecting = false;
static FW_softap_state_callback m_cb = NULL;
static int m_fd_broadcast = -1;
static sockaddr_in m_addrto;

UdpServer* UdpServer::m_instance;
UdpServer* UdpServer::getInstance() {
	if (m_instance == NULL) {
		static std::mutex mt;
		mt.lock();
		if (m_instance == NULL)
			m_instance = new UdpServer();

		mt.unlock();
	}
	return m_instance;
}

UdpServer::UdpServer() {
	m_wifiManager = WifiManager::getInstance();
	m_thread_broadcast = -1;
	m_thread = -1;
	m_fd_broadcast = -1;
}

bool UdpServer::isRunning() {
	return (m_thread >= 0);
}

static int initSocket(const unsigned int port) {
	int ret, fd_socket;
	struct sockaddr_in server_addr;

	/* create a socket */
	fd_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd_socket < 0) {
		return -1;
	}

	/*  initialize server address */
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	/* bind with the local file */
	ret = bind(fd_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret < 0) {
		close(fd_socket);
		return -2;
	}

	return fd_socket;
}

void* checkWifi(void *arg) {
	printf("Wifi check thread start...\n");
	WifiManager* wifiManager = WifiManager::getInstance();

	bool ret = false;
	int time;
	for (time = 0; time < 60; time++) {
		if (wifiManager->isWifiConnected()) {
			ret = true;
			break;
		}
		sleep(1);
	}

	m_broadcastMsg = (ret ? MSG_WIFI_CONNECTED : MSG_WIFI_FAILED);
	sendto(m_fd_broadcast, m_broadcastMsg.c_str(), strlen(m_broadcastMsg.c_str()) + 1, 0,
                        (struct sockaddr*)&m_addrto, sizeof(m_addrto));
	m_isConnecting = false;
	printf("Wifi connect result %d\n", ret ? 1 : 0);
	if (ret) {
	//	sleep(5);
	//	wifiManager->disableWifiAp();
		if (m_cb != NULL) {
			m_cb(FW_softAP_State_SUCCESS, NULL);
		}
	} else {
		if (m_cb != NULL) {
			m_cb(FW_softAP_State_FAIL, NULL);
		}
	}
}

static void handleRequest(const char* buff) {
	rapidjson::Document document;
	rapidjson::Value params;
	std::string ssid;
	std::string passwd;
	std::string userdata;

	if (document.Parse(buff).HasParseError()) {
		printf("UdpServer handleRequest parse error \"%s\"", buff);
		return;
	}

	if (document.HasMember("params")) {
		params = document["params"];
		if (params.IsObject()) {
			if (params.HasMember("ssid") && params["ssid"].IsString()) {
				ssid = params["ssid"].GetString();
			}
			if (params.HasMember("passwd") && params["passwd"].IsString()) {
				passwd = params["passwd"].GetString();
			}
			if (params.HasMember("userdata") && params["userdata"].IsString()) {
				userdata = params["userdata"].GetString();
			}
			if (!m_isConnecting && !ssid.empty()) {
				m_broadcastMsg = MSG_WIFI_CONNECTING;
				sendto(m_fd_broadcast, m_broadcastMsg.c_str(), strlen(m_broadcastMsg.c_str()) + 1, 0,
						(struct sockaddr*)&m_addrto, sizeof(m_addrto));
				if (m_cb != NULL)
					m_cb(FW_softAP_State_CONNECTTING, userdata.c_str());
				WifiManager* wifiManager = WifiManager::getInstance();
				if (0 == wifiManager->connect(ssid, passwd)) {
					m_isConnecting = true;
					pthread_t pid;
					pthread_create(&pid, NULL, checkWifi, NULL);
				}
			}
		}
	}
}

void* UdpServer::threadAccept(void *arg) {
	int fd_server, fd_client, port;
	struct sockaddr_in addr_client;
	socklen_t len_addr_client;
	len_addr_client = sizeof(addr_client);
	char buff[512 + 1];
	int n;

	port = *(int*) arg;
	fd_server = initSocket(port);

	if (fd_server < 0) {
		printf("UdpServer::threadAccept init tcp socket port %d fail. error:%d\n", port, fd_server);
		goto end;
	}

	/* Recv from all time */
	while (1) {
		memset(buff, 0, sizeof(buff));
		n = recvfrom(fd_server, buff, sizeof(buff) - 1, 0, (struct sockaddr*)&addr_client, &len_addr_client);
		if (n < 0)
			goto end;

		handleRequest(buff);
	}

end:
	if (fd_server >= 0)
		close(fd_server);
	m_instance->m_thread = -1;

	return NULL;
}

int UdpServer::startUdpServer(const unsigned int port) {
	int ret;

	startBroadcastThread();

	m_port = port;
	ret = pthread_create(&m_thread, NULL, threadAccept, &m_port);
	if (0 != ret) {
		m_thread = -1;
	}
	return ret;
}

void* UdpServer::threadBroadcast(void* arg) {
	int sock, port, ret;
	const int opt = 1;
	struct sockaddr_in addrto;

	port = *(int*) arg;
	bzero(&addrto, sizeof(struct sockaddr_in));
	addrto.sin_family = AF_INET;
	addrto.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	addrto.sin_port = htons(port);

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("create udp broadcast socket of port %d failed. error:%d\n", port, sock);
		goto end;
	}
	m_fd_broadcast = sock;
	m_addrto = addrto;

	ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&opt, sizeof(opt));
	if (ret < 0) {
		printf("udp broadcast setsockopt failed. error:%d\n", ret);
		goto end;
	}

	m_broadcastMsg = MSG_BROADCAST_AP_MODE;
	while (true) {
		if (!m_broadcastMsg.empty()) {
			ret = sendto(sock, m_broadcastMsg.c_str(), m_broadcastMsg.size() + 1, 0, (struct sockaddr*)&addrto, sizeof(addrto));
			if (ret < 0) {
				printf("udp send broadcast failed. error:%d\n", ret);
			}
		}
		sleep(1);
	}

end:
	if (sock >= 0)
		close(sock);
	m_fd_broadcast = -1;
	m_instance->m_thread_broadcast = -1;

	return NULL;
}

int UdpServer::startBroadcastThread(const unsigned int port) {
	int ret;

	m_port_broadcast = port;
	ret = pthread_create(&m_thread_broadcast, NULL, threadBroadcast, &m_port_broadcast);
	if (0 != ret) {
		m_thread_broadcast = -1;
	}
	return ret;
}

int UdpServer::registerCallback(FW_softap_state_callback cb) {
	m_cb = cb;
}

int UdpServer::stopBroadcastThread() {
	if (m_thread_broadcast < 0)
		return 0;

	if (0 != pthread_cancel(m_thread_broadcast)) {
		return -1;
	}

	if (0 != pthread_join(m_thread_broadcast, NULL)) {
		return -1;
	}

	m_thread_broadcast = -1;
	return 0;
}

int UdpServer::stopUdpServer() {
	stopBroadcastThread();
	if (m_thread < 0)
		return 0;

	if (0 != pthread_cancel(m_thread)) {
		return -1;
	}

	if (0 != pthread_join(m_thread, NULL)) {
		return -1;
	}

	m_thread = -1;
	return 0;
}

} // namespace framework
