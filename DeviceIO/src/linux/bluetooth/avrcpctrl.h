#ifndef AVRCPCTRL_H
#define AVRCPCTRL_H
//play status
#define AVRCP_PLAY_STATUS_STOPPED	0x00 // 停止
#define AVRCP_PLAY_STATUS_PLAYING	0x01 //正在播放
#define AVRCP_PLAY_STATUS_PAUSED	0x02 //暂停播放
#define AVRCP_PLAY_STATUS_FWD_SEEK	0x03 //快进
#define AVRCP_PLAY_STATUS_REV_SEEK	0x04 //重播
#define AVRCP_PLAY_STATUS_ERROR		0xFF //错误状态

/* Menu items */
enum AVK_CMD
{
	APP_AVK_MENU_DISCOVERY = 1,
	APP_AVK_MENU_REGISTER,
	APP_AVK_MENU_DEREGISTER,
	APP_AVK_MENU_OPEN,
	APP_AVK_MENU_CLOSE,
	APP_AVK_MENU_PLAY_START,
	APP_AVK_MENU_PLAY_STOP,
	APP_AVK_MENU_PLAY_PAUSE,
	APP_AVK_MENU_PLAY_NEXT_TRACK,
	APP_AVK_MENU_PLAY_PREVIOUS_TRACK,
	APP_AVK_MENU_RC_OPEN,
	APP_AVK_MENU_RC_CLOSE,
	APP_AVK_MENU_RC_CMD,
	APP_AVK_MENU_GET_ELEMENT_ATTR,
	APP_AVK_MENU_GET_CAPABILITIES,
	APP_AVK_MENU_REGISTER_NOTIFICATION,
	APP_AVK_MENU_REGISTER_NOTIFICATION_RESPONSE,
	APP_AVK_MENU_SEND_DELAY_RPT,
	APP_AVK_MENU_SEND_ABORT_REQ,
	APP_AVK_MENU_GET_PLAY_STATUS,
	APP_AVK_MENU_GET_ELEMENT_ATTRIBUTES,
	APP_AVK_MENU_SET_BROWSED_PLAYER,
	APP_AVK_MENU_SET_ADDRESSED_PLAYER,
	APP_AVK_MENU_GET_FOLDER_ITEMS,
	APP_AVK_MENU_CHANGE_PATH,
	APP_AVK_MENU_GET_ITEM_ATTRIBUTES,
	APP_AVK_MENU_PLAY_ITEM,
	APP_AVK_MENU_ADD_TO_NOW_PLAYING,
	APP_AVK_MENU_LIST_PLAYER_APP_SET_ATTR,
	APP_AVK_MENU_LIST_PLAYER_APP_SET_VALUE,
	APP_AVK_MENU_SET_ABSOLUTE_VOLUME,
	APP_AVK_MENU_SELECT_STREAMING_DEVICE,
	APP_AVK_MENU_QUIT = 99
};

/**
* 初始化 蓝牙音频反向控制模块
*/
int init_avrcp_ctrl();
/**
* 释放蓝牙音频反向控制相关资源
*/
int release_avrcp_ctrl();
/**
* 播放
*/
int play_avrcp();
 /**
* 暂停播放
*/
int pause_avrcp();
/**
* 停止播放
*/
int stop_avrcp();
/**
* 下一首
*/
int next_avrcp();
/**
* 上一首
*/
int previous_avrcp();
/**
* 获取当前蓝牙音频状态
*/
int getstatus_avrcp();

void volumeup_avrcp();

void volumedown_avrcp();
bool check_default_player(void);
int a2dp_sink_colse_coexist();
bool reconn_last(void);
bool disconn_device(void);
int a2dp_sink_open(void);
void rkbt_inquiry_scan(bool scan);

#endif
