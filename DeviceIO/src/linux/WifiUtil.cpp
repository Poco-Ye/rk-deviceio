#include "WifiUtil.h"
#include "shell.h"
#include "Logger.h"

#include <stdlib.h>
#include <thread>
#include <string>
#include <fstream>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <csignal>
#include <fcntl.h>

using std::string;
using std::vector;
using std::ifstream;

typedef std::list<std::string> LIST_STRING;
typedef std::list<WifiInfo*> LIST_WIFIINFO;
static int network_id;

static const char *WIFI_CONFIG_FORMAT = "ctrl_interface=/var/run/wpa_supplicant\n"
                                "ap_scan=1\n\nnetwork={\nssid=\"%s\"\n"
                                "psk=\"%s\"\npriority=1\n}\n";

WifiUtil* WifiUtil::m_instance = nullptr;
pthread_once_t WifiUtil::m_initOnce = PTHREAD_ONCE_INIT;
pthread_once_t WifiUtil::m_destroyOnce = PTHREAD_ONCE_INIT;

WifiUtil::WifiUtil() {
    m_destroyOnce = PTHREAD_ONCE_INIT;
}

WifiUtil::~WifiUtil() {
    m_initOnce = PTHREAD_ONCE_INIT;
}

WifiUtil* WifiUtil::getInstance() {
    pthread_once(&m_initOnce, WifiUtil::init);
    return m_instance;
}

void WifiUtil::releaseInstance() {
    pthread_once(&m_destroyOnce, WifiUtil::destroy);
}

void WifiUtil::init() {
    if (m_instance == nullptr) {
        m_instance = new WifiUtil;
    }
}

void WifiUtil::destroy() {
    if (m_instance != nullptr) {
        delete m_instance;
        m_instance = nullptr;
    }
}

bool check_ap_interface_status(string ap) {
    int sockfd;
    bool ret = false;
    struct ifreq ifr_mac;

    if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) <= 0) {
        APP_ERROR("socket create failed.\n");
        return false;
    }

    memset(&ifr_mac,0,sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, ap.c_str(), sizeof(ifr_mac.ifr_name)-1);

    if ((ioctl(sockfd, SIOCGIFHWADDR, &ifr_mac)) < 0) {
        APP_ERROR("Mac ioctl failed.\n");
    } else {
        APP_DEBUG("Mac ioctl suceess.\n");
        ret = true;
    }
    close(sockfd);

    return ret;
}

bool WifiUtil::start_wpa_supplicant() {
    Shell::system("ifconfig wlan0 0.0.0.0");
    Shell::system("killall dhcpcd");
    Shell::system("killall wpa_supplicant");
    sleep(1);
    Shell::system("wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf");
    Shell::system("dhcpcd -k wlan0");//udhcpc -b -i wlan0 -q ");
    sleep(1);
    Shell::system("dhcpcd wlan0 -t 0&");
    return true;
}

bool WifiUtil::stop_wpa_supplicant() {
    return Shell::system("killall wpa_supplicant &");
}

bool WifiUtil::stop_ap_mode() {
    APP_INFO("stop_ap_mode\n");

    Shell::system("softapDemo stop");
    Shell::system("killall softapServer &");
    int time = 100;
    while (time-- > 0 && !access("/var/run/hostapd", F_OK)) {
        usleep(10 * 1000);
    }
    APP_INFO("End stop_ap_mode\n");
}

bool WifiUtil::start_ap_mode(char *ap_name) {
    bool ret_value = true;
    string cmd;

    if (ap_name == NULL)
        ap_name = "RockchipEcho-123";

    APP_INFO("start_ap_mode: %s\n", ap_name);

    cmd.append("softapServer ");
    cmd += ap_name;
    cmd += " &";

    if (Shell::pidof("hostapd") || Shell::pidof("softapServer"))
        stop_ap_mode();

    Shell::system(cmd.c_str());
    int time = 100;
    while (time-- > 0 && access("/var/run/hostapd", F_OK)) {
        usleep(100 * 1000);
    }
    usleep(100 * 1000);
    APP_INFO("End start_ap_mode");

    return ret_value;
}

