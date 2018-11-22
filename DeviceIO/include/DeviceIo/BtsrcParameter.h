#ifndef __A2DP_SOURCE_SCAN_P__
#define __A2DP_SOURCE_SCAN_P__

#ifdef __cplusplus
extern "C" {
#endif


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

#ifdef __cplusplus
}
#endif

#endif /* __A2DP_SOURCE_SCAN_P__ */
