#ifndef __BLUETOOTH_SINK_H__
#define __BLUETOOTH_SINK_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	RK_BTA2DP_State_IDLE = 0,
	RK_BTA2DP_State_CONNECT,
	RK_BTA2DP_State_PLAY ,
	RK_BTA2DP_State_PAUSE,
	RK_BTA2DP_State_STOP ,
	RK_BTA2DP_State_DISCONNECT
} RK_BTA2DP_State_e;

typedef int (*RK_bta2dp_callback)(RK_BTA2DP_State_e state);

int RK_bta2dp_register_callback(RK_bta2dp_callback cb);
int RK_bta2dp_open(char* name);
int RK_bta2dp_setVisibility(const int visiable, const int connectal);
int RK_bta2dp_close(void);
int RK_bta2dp_getState(RK_BTA2DP_State_e *pState);
int RK_bta2dp_play(void);
int RK_bta2dp_pause(void);
int RK_bta2dp_prev(void);
int RK_bta2dp_next(void);
int RK_bta2dp_stop(void);
int RK_bta2dp_set_auto_reconnect(int enable);
int RK_bta2dp_disconnect();

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_SINK_H__ */