bool starup_ap_interface() {
    if (check_ap_interface_status(NETWORK_DEVICE_FOR_AP)) {
        APP_DEBUG("%s is up.\n", NETWORK_DEVICE_FOR_AP);

        return true;
    }

    return Shell::system("ifconfig wlan1 up &");
}

bool down_ap_interface() {
    if (!check_ap_interface_status(NETWORK_DEVICE_FOR_AP)) {
        APP_DEBUG("%s is down.\n", NETWORK_DEVICE_FOR_AP);
        return true;
    }

    return Shell::system("ifconfig wlan1 down &");
}

bool starup_wlan0_interface() {

    return Shell::system("ifconfig wlan0 up &");
}

bool down_wlan0_interface() {
    if (!check_ap_interface_status(NETWORK_DEVICE_FOR_WORK)) {
        APP_DEBUG("%s is down.\n", NETWORK_DEVICE_FOR_WORK);
        return true;
    }

    Shell::system("ifconfig wlan0 0.0.0.0");

    return Shell::system("ifconfig wlan0 down &");
}

bool stop_dhcp_server() {
    return Shell::system("killall dnsmasq &");
}

bool start_dhcp_server() {
    if (stop_dhcp_server()) {
        APP_DEBUG("[Start_dhcp_server] dnsmasq is killed.\n");
    }
    sleep(1);

    return Shell::system("dnsmasq &");
}

bool get_device_interface_ip()
{
    int sock_ip;
    struct ifreq ifr;
        struct sockaddr_in  sin;

    sock_ip = socket( AF_INET, SOCK_DGRAM, 0 );
    if (sock_ip == -1) {
        APP_ERROR("create ip socket failed.\n");
        return false;
    }

    memset(&ifr,0,sizeof(ifr));
    strncpy(ifr.ifr_name, NETWORK_DEVICE_FOR_WORK, sizeof(ifr.ifr_name)-1);

    if ((ioctl( sock_ip, SIOCGIFADDR, &ifr)) < 0) {
        APP_ERROR("ip socket ioctl failed.\n");
        close(sock_ip);
        return false;
    }

        memcpy(&sin,&ifr.ifr_addr,sizeof(sin));
        APP_DEBUG("eth0 ip: %s\n",inet_ntoa(sin.sin_addr));
    return true;

}

bool get_device_interface_mac(string &mac_address) {
    int sock_mac;
    struct ifreq ifr_mac;
    char mac_addr[30] = {0};

    sock_mac = socket( AF_INET, SOCK_STREAM, 0 );
    if (sock_mac == -1) {
        APP_ERROR("create mac socket failed.\n");
        return false;
    }

    memset(&ifr_mac,0,sizeof(ifr_mac));
    strncpy(ifr_mac.ifr_name, NETWORK_DEVICE_FOR_WORK, sizeof(ifr_mac.ifr_name)-1);

    if ((ioctl( sock_mac, SIOCGIFHWADDR, &ifr_mac)) < 0) {
        APP_ERROR("Mac socket ioctl failed.\n");
        close(sock_mac);
        return false;
    }

    sprintf(mac_addr,"%02X%02X",
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[4],
            (unsigned char)ifr_mac.ifr_hwaddr.sa_data[5]);

    APP_DEBUG("local mac:%s\n",mac_addr);

    close(sock_mac);

    mac_address = mac_addr;

    std::transform(mac_address.begin(), mac_address.end(), mac_address.begin(), toupper);

    return true;
}

void get_device_wifi_chip_type(string &wifi_chip_type)
{
	char wifi_chip[30] = {0};
    int fd = open("/sys/class/rkwifi/chip", O_RDONLY);
    if (fd < 0) {
		APP_ERROR("open /sys/class/rkwifi/chip err!\n");
		bzero(wifi_chip, sizeof(wifi_chip));
		strcpy(wifi_chip, "RTL8723DS");
	}
    else {
        memset(wifi_chip, '\0', sizeof(wifi_chip));
        read(fd, wifi_chip, sizeof(wifi_chip));
        close(fd);
    } 
	wifi_chip_type = wifi_chip;
	APP_INFO("get wifi chip: %s\n", wifi_chip);
}

