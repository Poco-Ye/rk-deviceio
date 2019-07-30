#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <DeviceIo/DeviceIo.h>
#include <DeviceIo/RkBtSink.h>
#include <DeviceIo/RkBtHfp.h>

#include <DeviceIo/bt_manager_1s2.h>

static btmg_callback_t *g_btmg_cb = NULL;
static bool g_btmg_enable = false;

int bt_manager_set_loglevel(btmg_log_level_t log_level)
{
	return 0;
}

/* get the bt_manager printing level*/
btmg_log_level_t bt_manager_get_loglevel(void)
{
	return BTMG_LOG_LEVEL_NONE;
}

void bt_manager_debug_open_syslog(void)
{
}

void bt_manager_debug_close_syslog(void)
{
}

static void btmg_gap_status_cb(RK_BT_STATE status)
{
	if(g_btmg_cb)
		g_btmg_cb->btmg_gap_cb.gap_status_cb((btmg_state_t)status);
}

static void btmg_gap_bond_state_cb(const char *bd_addr, const char *name, RK_BT_BOND_STATE state)
{
	if(g_btmg_cb)
		g_btmg_cb->btmg_gap_cb.gap_bond_state_cb((btmg_bond_state_t)state, bd_addr, name);
}

static int btmg_sink_callback(RK_BT_SINK_STATE state)
{
	char bd_addr[18];
	memset(bd_addr, 0, 18);
	rk_bt_sink_get_default_dev_addr(bd_addr, 18);

	switch(state) {
		case RK_BT_SINK_STATE_IDLE:
			break;
#if 0
		case RK_BT_SINK_STATE_CONNECTING:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb(bd_addr, BTMG_A2DP_SINK_CONNECTING);
			break;
		case RK_BT_SINK_STATE_DISCONNECTING:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb(bd_addr, BTMG_A2DP_SINK_DISCONNECTING);
			break;
#endif
		case RK_BT_SINK_STATE_CONNECT:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb(bd_addr, BTMG_A2DP_SINK_CONNECTED);
			break;
		case RK_BT_SINK_STATE_DISCONNECT:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb(bd_addr, BTMG_A2DP_SINK_DISCONNECTED);
			break;
		//avrcp
		case RK_BT_SINK_STATE_PLAY:
			if(g_btmg_cb)
				g_btmg_cb->btmg_avrcp_cb.avrcp_play_state_cb(bd_addr, BTMG_AVRCP_PLAYSTATE_PLAYING);
			break;
		case RK_BT_SINK_STATE_PAUSE:
			if(g_btmg_cb)
				g_btmg_cb->btmg_avrcp_cb.avrcp_play_state_cb(bd_addr, BTMG_AVRCP_PLAYSTATE_PAUSED);
			break;
		case RK_BT_SINK_STATE_STOP:
			if(g_btmg_cb)
				g_btmg_cb->btmg_avrcp_cb.avrcp_play_state_cb(bd_addr, BTMG_AVRCP_PLAYSTATE_STOPPED);
			break;
		//avdtp(a2dp)
		case RK_BT_A2DP_SINK_STARTED:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_audio_state_cb(bd_addr, BTMG_A2DP_SINK_AUDIO_STARTED);
			break;
		case RK_BT_A2DP_SINK_SUSPENDED:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_audio_state_cb(bd_addr, BTMG_A2DP_SINK_AUDIO_SUSPENDED);
			break;
		case RK_BT_A2DP_SINK_STOPPED:
			if(g_btmg_cb)
				g_btmg_cb->btmg_a2dp_sink_cb.a2dp_sink_audio_state_cb(bd_addr, BTMG_A2DP_SINK_AUDIO_STOPPED);
			break;
	}

	return 0;
}

static void btmg_sink_track_change_callback(const char *bd_addr, BtTrackInfo track_info)
{
	if(g_btmg_cb)
		g_btmg_cb->btmg_avrcp_cb.avrcp_track_changed_cb(bd_addr, track_info);
}

static void btmg_sink_position_change_callback(const char *bd_addr, int song_len, int song_pos)
{
	if(g_btmg_cb)
		g_btmg_cb->btmg_avrcp_cb.avrcp_play_position_cb(bd_addr, song_len, song_pos);
}

/*preinit function, to allocate room for callback struct, which will be free by bt_manager_deinit*/
int bt_manager_preinit(btmg_callback_t **btmg_cb)
{
	*btmg_cb = (btmg_callback_t *)malloc(sizeof(btmg_callback_t));
	if(*btmg_cb == NULL) {
		printf("malloc bt manager callback failed\n");
		return -1;
	}

	return 0;
}

/*init function, the callback functions will be registered*/
int bt_manager_init(btmg_callback_t *btmg_cb)
{
	g_btmg_cb = btmg_cb;
	return 0;
}

