#ifndef __BLUETOOTH_SPP_H__
#define __BLUETOOTH_SPP_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	RK_BTSPP_State_IDLE = 0,
	RK_BTSPP_State_CONNECT,
	RK_BTSPP_State_DISCONNECT
} RK_BTSPP_State;

typedef enum {
	RK_BTSPP_Event_DATA = 0,
	RK_BTSPP_Event_CONNECT,
	RK_BTSPP_Event_DISCONNECT
} RK_BTSPP_Event;

typedef void (*RK_btspp_callback)(int type, char *data, int len);

int RK_btspp_open(RK_btspp_callback cb);
int RK_btspp_close(void);
int RK_btspp_getState(RK_BTSPP_State *pState);
int RK_btspp_write(char *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_SPP_H__ */