/**
 * split buff array by '\n' into string list.
 * @parm buff[]
 */
LIST_STRING charArrayToList(char buff[]){
    LIST_STRING stringList;
    std::string item;
    for(int i=0;i<strlen(buff);i++){
        if(buff[i] != '\n'){
            item += buff[i];
        } else {
            stringList.push_back(item);
            item.clear();
        }
    }
    return stringList;
}

/**
 * format string list into wifiInfo list by specific rules
 * @parm string_list
 * @return LIST_WIFIINFO
 */
LIST_WIFIINFO wifiStringFormat(LIST_STRING string_list){
    LIST_WIFIINFO wifiInfo_list;

    LIST_STRING::iterator stringIte;

    /* delete first useless item */
    string_list.pop_front();

    for(stringIte=string_list.begin();stringIte!=string_list.end();stringIte++){
        WifiInfo *wifiInfoItem = new WifiInfo();
        std::string wifiStringItem = *stringIte;

        /* use for set wifiInfo item:bssid ssid etc*/
        std::string tempString;
        int index = 0; /* temp index,flag '\t' appear count*/

        for(int i=0;i<wifiStringItem.size();i++){
            if(wifiStringItem.at(i)!='\t' && i != (wifiStringItem.size()-1)){
                tempString += wifiStringItem.at(i);
            } else {
                switch(index){
                case 0: //bssid
                    wifiInfoItem->setBssid(tempString);
                    break;
                case 1: //frequency
                    wifiInfoItem->setFrequency(tempString);
                    break;
                case 2: //signalLevel
                    wifiInfoItem->setSignalLevel(tempString);
                    break;
                case 3: //flags
                    wifiInfoItem->setFlags(tempString);
                    break;
                case 4: //ssid
                    tempString += wifiStringItem.at(i);
                    wifiInfoItem->setSsid(tempString);
                    break;
                default:
                    break;
                }
                index ++;
                tempString.clear();
            }
        }
        wifiInfo_list.push_back(wifiInfoItem);
    }
    return wifiInfo_list;
}

/**
 * parse wifi info list into json string.
 * @parm wifiInfoList
 * @return json string
 */
