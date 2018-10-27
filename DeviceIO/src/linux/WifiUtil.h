#ifndef __WIFI_UTIL_H__
#define __WIFI_UTIL_H__

#include <stdio.h>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <string>

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
 
#include <iostream>
#include <fstream>

#include "WifiInfo.h"

#define WIFI_CONFIG_MAX 240

#define DEVICE_CONFIG_FILE "/data/property.txt"
class WifiUtil{
public:

    /**
     * @brief get instance
     *
     * @return the instance pointer
     */
    static WifiUtil* getInstance();

    /**
     * @brief release instance
     *
     */
    void releaseInstance();

    /**
     * @brief open wifi
     *
     */
    void openWifi();

    void closeWifi();

    /* use wpa_cli to get wifi list from device*/
    std::string getWifiListJson();

    /* read device 4 serial no from config file*/
    std::string getDeviceContextJson();

    /**
     * try connect wifi by wpa_cli.
     * it will create a new thread detached to check wifi state.
     * @Param recv_buff  http request header.
     */ 
    void WifiConnect(char *recv_buff);

private:
    static WifiUtil *m_instance;
    //static DeviceInNotify* m_notify;
    static pthread_once_t m_initOnce;
    static pthread_once_t m_destroyOnce;

    WifiUtil();
    ~WifiUtil();
    static void init();
    static void destroy();
};
#endif // __WIFI_UTIL_H__
