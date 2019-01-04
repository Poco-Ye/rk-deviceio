#ifndef __BLUETOOTH_BLE_H__
#define __BLUETOOTH_BLE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	RK_BLEWIFI_State_IDLE = 0,
	RK_BLEWIFI_State_CONNECTTING,
	RK_BLEWIFI_State_SUCCESS,
	RK_BLEWIFI_State_FAIL,
	RK_BLEWIFI_State_DISCONNECT
} RK_BLEWIFI_State_e;

typedef enum
{
	RK_BLE_State_IDLE = 0,
	RK_BLE_State_CONNECTTING,
	RK_BLE_State_SUCCESS,
	RK_BLE_State_FAIL,
	RK_BLE_State_DISCONNECT
} RK_BLE_State_e;

typedef int (*RK_blewifi_state_callback)(RK_BLEWIFI_State_e state);
typedef int (*RK_ble_recv_data)(const char *uuid, unsigned char *data, int len);
typedef int (*RK_ble_audio_state_callback)(RK_BLE_State_e state);
typedef int (*RK_ble_audio_recv_data)(const char *uuid, unsigned char *data, int len);

int RK_blewifi_register_callback(RK_blewifi_state_callback cb);
int RK_ble_recv_data_callback(RK_ble_recv_data cb);
int RK_ble_audio_register_callback(RK_ble_audio_state_callback cb);
int RK_ble_audio_recv_data_callback(RK_ble_audio_recv_data cb);
int RK_blewifi_start(char *name);
int RK_bleaudio_start(char *name);
int RK_blewifi_stop(void);
int RK_blewifi_getState(RK_BLEWIFI_State_e *pState);
int RK_bleaudio_getState(RK_BLE_State_e *pState);
int RK_blewifi_get_exdata(char *buffer, int *length);
int RK_ble_write(const char *uuid, unsigned char *data, int len);

typedef struct {
	char uuid[38];
	char data[134];
	int len;
} rk_ble_config;

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_BLE_H__ */
