#ifndef __BLUETOOTH_SINK_H__
#define __BLUETOOTH_SINK_H__

#include <DeviceIo/RkBtBase.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_TRACK_INFO_LEN 256
#define MAX_TRACK_NUM_LEN 64

typedef struct {
	char title[MAX_TRACK_INFO_LEN];
	char artist[MAX_TRACK_INFO_LEN];
	char album[MAX_TRACK_INFO_LEN];
	char track_num[MAX_TRACK_NUM_LEN];
	char num_tracks[MAX_TRACK_NUM_LEN];
	char genre[MAX_TRACK_INFO_LEN];
	char playing_time[MAX_TRACK_INFO_LEN];
} BtTrackInfo;

typedef enum {
	RK_BT_SINK_STATE_IDLE = 0,
	RK_BT_SINK_STATE_CONNECT,
	RK_BT_SINK_STATE_DISCONNECT,

	//avrcp
	RK_BT_SINK_STATE_PLAY,
	RK_BT_SINK_STATE_PAUSE,
	RK_BT_SINK_STATE_STOP,

	//a2dp(avdtp)
	RK_BT_A2DP_SINK_STARTED,
	RK_BT_A2DP_SINK_SUSPENDED,
	RK_BT_A2DP_SINK_STOPPED,
} RK_BT_SINK_STATE;

typedef int (*RK_BT_SINK_CALLBACK)(RK_BT_SINK_STATE state);
typedef void (*RK_BT_SINK_VOLUME_CALLBACK)(int volume);
typedef void (*RK_BT_AVRCP_TRACK_CHANGE_CB)(const char *bd_addr, BtTrackInfo track_info);
typedef void (*RK_BT_AVRCP_PLAY_POSITION_CB)(const char *bd_addr, int song_len, int song_pos);

int rk_bt_sink_register_callback(RK_BT_SINK_CALLBACK cb);
int rk_bt_sink_register_volume_callback(RK_BT_SINK_VOLUME_CALLBACK cb);
int rk_bt_sink_register_track_callback(RK_BT_AVRCP_TRACK_CHANGE_CB cb);
int rk_bt_sink_register_position_callback(RK_BT_AVRCP_PLAY_POSITION_CB cb);

int rk_bt_sink_open();
int rk_bt_sink_set_visibility(const int visiable, const int connectable);
int rk_bt_sink_close(void);
int rk_bt_sink_get_state(RK_BT_SINK_STATE *p_state);
int rk_bt_sink_play(void);
int rk_bt_sink_pause(void);
int rk_bt_sink_prev(void);
int rk_bt_sink_next(void);
int rk_bt_sink_stop(void);
int rk_bt_sink_volume_up(void);
int rk_bt_sink_volume_down(void);
int rk_bt_sink_set_volume(int volume);
int rk_bt_sink_disconnect();
int rk_bt_sink_connect_by_addr(char *addr);
int rk_bt_sink_disconnect_by_addr(char *addr);
int rk_bt_sink_get_default_dev_addr(char *addr, int len);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_SINK_H__ */