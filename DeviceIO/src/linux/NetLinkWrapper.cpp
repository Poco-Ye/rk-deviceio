/*
 * Copyright (c) 2017 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

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
#include <errno.h>
#include <paths.h>
#include <sys/wait.h>

#include "Logger.h"
#include "DeviceIo/DeviceIo.h"
#include "NetLinkWrapper.h"
#include "SoundController.h"
#include "shell.h"

namespace DeviceIOFramework {

using std::string;
using std::vector;
using std::ifstream;

NetLinkWrapper *NetLinkWrapper::s_netLinkWrapper = nullptr;
pthread_once_t  NetLinkWrapper::s_initOnce = PTHREAD_ONCE_INIT;
pthread_once_t  NetLinkWrapper::s_destroyOnce = PTHREAD_ONCE_INIT;

static string m_target_ssid;
static string m_target_pwd;
static string m_target_ssid_prefix;
static int m_ping_interval = 1;
static int m_network_status = 0;
static bool m_pinging = false;

NetLinkWrapper::NetLinkWrapper() : m_networkStatus{NETLINK_NETWORK_SUCCEEDED},
                                     m_isLoopNetworkConfig{false},
                                     m_isNetworkOnline{false},
                                     m_isFirstNetworkReady{true},
                                     m_isFromConfigNetwork{false},
                                     m_callback{NULL} {
    s_destroyOnce = PTHREAD_ONCE_INIT;
    m_maxPacketSize = MAX_PACKETS_COUNT;
    m_datalen = 56;
    m_nsend = 0;
    m_nreceived = 0;
    m_icmp_seq = 0;
    m_stop_network_recovery = false;
    m_operation_type = operation_type::EOperationStart;

    pthread_mutex_init(&m_ping_lock, NULL);
    init_network_config_timeout_alarm();
}

NetLinkWrapper::~NetLinkWrapper() {
    s_initOnce = PTHREAD_ONCE_INIT;
    pthread_mutex_destroy(&m_ping_lock);
}

NetLinkWrapper* NetLinkWrapper::getInstance() {
    pthread_once(&s_initOnce, &NetLinkWrapper::init);
    return s_netLinkWrapper;
}

void NetLinkWrapper::init() {
    if (s_netLinkWrapper == nullptr) {
        s_netLinkWrapper = new NetLinkWrapper();
    }
}

void NetLinkWrapper::destroy() {
    delete s_netLinkWrapper;
    s_netLinkWrapper = nullptr;
}

void NetLinkWrapper::release() {
    pthread_once(&s_destroyOnce, NetLinkWrapper::destroy);
}

void NetLinkWrapper::setCallback(INetLinkWrapperCallback *callback) {
    m_callback = callback;
}

void NetLinkWrapper::logFunction(const char* msg, ...) {
}

void NetLinkWrapper::sigalrm_fn(int sig) {
    APP_INFO("alarm is run.");

    getInstance()->m_operation_type = operation_type::EAutoEnd;

    getInstance()->stop_network_config_timeout_alarm();
    getInstance()->stop_network_config();
    getInstance()->notify_network_config_status(ENetworkConfigRouteFailed);
}

void NetLinkWrapper::init_network_config_timeout_alarm() {
    APP_INFO("set alarm.");

    signal(SIGALRM, sigalrm_fn);
}

void NetLinkWrapper::start_network_config_timeout_alarm(int timeout) {
    APP_INFO("start alarm.");

    alarm(timeout);
}

void NetLinkWrapper::stop_network_config_timeout_alarm() {
    APP_INFO("stop alarm.");

    alarm(0);
}

static string generate_ssid(void) {
    string ssid;
    char mac_address[18];

    ssid.append(NETLINK_SSID_PREFIX_ROCKCHIP);
    DeviceIo::getInstance()->controlWifi(WifiControl::GET_WIFI_MAC, mac_address, 18);
    ssid += mac_address;
    ssid.erase(std::remove(ssid.begin(), ssid.end(), ':'), ssid.end());
    return ssid;
}
bool NetLinkWrapper::start_network_config() {
    string ssid = generate_ssid();
    //disconnect wifi if wifi network is ready;
    DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_DISCONNECT);
    DeviceIo::getInstance()->controlBt(BtControl::BLE_CLOSE_SERVER);
#ifdef ENABLE_SOFTAP
    DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN_AP_MODE, (void *)ssid.c_str(), strlen(ssid.c_str()));
#endif
    DeviceIo::getInstance()->controlBt(BtControl::BLE_OPEN_SERVER);
    getInstance()->notify_network_config_status(ENetworkConfigStarted);
}

void NetLinkWrapper::stop_network_config() {
#ifdef ENABLE_SOFTAP
    DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_CLOSE_AP_MODE);
#endif
    DeviceIo::getInstance()->controlBt(BtControl::BLE_CLOSE_SERVER);
}

void *NetLinkWrapper::monitor_work_routine(void *arg) {
    auto thread = static_cast<NetLinkWrapper*>(arg);
    int time_interval = 1;
    int time_count = 1;
    while(1) {
        thread->ping_network(false);
        time_count = time_interval = m_ping_interval;
        APP_DEBUG("monitor_work_routine m_ping_interval:%d", m_ping_interval);

        while (time_count > 0) {
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            ::select(0, NULL, NULL, NULL, &tv);
            time_count--;
            if (time_interval != m_ping_interval) {
                APP_DEBUG("monitor_work_routine m_ping_interval:%d, time_interval:%d", m_ping_interval, time_interval);
                break;
            }
        }
    }

    return nullptr;
}

void NetLinkWrapper::start_network_monitor() {
    pthread_t network_config_threadId;

    pthread_create(&network_config_threadId, nullptr, monitor_work_routine, this);
    pthread_detach(network_config_threadId);
}

bool is_first_network_config(string path) {
    ifstream it_stream;
    int length = 0;
    string wpa_config_file = path;

#if 1 //for hisense board
    if (access("/data/property.txt", F_OK))
	return true;
#endif

    it_stream.open(wpa_config_file.c_str());
    if (!it_stream.is_open()) {
        APP_ERROR("wpa config file open error.");
        return false;
    }

    it_stream.seekg(0,std::ios::end);
    length = it_stream.tellg();
    it_stream.seekg(0,std::ios::beg);

    char *buffer = new char[length + 1];
    it_stream.read(buffer, length);
    it_stream.close();
    buffer[length] = 0;

    char * position = nullptr;
    position = strstr(buffer,"ssid");
    delete [] buffer;
    buffer = nullptr;

    if (nullptr == position) {
        APP_ERROR("First network config.");
        return true;
    }

    APP_INFO("Not first network config.");

    return false;
}

void NetLinkWrapper::startNetworkRecovery() {

    DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_CLOSE_AP_MODE);
    DeviceIo::getInstance()->controlWifi(WifiControl::WIFI_OPEN);

    if (is_first_network_config(NETLINK_WPA_CONFIG_FILE)) {
        getInstance()->m_operation_type = operation_type::EAutoConfig;
        start_network_config_timeout_alarm(NETLINK_AUTO_CONFIG_TIMEOUT);
        start_network_config();
    } else {
        check_recovery_network_status();
    }
    start_network_monitor();
}

void NetLinkWrapper::stopNetworkRecovery() {
    m_stop_network_recovery = true;
}

bool NetLinkWrapper::startNetworkConfig(int timeout) {
    APP_INFO("start configing %d.", timeout);

    m_operation_type = operation_type::EManualConfig;

    start_network_config_timeout_alarm(timeout);
    start_network_config();
}

void NetLinkWrapper::stopNetworkConfig() {
    APP_INFO("stopping networkconfig.");

    m_operation_type = operation_type::EAutoEnd;

    stop_network_config_timeout_alarm();
    stop_network_config();
    getInstance()->notify_network_config_status(ENetworkConfigExited);
}

NetLinkNetworkStatus NetLinkWrapper::getNetworkStatus() const {
    return m_networkStatus;
}


static char *netstatus[] = {
    "NETLINK_NETWORK_CONFIG_STARTED",
    "NETLINK_NETWORK_CONFIGING",
    "NETLINK_NETWORK_CONFIG_SUCCEEDED",
    "NETLINK_NETWORK_CONFIG_FAILED",
    "NETLINK_NETWORK_SUCCEEDED",
    "NETLINK_NETWORK_FAILED",
    "NETLINK_NETWORK_RECOVERY_START",
    "NETLINK_NETWORK_RECOVERY_SUCCEEDED",
    "NETLINK_NETWORK_RECOVERY_FAILED",
    "NETLINK_WAIT_LOGIN"
};

static char *notifyEvent[] = {
    "ENetworkNone",
    "ENetworkConfigExited",
    "ENetworkConfigStarted",
    "ENetworkDeviceConnected",
    "ENetworkConfigIng",
    "ENetworkConfigRouteFailed",
    "ENetworkLinkSucceed",
    "ENetworkLinkFailed",
    "ENetworkRecoveryStart",
    "ENetworkRecoverySucceed",
    "ENetworkRecoveryFailed"
};

void NetLinkWrapper::setNetworkStatus(NetLinkNetworkStatus networkStatus) {
    std::lock_guard<std::mutex> lock(m_mutex);
    APP_INFO("#### setNetworkStatus from %s to %s\n", netstatus[m_networkStatus - NETLINK_NETWORK_CONFIG_STARTED],
        netstatus[networkStatus - NETLINK_NETWORK_CONFIG_STARTED]);
    NetLinkWrapper::m_networkStatus = networkStatus;
    if (m_callback)
        m_callback->netlinkNetworkStatusChanged(networkStatus);
}

void NetLinkWrapper::notify_network_config_status(notify_network_status_type notify_type) {

    APP_INFO("#### notify_event: ops %d, event: %s\n", get_operation_type(), notifyEvent[notify_type - ENetworkNone]);

    switch (notify_type) {
        case ENetworkConfigStarted: {
            setNetworkStatus(NETLINK_NETWORK_CONFIG_STARTED);
            if (!m_isLoopNetworkConfig) {
                if (get_operation_type() == operation_type::EAutoConfig) {
                    SoundController::getInstance()->linkStartFirst();
                } else if (get_operation_type() == operation_type::EManualConfig) {
                    SoundController::getInstance()->linkStart();
                }
                m_isLoopNetworkConfig = true;
            }
            break;
        }
        case ENetworkConfigIng: {
            setNetworkStatus(NETLINK_NETWORK_CONFIGING);
            if (get_operation_type() != operation_type::EAutoEnd) {
                SoundController::getInstance()->linkConnecting();
            }
            break;
        }
        case ENetworkLinkFailed: {
            //Network config failed, reset wpa_supplicant.conf
            //set_wpa_conf(false);

            setNetworkStatus(NETLINK_NETWORK_CONFIG_FAILED);
            SoundController::getInstance()->linkFailedPing(NetLinkWrapper::networkLinkFailed);
            break;
        }
        case ENetworkConfigRouteFailed: {
            //Network config failed, reset wpa_supplicant.conf
            //set_wpa_conf(false);

            setNetworkStatus(NETLINK_NETWORK_CONFIG_FAILED);
            SoundController::getInstance()->linkFailedIp(NetLinkWrapper::networkLinkFailed);
            break;
        }
        case ENetworkConfigExited: {
            setNetworkStatus(NETLINK_NETWORK_FAILED);
            m_isLoopNetworkConfig = false;
            SoundController::getInstance()->linkExit(nullptr);
            APP_INFO("notify_network_config_status: ENetworkConfigExited=====End");
            break;
        }
        case ENetworkLinkSucceed: {
            setFromConfigNetwork(false);
            if (1) {//DeviceIoWrapper::getInstance()->isTouchStartNetworkConfig()) {
                /// from networkConfig
                setFromConfigNetwork(true);
            }
            //Network config succed, update wpa_supplicant.conf
            stop_network_config_timeout_alarm();
            //set_wpa_conf(true);

            networkLinkOrRecoverySuccess();

            setNetworkStatus(NETLINK_NETWORK_CONFIG_SUCCEEDED);
            if (!isFromConfigNetwork()) {
            }

            m_isLoopNetworkConfig = false;
            OnNetworkReady();

            break;
        }
        case ENetworkRecoveryStart: {
            setNetworkStatus(NETLINK_NETWORK_RECOVERY_START);
            SoundController::getInstance()->reLink();
            break;
        }
        case ENetworkRecoverySucceed: {
            setNetworkStatus(NETLINK_NETWORK_RECOVERY_SUCCEEDED);

            networkLinkOrRecoverySuccess();

            setFromConfigNetwork(false);
            OnNetworkReady();
            break;
        }
        case ENetworkRecoveryFailed: {
            setNetworkStatus(NETLINK_NETWORK_RECOVERY_FAILED);
            SoundController::getInstance()->reLinkFailed();
            break;
        }
        case ENetworkDeviceConnected: {
            SoundController::getInstance()->hotConnected();
            break;
        }
        default:
            break;
    }
}

#define SYSTEM_AUTHOR_CODE_PATH     "/userdata/cfg/check_data"
#define SYSTEM_RM_AUTHOR_CODE     "rm /userdata/cfg/check_data"

void NetLinkWrapper::network_status_changed(InternetConnectivity current_status, bool wakeupTrigger) {
    if (current_status == InternetConnectivity::AVAILABLE) {
        if (!isNetworkOnline()) {
            setNetworkOnline(true);
            if (m_callback) {
                m_callback->netlinkNetworkOnlineStatus(isNetworkOnline());
            }
            switch (getNetworkStatus()) {
                case DeviceIOFramework::NETLINK_NETWORK_CONFIG_STARTED:
                case DeviceIOFramework::NETLINK_NETWORK_CONFIGING:
                case DeviceIOFramework::NETLINK_NETWORK_CONFIG_FAILED:
                        notify_network_config_status(ENetworkLinkSucceed);
                        break;
                case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_FAILED:
                        notify_network_config_status(ENetworkRecoverySucceed);
                        break;
                case DeviceIOFramework::NETLINK_NETWORK_FAILED:
                        setNetworkStatus(NETLINK_NETWORK_SUCCEEDED);
                        break;
                default:
                        break;
                break;
            }
        }
        //setNetworkStatus(NETLINK_NETWORK_SUCCEEDED);
    } else {
        if (isNetworkOnline()) {
            setNetworkOnline(false);
            if (m_callback) {
                m_callback->netlinkNetworkOnlineStatus(isNetworkOnline());
            }
        }
        if (0) {//!DeviceIoWrapper::getInstance()->isTouchStartNetworkConfig()) {
            //setNetworkStatus(NETLINK_NETWORK_FAILED);
            if (wakeupTrigger) {
                wakeupNetLinkNetworkStatus();
            }
        }
        switch (getNetworkStatus()) {
            case DeviceIOFramework::NETLINK_NETWORK_CONFIG_STARTED:
                    if (!access(SYSTEM_AUTHOR_CODE_PATH, F_OK)) {
                        Shell::system(SYSTEM_RM_AUTHOR_CODE);
                        notify_network_config_status(ENetworkConfigIng);
                    }
                    break;
            case DeviceIOFramework::NETLINK_NETWORK_CONFIG_SUCCEEDED:
            case DeviceIOFramework::NETLINK_NETWORK_SUCCEEDED:
            case DeviceIOFramework::NETLINK_NETWORK_RECOVERY_SUCCEEDED:
                    setNetworkStatus(NETLINK_NETWORK_FAILED);
                    break;
            default:
                    break;
            break;
        }
    }
}

void NetLinkWrapper::wakeupNetLinkNetworkStatus() {
    switch (m_networkStatus) {
        case NETLINK_NETWORK_CONFIG_STARTED: {
            APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_CONFIG_STARTED=====");
            SoundController::getInstance()->linkStart();
            break;
        }
        case NETLINK_NETWORK_CONFIGING: {
            APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_CONFIGING=====");
            SoundController::getInstance()->linkConnecting();
            break;
        }
        case NETLINK_NETWORK_RECOVERY_START: {
            APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_RECOVERY_START=====");
            SoundController::getInstance()->reLink();
            break;
        }
        case NETLINK_NETWORK_FAILED: {
            APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_FAILED=====");
            SoundController::getInstance()->networkConnectFailed();
            break;
        }
        case NETLINK_NETWORK_RECOVERY_FAILED: {
            APP_INFO("wakeup_net_link_network_status: NETLINK_NETWORK_RECOVERY_FAILED=====");
            SoundController::getInstance()->reLinkFailed();
            break;
        }
        default:
            break;
    }
}

void NetLinkWrapper::networkLinkSuccess() {

}

void NetLinkWrapper::networkLinkFailed() {

}

bool NetLinkWrapper::isNetworkOnline() const {
    return m_isNetworkOnline;
}

void NetLinkWrapper::setNetworkOnline(bool isNetworkOnline) {
    NetLinkWrapper::m_isNetworkOnline = isNetworkOnline;
}

void NetLinkWrapper::networkLinkOrRecoverySuccess() {
    if (isFromConfigNetwork()) {
        SoundController::getInstance()->linkSuccess(NetLinkWrapper::networkLinkSuccess);
    } else {
        SoundController::getInstance()->reLinkSuccess(NetLinkWrapper::networkLinkSuccess);
    }
}

bool NetLinkWrapper::isFirstNetworkReady() const {
    return m_isFirstNetworkReady;
}

void NetLinkWrapper::setFirstNetworkReady(bool isFirstNetworkReady) {
    NetLinkWrapper::m_isFirstNetworkReady = isFirstNetworkReady;
}

bool NetLinkWrapper::isFromConfigNetwork() const {
    return m_isFromConfigNetwork;
}

void NetLinkWrapper::setFromConfigNetwork(bool isFromConfigNetwork) {
    NetLinkWrapper::m_isFromConfigNetwork = isFromConfigNetwork;
}

void NetLinkWrapper::OnNetworkReady() {
    if (m_callback)
        m_callback->networkReady();
    Shell::system("/etc/init.d/S49ntp stop;"
                   "ntpdate cn.pool.ntp.org;"
                   "/etc/init.d/S49ntp start");
}

unsigned short NetLinkWrapper::getChksum(unsigned short *addr,int len) {
    int nleft = len;
    int sum = 0;
    unsigned short *w = addr;
    unsigned short answer = 0;

    while (nleft > 1) {
        sum += *w++;
        nleft-= 2;
    }

    if (nleft == 1) {
        *(unsigned char *)(&answer) = *(unsigned char *)w;
        sum += answer;
    }

    sum = ((sum>>16) + (sum&0xffff));
    sum += (sum>>16);
    answer = ~sum;

    return answer;
}

int NetLinkWrapper::packIcmp(int pack_no, struct icmp* icmp) {
    int packsize;
    struct icmp *picmp;
    struct timeval *tval;

    picmp = icmp;
    picmp->icmp_type = ICMP_ECHO;
    picmp->icmp_code = 0;
    picmp->icmp_cksum = 0;
    picmp->icmp_seq = pack_no;
    picmp->icmp_id = m_pid;
    packsize = (8 + m_datalen);
    tval= (struct timeval *)icmp->icmp_data;
    gettimeofday(tval, nullptr);
    picmp->icmp_cksum = getChksum((unsigned short *)icmp, packsize);

    return packsize;
}

bool NetLinkWrapper::unpackIcmp(char *buf, int len, struct IcmpEchoReply *icmpEchoReply) {
    int iphdrlen;
    struct ip *ip;
    struct icmp *icmp;
    struct timeval *tvsend, tvrecv, tvresult;
    double rtt;

    ip = (struct ip *)buf;
    iphdrlen = ip->ip_hl << 2;
    icmp = (struct icmp *)(buf + iphdrlen);
    len -= iphdrlen;

    if (len < 8) {
        APP_ERROR("ICMP packets's length is less than 8.");
        return false;
    }

    if( (icmp->icmp_type == ICMP_ECHOREPLY) && (icmp->icmp_id == m_pid) ) {
        tvsend = (struct timeval *)icmp->icmp_data;
        gettimeofday(&tvrecv, nullptr);
        tvresult = timevalSub(tvrecv, *tvsend);
        rtt = tvresult.tv_sec*1000 + tvresult.tv_usec/1000;  //ms
        icmpEchoReply->rtt = rtt;
        icmpEchoReply->icmpSeq = icmp->icmp_seq;
        icmpEchoReply->ipTtl = ip->ip_ttl;
        icmpEchoReply->icmpLen = len;

        return true;
    } else {
        return false;
    }
}

struct timeval NetLinkWrapper::timevalSub(struct timeval timeval1, struct timeval timeval2) {
    struct timeval result;

    result = timeval1;

    if ((result.tv_usec < timeval2.tv_usec) && (timeval2.tv_usec < 0)) {
        -- result.tv_sec;
        result.tv_usec += 1000000;
    }

    result.tv_sec -= timeval2.tv_sec;

    return result;
}

bool NetLinkWrapper::sendPacket() {
    size_t packetsize;
    while( m_nsend < m_maxPacketSize) {
        m_nsend ++;
        m_icmp_seq ++;
        packetsize = packIcmp(m_icmp_seq, (struct icmp*)m_sendpacket);

        if (sendto(m_sockfd,m_sendpacket, packetsize, 0, (struct sockaddr *) &m_dest_addr, sizeof(m_dest_addr)) < 0) {
            APP_ERROR("Ping sendto failed:%s.", strerror(errno));
            continue;
        }
    }

    return true;
}

bool NetLinkWrapper::recvPacket(PingResult &pingResult) {
    int len = 0;
    struct IcmpEchoReply icmpEchoReply;
    int maxfds = m_sockfd + 1;
    int nfd  = 0;
    fd_set rset;
    struct timeval timeout;
    socklen_t fromlen = sizeof(m_from_addr);

    timeout.tv_sec = MAX_WAIT_TIME;
    timeout.tv_usec = 0;

    FD_ZERO(&rset);

    for (int recvCount = 0; recvCount < m_maxPacketSize; recvCount ++) {
        FD_SET(m_sockfd, &rset);
        if ((nfd = select(maxfds, &rset, nullptr, nullptr, &timeout)) == -1) {
            APP_ERROR("Ping recv select failed:%s.", strerror(errno));
            continue;
        }

        if (nfd == 0) {
            icmpEchoReply.isReply = false;
            pingResult.icmpEchoReplys.push_back(icmpEchoReply);
            continue;
        }

        if (FD_ISSET(m_sockfd, &rset)) {
            if ((len = recvfrom(m_sockfd,
                                m_recvpacket,
                                sizeof(m_recvpacket),
                                0,
                                (struct sockaddr *)&m_from_addr,
                                &fromlen)) <0) {
                if(errno == EINTR) {
                    continue;
                }
                APP_ERROR("Ping recvfrom failed: %s.", strerror(errno));
                continue;
            }

            icmpEchoReply.fromAddr = inet_ntoa(m_from_addr.sin_addr) ;
            if (strncmp(icmpEchoReply.fromAddr.c_str(), pingResult.ip, strlen(pingResult.ip)) != 0) {
                recvCount--;
                continue;
            }
        }

        if (!unpackIcmp(m_recvpacket, len, &icmpEchoReply)) {
            recvCount--;
            continue;
        }

        icmpEchoReply.isReply = true;
        pingResult.icmpEchoReplys.push_back(icmpEchoReply);
        m_nreceived ++;
    }

    return true;

}

bool NetLinkWrapper::getsockaddr(const char * hostOrIp, struct sockaddr_in* sockaddr) {
    struct hostent *host;
    struct sockaddr_in dest_addr;
    unsigned long inaddr = 0l;

    bzero(&dest_addr,sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;

    inaddr = inet_addr(hostOrIp);
    if (inaddr == INADDR_NONE) {
        host = gethostbyname(hostOrIp);
        if (host == nullptr) {
            return false;
        }
        memcpy( (char *)&dest_addr.sin_addr,host->h_addr, host->h_length);
    } else if (!inet_aton(hostOrIp, &dest_addr.sin_addr)) {
        return false;
    }

    *sockaddr = dest_addr;

    return true;
}

bool NetLinkWrapper::ping(string host, int count, PingResult& pingResult) {
    int size = 50 * 1024;
    IcmpEchoReply icmpEchoReply;

    m_nsend = 0;
    m_nreceived = 0;
    pingResult.icmpEchoReplys.clear();
    m_maxPacketSize = count;
    m_pid = getpid();

    pingResult.dataLen = m_datalen;

    if ((m_sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) < 0) {
        APP_ERROR("Ping socket failed:%s.", strerror(errno));
        pingResult.error = strerror(errno);
        return false;
    }

    if (setsockopt(m_sockfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
        APP_ERROR("Setsockopt SO_RCVBUF failed:%s.", strerror(errno));
        close(m_sockfd);
        return false;
    }

    if (!getsockaddr(host.c_str(), &m_dest_addr)) {
        pingResult.error = "unknow host " + host;
        close(m_sockfd);
        return false;
    }

    strcpy(pingResult.ip, inet_ntoa(m_dest_addr.sin_addr));

    sendPacket();
    recvPacket(pingResult);

    pingResult.nsend = m_nsend;
    pingResult.nreceived = m_nreceived;

    close(m_sockfd);

    return true;
}

bool NetLinkWrapper::ping_network(bool wakeupTrigger) {
    string hostOrIp = PING_DEST_HOST1;
    int nsend = 0, nreceived = 0;
    bool ret;
    PingResult pingResult;
    InternetConnectivity networkResult = UNAVAILABLE;

    pthread_mutex_lock(&m_ping_lock);

    for (int count = 1; count <= MAX_PACKETS_COUNT; count ++) {
        memset(&pingResult.ip, 0x0, 32);
        ret = ping(hostOrIp, 1, pingResult);

        if (!ret) {
            APP_ERROR("Ping error:%s", pingResult.error.c_str());
        } else {
            nsend += pingResult.nsend;
            nreceived += pingResult.nreceived;
            if (nreceived > 0)
                break;
        }

        if (count == 2) {
            hostOrIp = PING_DEST_HOST2;
        }
    }

    if (nreceived > 0) {
        ret = true;
        networkResult = AVAILABLE;
        if (m_network_status == (int)UNAVAILABLE) {
            m_ping_interval = 1;
        } else {
            if (m_ping_interval < MAX_PING_INTERVAL) {
                m_ping_interval = m_ping_interval * 2;
                if (m_ping_interval > MAX_PING_INTERVAL) {
                    m_ping_interval = MAX_PING_INTERVAL;
                }
            }
        }
        m_network_status = 1;
    } else {
        ret = false;
        networkResult = UNAVAILABLE;
        m_network_status = 0;
        m_ping_interval = 1;
    }

    network_status_changed(networkResult, wakeupTrigger);

    pthread_mutex_unlock(&m_ping_lock);

    return ret;
}

bool NetLinkWrapper::check_recovery_network_status() {
    notify_network_config_status(ENetworkRecoveryStart);

    int network_check_count = 0;
    bool recovery = false;

    while(!(recovery = ping_network(false))) {
        if (m_stop_network_recovery || network_check_count == NETLINK_NETWORK_CONFIGURE_PING_COUNT) {
            APP_ERROR("Network recovery ping failed.");

            break;
        }

        sleep(1);
        network_check_count++;
    }

    if (m_stop_network_recovery) {
        APP_INFO("Network recovery cancel.");
        startNetworkConfig(NETLINK_AUTO_CONFIG_TIMEOUT);
        return true;
    } else if (recovery) {
        APP_INFO("Network recovery succed.");
        notify_network_config_status(ENetworkRecoverySucceed);
        return true;
    } else {
        APP_ERROR("Network recovery failed.");
        notify_network_config_status(ENetworkRecoveryFailed);
        return false;
    }
}

}  // namespace application