std::string parseIntoJson(LIST_WIFIINFO wifiInfoList){
    LIST_WIFIINFO::iterator iterator;

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    /* 1. add return type */
    document.AddMember("type","WifiList",allocator);
    /* 2. add reutn content */
    rapidjson::Value wifiArrayValue(rapidjson::kArrayType);
    for(iterator = wifiInfoList.begin(); iterator != wifiInfoList.end(); ++iterator){
        (*iterator)->addJsonToRoot(document,wifiArrayValue);
    }
    document.AddMember("content",wifiArrayValue,allocator);

    /* parse into string */
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

/**
 * get json substr from http respose head.
 * split by '{' and "}"
 * @parm message http.
 */
std::string getJsonFromMessage(char message[]){
    std::string str(message);
    return str.substr(str.find('{'));
}

/**
 * use wpa_cli tool to connnect wifi in alexa device
 * @parm ssid
 * @parm password
 */
bool wifiConnect(std::string ssid,std::string password){
    char ret_buff[MSG_BUFF_LEN] = {0};
    char cmdline[MSG_BUFF_LEN] = {0};
    int id = -1;
    bool execute_result = false;

    // 1. add network
    Shell::exec("wpa_cli -iwlan0 add_network",ret_buff);
    id = atoi(ret_buff);
    if(id < 0){
        log_err("add_network failed.\n");
        return false;
    }
    // 2. setNetWorkSSID
    memset(cmdline, 0, sizeof(cmdline));
    sprintf(cmdline,"wpa_cli -iwlan0 set_network %d ssid \\\"%s\\\"",id, ssid.c_str());
    printf("%s\n", cmdline);
    Shell::exec(cmdline,ret_buff);
    execute_result = !strncmp(ret_buff,"OK",2);
    if(!execute_result){
        log_err("setNetWorkSSID failed.\n");
        return false;
    }
    // 3. setNetWorkPWD
    memset(cmdline, 0, sizeof(cmdline));
    sprintf(cmdline,"wpa_cli -iwlan0 set_network %d psk \\\"%s\\\"", id,password.c_str());
    printf("%s\n", cmdline);
    Shell::exec(cmdline,ret_buff);
    execute_result = !strncmp(ret_buff,"OK",2);
    if(!execute_result){
        log_err("setNetWorkPWD failed.\n");
        return false;
    }
    // 4. selectNetWork
    memset(cmdline, 0, sizeof(cmdline));
    sprintf(cmdline,"wpa_cli -iwlan0 select_network %d", id);
    printf("%s\n", cmdline);
    Shell::exec(cmdline,ret_buff);
    execute_result = !strncmp(ret_buff,"OK",2);
    if(!execute_result){
        log_err("setNetWorkPWD failed.\n");
        return false;
    }

    return true;
}

bool checkWifiIsConnected() {
    char ret_buff[MSG_BUFF_LEN] = {0};
    char cmdline[MSG_BUFF_LEN] = {0};

    LIST_STRING stateSList;
    LIST_STRING::iterator iterator;   

    // udhcpc network
    int udhcpc_pid = Shell::pidof("udhcpc");
    if(udhcpc_pid != 0){
        memset(cmdline, 0, sizeof(cmdline));
        sprintf(cmdline,"kill %d",udhcpc_pid);
        Shell::exec(cmdline,ret_buff);
    }
    Shell::exec("udhcpc -n -t 10 -i wlan0",ret_buff);

    bool isWifiConnected = false;
    int match = 0;
    /* 15s to check wifi whether connected */
    for(int i=0;i<5;i++){
        sleep(2);
        match = 0;
        Shell::exec("wpa_cli -iwlan0 status",ret_buff);
        stateSList = charArrayToList(ret_buff);
        for(iterator=stateSList.begin();iterator!=stateSList.end();iterator++){
            std::string item = (*iterator);
            if(item.find("wpa_state")!=std::string::npos){
                if(item.substr(item.find('=')+1)=="COMPLETED"){
                    match++;
                }
            }
            if(item.find("ip_address")!=std::string::npos){
                if(item.substr(item.find('=')+1)!="127.0.0.1"){
                    match++;
                }
            }
        }
        if(match >= 2){
            isWifiConnected = true;
            // TODO play audio: wifi connected
            log_info("Congratulation: wifi connected.\n");
            break;
        }
        log_info("Check wifi state with none state. try more %d/5, \n",i+1);
    }

    if(!isWifiConnected){
        // TODO play audio: wifi failed.
        log_info("wifi connect failed.please check enviroment.\n");
        system("gst-play-1.0 -q --no-interactive /usr/ap_notification/wifi_connect_failed.mp3 &");
 
    } else {
        system("gst-play-1.0 -q --no-interactive /usr/ap_notification/wifi_conneted.mp3 &");
        system("softapServer stop &");
    }
}


std::string WifiUtil::getWifiListJson(){
    char ret_buff[MSG_BUFF_LEN] = {0};
    std::string ret;
    int retry_count = 10;

    LIST_STRING wifiStringList;
    LIST_WIFIINFO wifiInfoList;

retry:
    Shell::exec("wpa_cli -i wlan0 scan", ret_buff);
    /* wap_cli sacn is useable */
    if(!strncmp(ret_buff,"OK",2)){
        log_info("scan useable: OKOK\n");
        Shell::exec("wpa_cli -i wlan0 scan_r", ret_buff);
        wifiStringList = charArrayToList(ret_buff);
        wifiInfoList = wifiStringFormat(wifiStringList);
    }
    
    if ((wifiInfoList.size() == 0)  && (--retry_count > 0)) {
        goto retry;
    }
    // parse wifiInfo list into json.
    ret = parseIntoJson(wifiInfoList);
    log_info("list size: %d\n",wifiInfoList.size());
    return ret;
}

std::string WifiUtil::getDeviceContextJson() {
    std::string ret = " ";
    std::string sn1;
    std::string sn2;
    std::string sn3;
    std::string sn4;

#define SN_1 "ro.hisense.jhkdeviceid"
#define SN_2 "ro.hisense.jhldeviceid"
#define SN_3 "ro.hisense.wifiid"
#define SN_4 "ro.hisense.uuid"

    rapidjson::Document newDoc;
    FILE *myFile = fopen(DEVICE_CONFIG_FILE, "r");
    if (!myFile) {
        log_info("%s, %s not exist\n", __func__, DEVICE_CONFIG_FILE);
        return ret;
    }
    char readBuffer[65536];
    rapidjson::FileReadStream is(myFile, readBuffer, sizeof(readBuffer));
    newDoc.ParseStream<0>(is);
    fclose(myFile);

    if (newDoc.HasParseError()) {
        log_info("Json Parse error: %d\n", newDoc.GetParseError());
        return ret;
    }
    if (newDoc.HasMember(SN_1)) {
         sn1 = newDoc[SN_1].GetString();
    }
    if (newDoc.HasMember(SN_2)) {
         sn2 = newDoc[SN_2].GetString();
    }
    if (newDoc.HasMember(SN_3)) {
         sn3 = newDoc[SN_3].GetString();
    }
    if (newDoc.HasMember(SN_4)) {
         sn4 = newDoc[SN_4].GetString();
    }
    log_info("Json Parse : %s, %s, %s, %s\n", sn1.c_str(), sn2.c_str(), sn3.c_str(), sn4.c_str());

    rapidjson::Document document;
    document.SetObject();
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    /* 1. add return type */
    document.AddMember("type","DeviceContext",allocator);
    /* 2. add reutn content */
    rapidjson::Value snObj(rapidjson::kObjectType);
    snObj.AddMember(SN_1, rapidjson::StringRef(sn1.c_str()), allocator);
    snObj.AddMember(SN_2, rapidjson::StringRef(sn2.c_str()), allocator);
    snObj.AddMember(SN_3, rapidjson::StringRef(sn3.c_str()), allocator);
    snObj.AddMember(SN_4, rapidjson::StringRef(sn4.c_str()), allocator);
    document.AddMember("content",snObj,allocator);
    /* parse into string */
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    return buffer.GetString();
}

static bool saveWifiConfig(const char* name, const char* pwd)
{
    FILE *fp;
    char body[WIFI_CONFIG_MAX];
    int fd;
    fp = fopen("/data/cfg/wpa_supplicant.conf", "w");

    if (fp == NULL)
    {
        return -1;
    }

    snprintf(body, sizeof(body), WIFI_CONFIG_FORMAT, name, pwd);
    fputs(body, fp);
    fflush(fp);
    fd = fileno(fp);
    if (fd >= 0) {
        fsync(fd);
        printf("save wpa_supplicant.conf sucecees.\n");
    }
    fclose(fp);

    return 0;
}

void WifiUtil::connect(char *ssid, char *psk) {
    if (ssid && psk)
        wifiConnect(ssid, psk);
    else
        Shell::system("wpa_cli -iwlan0 reconnect");
}

void WifiUtil::disconnect() {
    Shell::system("wpa_cli -iwlan0 disconnect");
}

void WifiUtil::connectJson(char *recv_buff) {
    std::string jsonString = getJsonFromMessage(recv_buff);

    /* get setUp user name and password */
    rapidjson::Document document;
    if (document.Parse(jsonString.c_str()).HasParseError()) {
        log_err("parseJsonFailed \n");
        return;
    }

    std::string userName;
    std::string password;

    auto userNameIterator = document.FindMember("ssid");
    if (userNameIterator != document.MemberEnd() && userNameIterator->value.IsString()) {
       	userName = userNameIterator->value.GetString();
    }

    auto passwordIterator = document.FindMember("pwd");
    if (passwordIterator != document.MemberEnd() && passwordIterator->value.IsString()) {
        password = passwordIterator->value.GetString();
    }

    if(userName.empty()||password.empty()){
        log_err("userName or password empty. \n");
        return;
    }

    /* use wpa_cli to connect wifi by ssid and password */
    bool connectResult = wifiConnect(userName,password);

    if(connectResult){
        std::thread thread(checkWifiIsConnected);
        thread.detach();

        saveWifiConfig(userName.c_str(), password.c_str());
    }else{
        log_info("wifi connect failed.please check enviroment. \n");
        // TODO play audio: wifi connect failed.
    }
    return;
}
