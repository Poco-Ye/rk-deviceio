#ifndef __BLUETOOTH_BLE_H__
#define __BLUETOOTH_BLE_H__

#include "DeviceIo/BtParameter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	RK_BLE_State_IDLE = 0,
	RK_BLE_State_CONNECTTING,
	RK_BLE_State_SUCCESS,
	RK_BLE_State_FAIL,
	RK_BLE_State_DISCONNECT
} RK_BLE_State_e;

typedef struct {
	char uuid[38];
	char data[134];
	int len;
} rk_ble_config;

typedef int (*RK_ble_state_callback)(RK_BLE_State_e state);
typedef int (*RK_ble_recv_data)(const char *uuid, unsigned char *data, int len);

int RK_ble_register_callback(RK_ble_state_callback cb);
int RK_ble_recv_data_callback(RK_ble_recv_data cb);
int RK_ble_start(Ble_Gatt_Content_t ble_content);
int RK_ble_stop(void);
int RK_ble_getState(RK_BLE_State_e *pState);
int RK_ble_get_exdata(char *buffer, int *length);
int RK_ble_write(const char *uuid, unsigned char *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_BLE_H__ */