/*deinit function, must be called before exit*/
int bt_manager_deinit(btmg_callback_t *btmg_cb)
{
	if(bt_manager_is_enabled())
		bt_manager_enable(false);

	if(btmg_cb)
		free(btmg_cb);

	g_btmg_cb = NULL;
	return 0;
}

/*enable BT*/
int bt_manager_enable(bool enable)
{
	int ret;
	if(enable) {
		rk_bt_register_state_callback(btmg_gap_status_cb);
		rk_bt_register_bond_callback(btmg_gap_bond_state_cb);
		if(rk_bt_init(NULL) < 0) {
			printf("%s: rk_bt_init error\n", __func__);
			return -1;
		}

		//rk_bt_sink_register_volume_callback(NULL);
		rk_bt_sink_register_track_callback(btmg_sink_track_change_callback);
		rk_bt_sink_register_position_callback(btmg_sink_position_change_callback);
		rk_bt_sink_register_callback(btmg_sink_callback);
		ret = rk_bt_sink_open();
	} else {
		rk_bt_deinit();
		ret = 0;
	}

	g_btmg_enable = enable;
	return ret;
}

/*return BT state, is enabled or not*/
bool bt_manager_is_enabled(void)
{
	return g_btmg_enable;
}

/*GAP APIs*/
/*set BT discovery mode*/
int bt_manager_set_discovery_mode(btmg_discovery_mode_t mode)
{
	int ret = -1;
	switch (mode) {
		case BTMG_SCAN_MODE_NONE:
			ret = rk_bt_sink_set_visibility(0, 0);
			break;
		case BTMG_SCAN_MODE_CONNECTABLE:
			ret = rk_bt_sink_set_visibility(0, 1);
			break;
		case BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE:
			ret = rk_bt_sink_set_visibility(1, 1);
			break;
	}

	return ret;
}

/*pair*/
int bt_manager_pair(char *addr)
{
	return rk_bt_pair_by_addr(addr);
}

/*unpair*/
int bt_manager_unpair(char *addr)
{
	return rk_bt_unpair_by_addr(addr);
}

/*get bt state*/
btmg_state_t bt_manager_get_state()
{
	return BTMG_STATE_OFF;
}

/*get BT name*/
int bt_manager_get_name(char *name, int size)
{
	return rk_bt_get_device_name(name, size);
}

/*set BT name*/
int bt_manager_set_name(const char *name)
{
	return rk_bt_set_device_name(name);
}

/*get local device address*/
int bt_manager_get_address(char *addr, int size)
{
	return rk_bt_get_device_addr(addr, size);
}

/*a2dp sink APIs*/
/*request a2dp_sink connection*/
int bt_manager_a2dp_sink_connect(char *addr)
{
	return rk_bt_sink_connect_by_addr(addr);
}

/*request a2dp_sink disconnection*/
int bt_manager_a2dp_sink_disconnect(char *addr)
{
	return rk_bt_sink_disconnect_by_addr(addr);
}

/*used to send avrcp command, refer to the struct btmg_avrcp_command_t for the supported commands*/
int bt_manager_avrcp_command(char *addr, btmg_avrcp_command_t command)
{
	int ret = -1;
	char bd_addr[18];

	memset(bd_addr, 0, 18);
	rk_bt_sink_get_default_dev_addr(bd_addr, 18);
	if(strncmp(bd_addr, addr, 17)) {
		printf("%s: Invalid address(%s)\n", __func__, addr);
		return -1;
	}

	switch(command) {
		case BTMG_AVRCP_PLAY:
			ret = rk_bt_sink_play();
			break;
		case BTMG_AVRCP_STOP:
			ret = rk_bt_sink_stop();
			break;
		case BTMG_AVRCP_PAUSE:
			ret = rk_bt_sink_pause();
			break;
		case BTMG_AVRCP_FORWARD:
			ret = rk_bt_sink_prev();
			break;
		case BTMG_AVRCP_BACKWARD:
			ret = rk_bt_sink_next();
			break;
	}

	return ret;
}

/* Get the paired device,need to call <bt_manager_free_paired_devices> to free data*/
int bt_manager_get_paired_devices(bt_paried_device **dev_list,int *count)
{
	return rk_bt_get_paired_devices(dev_list, count);
}

/* free paird device data resource*/
int bt_manager_free_paired_devices(bt_paried_device **dev_list)
{
	return rk_bt_free_paired_devices(dev_list);
}

int bt_manager_disconnect(char *addr)
{
	return 0;
}

/*send GetPlayStatus cmd*/
int bt_manager_send_get_play_status()
{
	return rk_bt_sink_get_play_status();
}

/*if support avrcp EVENT_PLAYBACK_POS_CHANGED,*/
bool bt_manager_is_support_pos_changed()
{
	return rk_bt_sink_get_poschange();
}

int bt_manager_switch_throughput(bool sw_to_wlan)
{
	return 0;
}