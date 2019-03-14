/*****************************************************************************
**
**  Name:           app_ble_wifi_introducer.h
**
**  Description:    Bluetooth BLE WiFi introducer include file
**
**  Copyright (c) 2018, Cypress Corp., All Rights Reserved.
**  Cypress Bluetooth Core. Proprietary and confidential.
**
*****************************************************************************/
#ifndef APP_BLE_WIFI_INTRODUCER_H
#define APP_BLE_WIFI_INTRODUCER_H

#include "bsa_api.h"
#include "app_ble.h"
#include "bluetooth_bsa.h"

#define APP_BLE_WIFI_INTRODUCER_TIMEOUT 60 /* 6 seconds */
#define APP_BLE_WIFI_INTRODUCER_GATT_ATTRIBUTE_SIZE (22)
typedef void (tAPP_BLE_WIFI_CBACK)(tBSA_BLE_EVT event, tBSA_BLE_MSG *p_data);
/* This is the default AP the device will connect to (as a client)*/
#define CLIENT_AP_SSID       "YOUR_AP_SSID"
#define CLIENT_AP_PASSPHRASE "YOUR_AP_PASSPHRASE"

#ifndef APP_BLE_WIFI_INTRODUCER_ATTRIBUTE_MAX
#define APP_BLE_WIFI_INTRODUCER_ATTRIBUTE_MAX BSA_BLE_ATTRIBUTE_MAX
#endif

typedef struct
{
    tBT_UUID       attr_UUID;
    INT8          uuid_string[38]; //save string uuid
    UINT16         service_id;
    UINT16         attr_id;
    UINT8          attr_type;
    tBSA_BLE_CHAR_PROP prop;
    tBSA_BLE_PERM       perm;
    BOOLEAN        is_pri;
    BOOLEAN        wait_flag;
} tAPP_BLE_WIFI_INTRODUCER_ATTRIBUTE;

typedef struct
{
    tBSA_BLE_IF         server_if;
    UINT16              conn_id;
    tAPP_BLE_WIFI_INTRODUCER_ATTRIBUTE  attr[APP_BLE_WIFI_INTRODUCER_ATTRIBUTE_MAX]; //service + characteristic + descriptor
} tAPP_BLE_WIFI_INTRODUCER_CB;

typedef struct
{
    UINT16 company_id;
    UINT16 beacon;
    UINT8  proximity_uuid[MAX_UUID_SIZE];
    UINT16 major_id;
    UINT16 minor_id;
    UINT8 measured_power;
}__attribute__((packed)) tAPP_BLE_KG_MANUFACTURE_DATA;

typedef struct {
    uint16_t service_uuid_value;
    uint16_t company_id;
    uint16_t pid;
    uint8_t version;
    BD_ADDR mac_addr;
}__attribute__((packed)) tAPP_BLE_KG_RESP_DATA;

/*******************************************************************************
 **
 ** Function        app_ble_wifi_introducer_init
 **
 ** Description     APP BLE wifi introducer control block init
 **
 ** Parameters      None
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_wifi_introducer_init(void);

/*******************************************************************************
 **
 ** Function        app_ble_wifi_introducer_gatt_server_init
 **
 ** Description     APP BLE wifi introducer GATT Server init
 **
 ** Parameters
 **
 ** Returns
 **
 *******************************************************************************/
int app_ble_wifi_introducer_gatt_server_init(Ble_Gatt_Content_t ble_content);

/*******************************************************************************
 **
 ** Function        app_ble_wifi_introducer_send_message
 **
 ** Description     Check if client has registered for notification/indication
 **                       and send message if appropriate
 **
 ** Parameters      data, len
 **
 ** Returns          None
 **
 *******************************************************************************/
void app_ble_wifi_introducer_send_message(const char *uuid, UINT8 * data, UINT16 len);

/*******************************************************************************
 **
 ** Function        app_ble_wifi_introducer_open
 **
 ** Description     APP BLE wifi introducer open
 **
 ** Parameters
 **
 ** Returns
 **
 *******************************************************************************/
int app_ble_wifi_introducer_open(Ble_Gatt_Content_t ble_content);

/*******************************************************************************
 **
 ** Function        app_ble_wifi_introducer_close
 **
 ** Description     APP BLE wifi introducer close
 **
 ** Parameters      None
 **
 ** Returns         None
 **
 *******************************************************************************/
void app_ble_wifi_introducer_close(void);

/*******************************************************************************
 **
 ** Function        app_ble_wifi_introducer_recv_data_callback
 **
 ** Description     register send data callback
 **
 ** Parameters      None
 **
 ** Returns
 **
 *******************************************************************************/
void app_ble_wifi_introducer_recv_data_callback(RK_ble_recv_data cb);

#endif
