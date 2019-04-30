# Rockchip Linux Network Config Documentation #

---

发布版本：1.0

作者：CTF

日期：2019.4.29

文件密级：公开资料

---

**概述**

该文档旨在介绍Rockchip Linux各种配网方式。

**读者对象**

本文档（本指南）主要适用于以下工程师：

技术支持工程师

软件开发工程师

**对应DeviceIo库版本**

V1.2.1以上，不包含V1.2.1

**修订记录**

| **日期**  | **版本** | **作者** | **修改说明** |
| ---------| -------- | -------- | ---------- |
| 2019-4-29 | V1.0     | CTF | 初始版本     |
|  |  |  |  |

---

[TOC]

---

## 1、WIFI/BT配置 

## 1.1 kernel配置 

- kernel目录下执行`make menuconfig ` ，根据实际wifi选择相应配置

  ![1556523959514](img\Network_Config\1556523959514.png)

  ![1556524934650](img\Network_Config\1556524934650.png)

- 退出配置框，make savedefconfig保存配置
- 重新编译kernel

### 1.2 buildroot配置

- 根目录下执行`make menuconfig `

- rkwifibt配置，根据实际WiFi选择对应配置，必须跟kernel配置一致

  ![1556525830603](img\Network_Config\1556525830603.png)

  ![1556525916829](img\Network_Config\1556525916829.png)

- 蓝牙配置

  - realtek模组建议使用bluez 协议，正基/海华模组建议使用bsa 协议。

  - 以下配置，根据模组类型三选一

    - realtek模组选择：`bluez-utils 5.x `，使用bluez需要同时开启`bluez-alsa `  `readline `

      ![1556588857360](img\Network_Config\1556588857360.png)

      ![1556590054849](img\Network_Config\1556590054849.png)

      ![1556589025927](img\Network_Config\1556589025927.png)

      ![1556589074027](img\Network_Config\1556589074027.png)

      ![1556589318223](img\Network_Config\1556589318223.png)

      ![1556589342161](img\Network_Config\1556589342161.png)

    - 正基模组选择：  `broadcom(ampak) bsa server and app `

      进入 `wifi/bt chip support(XXX)--->  `  选择实际的芯片型号，必须跟rkwifibt配置一致

    - 海华模组选择 ： `broadcom(cypress) bsa server and app `

      进入 `wifi/bt chip support(XXX)--->  `  选择实际的芯片型号，必须跟rkwifibt配置一致

      ![1556526215526](img\Network_Config\1556526215526.png)

  - 退出配置框，make savedefconfig保存配置

### 1.3 编译说明

- 根目录下执行：`make rkwifibt-dirclean && make rkwifibt-rebuild`

- 以下编译选项，根据模组类型三选一

  - realtek模组编译：`make bluez5_utils-rebuild`

    ​			        `make bluez-alsa-rebuild`

  - 正基模组编译： `make broadcom_bsa-rebuild`

  - 海华模组编译： `make cypress_bsa-rebuild`

- 根目录下执行：`make deviceio-dirclean && make deviceio-rebuild`

- 根目录下执行：`make`

- 打包固件：`./mkfirmware.sh`


## 2、命令行配网

- 首先确保WiFi的服务进程启动，串口输入：  `ps | grep wpa_supplicant`

  ![1556521720711](img\Network_Config\1556521720711.png)

- 如果没启动，请手动启动：

  `wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf & `

- 修改  ` /data/cfg/wpa_supplicant.conf `文件，添加配置项

  ```
  network={
  		ssid="WiFi-AP"		// WiFi名字
  		psk="12345678"		// WiFi密码
  		key_mgmt=WPA-PSK	// 选填加密方式，不填的话可以自动识别
  		# key_mgmt=NONE		// 不加密
  }
  ```

- 重新读取上述配置： `wpa_cli reconfigure`

- 重新连接： `wpa_cli reconnect`

## 3、手机配网

### 3.1 ble 配网

- 简介

  目前ble配网已经集成到deviceio，接口位于RkBle.h。同时支持bluez ble配网和bsa ble配网，配置参照本文档的第一章节’WIFI/BT 配置‘。


- 接口说明

  请参考/external/deviceio/doc目录下Rockchip_Developer_Guide_Rk3308_DeviceIo_Bluetooth_CN.pdf文档的第二章节’BLE接口介绍（RkBle.h）‘。

- APP：Rkble.apk

