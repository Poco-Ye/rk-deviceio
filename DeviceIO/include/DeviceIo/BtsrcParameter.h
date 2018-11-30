#ifndef __A2DP_SOURCE_SCAN_P__
#define __A2DP_SOURCE_SCAN_P__

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char	  uint8_t;

typedef struct _bt_device_info {
    char name[128]; // bt name
    char address[17]; // bt address
    bool rssi_valid;
    int rssi;
    char playrole[48]; // sink? source?
    struct _bt_device_info *next;
} BtDeviceInfo;

/*
 * Specify Bluetooth scan parameters.
 * mseconds: How long is the scan, in milliseconds.
 * item_cnt: How many entries to scan. Stop scanning after reaching this value.
 * device_list: Save scan results.
 */
typedef struct _bt_scan_parameter {
    unsigned short mseconds;
    unsigned char item_cnt;
    BtDeviceInfo *device_list;
} BtScanParam;

#define GATT_MAX_CHR 10
typedef struct BLE_CONTENT_T
{
	uint8_t advData[32];
	uint8_t advDataLen;
	uint8_t respData[32];
	uint8_t respDataLen;
	uint8_t server_uuid[38];
	uint8_t char_uuid[GATT_MAX_CHR][38];
	uint8_t char_cnt;
	int (*cb_ble_recv_fun)(char *uuid, char *data, int len);
	void (*cb_ble_request_data)(char *uuid);
} ble_content_t;

int gatt_main(ble_content_t *ble_content);
int gatt_write_data(char *uuid, void *data, int len);
void release_ble_gatt(void);
void ble_enable_adv(void);
void ble_disable_adv(void);

#ifdef __cplusplus
}
#endif

#endif /* __A2DP_SOURCE_SCAN_P__ */
