#include <iostream>
#include <stdio.h>
#include "DeviceIo/WifiManager.h"
#include "../TcpServer.h"

static void help(void) {
	printf("Usage: deviceio_netlink_test INTERFACE [ARGS...]\n\n");
	printf("  startTcpServer [port]     start tcp server with port\n");
}

static bool isRunning(DeviceIOFramework::TcpServer* server)
{
	bool isRun = server->isRunning();
	if (isRun) {
		printf("Tcp server is running!\n");
	} else {
		printf("Tcp server is not running!\n");
	}

	return isRun;
}

static int startTcpServer(DeviceIOFramework::TcpServer* server, const unsigned int port = 8443)
{
	int ret = server->startTcpServer(port);
	if (ret != 0) {
		printf("Tcp server start fail! port:%u\n; error:%d.", port, ret);
	} else {
		printf("Tcp server start success! port:%u\n", port);
	}
	return ret;
}

static int stopTcpServer(DeviceIOFramework::TcpServer* server)
{
	int ret = server->stopTcpServer();
	if (0 != ret) {
		printf("Tcp server stop fail!\n");
	} else {
		printf("Tcp server stop success!\n");
	}
	return ret;
}

#if 0
int main(int argc, char** argv)
{
	DeviceIOFramework::WifiManager* manager = DeviceIOFramework::WifiManager::getInstance();
	DeviceIOFramework::TcpServer* server = DeviceIOFramework::TcpServer::getInstance();

	if (argc < 2 || 0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "--help")) {
		help();
		return 0;
	}

	if (0 == strcmp(argv[1], "startTcpServer")) {
		if (0 != manager->enableWifiAp("Rockchip-Echo-Test")) {
			printf("Enable wifi ap mode failed...\n");
			return 0;
		}
		if (argc < 3) {
			startTcpServer(server);
		} else {
			startTcpServer(server, std::stoi(argv[2]));
		}
	}
	for (;;);

	delete server;
	manager->disableWifiAp();
	delete manager;
	return 0;
}
#endif