- 配网步骤

  - 该配网步骤以bsa ble配网为例进行说明，所有板端log均为bsa的配网log。bluez操作步骤相同，板端log不同。

  - 确保wifi server进程启动 ，板端命令行执行：

    `wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf &`

  - 板端命令行执行：`deviceio_test blewifi`  启动ble 配网，设置的ble广播设备名必须以RockChip为前缀，否则Rkble.apk无法检索到设备

    ![1556593573390](img\Network_Config\1556593573390.png)

    ![1556593697645](img\Network_Config\1556593697645.png)

  - 手机端打开apk：

    点击CONTINUE -> START SCAN，扫描以RockChip为前缀命名的ble设备

    ![1556595298129](img\Network_Config\1556595298129.png)![1556595307075](img\Network_Config\1556595307075.png)![1556595311834](img\Network_Config\1556595311834.png)

  - 点击想要连接的ble设备，开始连接设备，设备连接成功，板端log如下：

    ![1556593947421](img\Network_Config\1556593947421.png)

  - 设备连接成功，apk进入配网界面，输入ssid和psk，点击Confirm，发送配网信息

    ![1556595671867](img\Network_Config\1556595671867.png)

  - 板端接收到ssid和psk后，启动配网

    ![1556606988821](img\Network_Config\1556606988821.png)

  - 配网成功

    ![1556606068410](img\Network_Config\1556606068410.png)

### 3.2 airkiss 配网

- 简介

  ​目前微信airkiss配网只支持realtek，请参照本文档第一章节 ’WIFI/BT 配置‘，正确配置kernel和rkwifibt；并且已集成到deviceio_test中。

- kernel 修改

  修改 ` /drivers/net/wireless/rockchip_wlan/rtl8723ds/Makefile `   文件

  ```
  -CONFIG_WIFI_MONITOR = n
  +CONFIG_WIFI_MONITOR = y
  ```

- 接口说明

  `int RK_wifi_airkiss_config(char *ssid, char *password)`

  启动airkiss配网，并通过ssid、password参数返回手机端传输的wifi名称和密码，成功返回0，失败返回-1

- 示例程序

  示例程序的路径为：`external/deviceio/test/rk_wifi_test.c`

  该测试用例调用`RK_wifi_airkiss_config()`启动airkiss，获取ssid和password并启动wifi配网。

  主要接口：`void rk_wifi_airkiss()`，  在DeviceIOTest.cpp中调用。

- 配网步骤

  - 确保wifi server进程启动 ，命令行执行：

    `wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf &`

  - 手机必须开启wifi，并连接网络，微信扫描二维码，进入网络配置界面

    ![1556534485235](img\Network_Config\1556534485235.png)

  - 选择 '配置设备上网'，输入手机当前连接wifi的密码，点击连接

    ![1556534634593](img\Network_Config\1556534634593.png)![1556534642529](img\Network_Config\1556534642529.png)

  - 板端命令行执行：`deviceio_test airkiss`  启动airkiss 配网

    - airkiss 启动成功可以看到如下log

      ![1556536798533](img\Network_Config\1556536798533.png)

    - 成功接收ssid和password，并开始配网

      ![1556536856374](img\Network_Config\1556536856374.png)

    - 配网成功

      ![1556536891941](img\Network_Config\1556536891941.png)

### 3.3 Softap 配网

- 简介

   ​	首先，用SDK板的WiFi创建一个AP热点，在手机端连接该AP热点；其次，通过手机端apk获取SDK板的当前扫描到的热点列表，在手机端填入要连接AP的密码，apk会把AP的ssid和密码发到SDK板端；最后，SDK板端会根据收到的信息连接WiFi。

   ​	目前Softap还未集成到deviceio_test中，后续会进一步更新！！！

- APP: /external/app/RkEcho.apk

- buildroot配置

  ![1556529149205](img\Network_Config\1556529149205.png)

- 源码开发目录

  wifi与apk端相关操作：/external/softapServer

  wifi相关操作：/external/softapDemo

- 配网步骤

  - 确保wifi server进程启动 ，命令行执行：

    `wpa_supplicant -B -i wlan0 -c /data/cfg/wpa_supplicant.conf &`

  - 板端命令行执行：

    `softapServer Rockchip-Echo-123`(wifi热点的名字，前缀必须为Rockchip-Echo-xxx)

  - 打开手机wifi setting界面，找到Rockchip-Echo-xxx , 点击连接 ：

    ![1556529808878](img\Network_Config\1556529808878.png)![1556529842514](img\Network_Config\1556529842514.png)

  - 打开apk，点击wifi setup->CONFIRM->确认->wifi列表->点击你要连接的网络名字->输入密码->点击确认

    ![1556530238748](img\Network_Config\1556530238748.png)![1556530250512](img\Network_Config\1556530250512.png)![1556530283071](img\Network_Config\1556530283071.png)

    ![1556530302765](img\Network_Config\1556530302765.png)![1556530349048](img\Network_Config\1556530349048.png)

  - 板子串口端显示

    ![1556530470219](img\Network_Config\1556530470219.png)

  - 检查网络是否连通

    - 添加dns域名解析 :`echo nameserver 8.8.8.8 > etc/resolv.conf`
    - 看下是否ping通 :`ping www.baidu.com`

- 注意要点
  - softspServer  Rockchip-Echo-123 执行后命令行是无法退出的，直到配网完成
  - 热点名千万不要写错，否则apk无法进入确认界面（Rockchip-Echo-xxx）