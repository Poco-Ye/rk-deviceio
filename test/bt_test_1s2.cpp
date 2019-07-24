#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <DeviceIo/bt_manager_1s2.h>
#include "bt_test_1s2.h"

#ifdef BLUEZ_USE
/* Must be initialized before using Bluetooth ble */
static RkBtContent btmg_content;
static btmg_callback_t *g_btmg_test_cb;
static bt_paried_device *g_dev_list_test;

void _bt_gap_status_cb(btmg_state_t status)
{
	switch(status) {
		case BTMG_STATE_OFF:
			printf("++++++++++ BTMG_STATE_OFF ++++++++++\n");
			break;
		case BTMG_STATE_ON:
			printf("++++++++++ BTMG_STATE_ON ++++++++++\n");
			break;
		case BTMG_STATE_TURNING_ON:
			printf("++++++++++ BTMG_STATE_TURNING_ON ++++++++++\n");
			break;
		case BTMG_STATE_TURNING_OFF:
			printf("++++++++++ BTMG_STATE_TURNING_OFF ++++++++++\n");
			break;
	}
}

void _bt_gap_bond_state_cb(btmg_bond_state_t state, const char *bd_addr, const char *name)
{
	switch(state) {
		case BTMG_BOND_STATE_NONE:
			printf("++++++++++ BTMG_BOND_STATE_NONE ++++++++++\n");
			break;
		case BTMG_BOND_STATE_BONDING:
			printf("++++++++++ BTMG_BOND_STATE_BONDING ++++++++++\n",
				__func__, bd_addr, name);
			break;
		case BTMG_BOND_STATE_BONDED:
			printf("++++++++++ BTMG_BOND_STATE_BONDED ++++++++++\n",
				__func__, bd_addr, name);
			break;
	}
	printf("%s: bd_addr: %s, name: %s\n", __func__, bd_addr, name);
}

void _bt_a2dp_sink_connection_state_cb(const char *bd_addr, btmg_a2dp_sink_connection_state_t state)
{
	switch(state) {
		case BTMG_A2DP_SINK_CONNECTING:
			printf("++++++++++ BTMG_A2DP_SINK_CONNECTING ++++++++++\n");
			break;
		case BTMG_A2DP_SINK_CONNECTED:
			printf("++++++++++ BTMG_A2DP_SINK_CONNECTED ++++++++++\n");
			break;
		case BTMG_A2DP_SINK_DISCONNECTING:
			printf("++++++++++ BTMG_A2DP_SINK_DISCONNECTING ++++++++++\n");
			break;
		case BTMG_A2DP_SINK_DISCONNECTED:
			printf("++++++++++ BTMG_A2DP_SINK_DISCONNECTED ++++++++++\n");
			break;
	}
	printf("%s: bd_addr: %s\n", __func__, bd_addr);
}

void _bt_a2dp_sink_audio_state_cb(const char *bd_addr, btmg_a2dp_sink_audio_state_t state)
{
	switch(state) {
		case BTMG_A2DP_SINK_AUDIO_SUSPENDED:
			printf("++++++++++ BTMG_A2DP_SINK_AUDIO_SUSPENDED ++++++++++\n");
			break;
		case BTMG_A2DP_SINK_AUDIO_STOPPED:
			printf("++++++++++ BTMG_A2DP_SINK_AUDIO_STOPPED ++++++++++\n");
			break;
		case BTMG_A2DP_SINK_AUDIO_STARTED:
			printf("++++++++++ BTMG_A2DP_SINK_AUDIO_STARTED ++++++++++\n");
			break;
	}
	printf("%s: bd_addr: %s\n", __func__, bd_addr);
}

void _bt_avrcp_play_state_cb(const char *bd_addr, btmg_avrcp_play_state_t state)
{
	switch(state) {
		case BTMG_AVRCP_PLAYSTATE_STOPPED:
			printf("++++++++++ BTMG_AVRCP_PLAYSTATE_STOPPED ++++++++++\n");
			break;
		case BTMG_AVRCP_PLAYSTATE_PLAYING:
			printf("++++++++++ BTMG_AVRCP_PLAYSTATE_PLAYING ++++++++++\n");
			break;
		case BTMG_AVRCP_PLAYSTATE_PAUSED:
			printf("++++++++++ BTMG_AVRCP_PLAYSTATE_PAUSED ++++++++++\n");
			break;
		case BTMG_AVRCP_PLAYSTATE_ERROR:
			printf("++++++++++ BTMG_AVRCP_PLAYSTATE_ERROR ++++++++++\n");
			break;
	}
	printf("%s: bd_addr: %s\n", __func__, bd_addr);
}

void _bt_avrcp_track_changed_cb(const char *bd_addr, btmg_track_info_t track_info)
{
	printf("++++++++++ bt sink track change ++++++++++\n");
	printf("	remote device address: %s\n", bd_addr);
	printf("	title: %s\n", track_info.title);
	printf("	artist: %s\n", track_info.artist);
	printf("	album: %s\n", track_info.album);
	printf("	genre: %s\n", track_info.genre);
	printf("	num_tracks: %s\n", track_info.num_tracks);
	printf("	track_num: %s\n", track_info.track_num);
	printf("	playing_time: %s\n", track_info.playing_time);
}

void _bt_avrcp_play_position_cb(const char *bd_addr, int song_len, int song_pos)
{
	printf("++++++++++ bt sink position change ++++++++++\n");
	printf("	remote device address: %s\n", bd_addr);
	printf("	song_len: %d, song_pos: %d\n", song_len, song_pos);
}

/*preinit function, to allocate room for callback struct, which will be free by bt_manager_deinit*/
int bt_manager_preinit(btmg_callback_t **btmg_cb)
{
	*btmg_cb = (btmg_callback_t *)malloc(sizeof(btmg_callback_t));
	if(*btmg_cb == NULL) {
		printf("malloc bt manager callback failed\n");
		return -1;
	}

	(*btmg_cb)->btmg_gap_cb.gap_status_cb = _bt_gap_status_cb;
	(*btmg_cb)->btmg_gap_cb.gap_bond_state_cb = _bt_gap_bond_state_cb;

	(*btmg_cb)->btmg_a2dp_sink_cb.a2dp_sink_connection_state_cb = _bt_a2dp_sink_connection_state_cb;
	(*btmg_cb)->btmg_a2dp_sink_cb.a2dp_sink_audio_state_cb = _bt_a2dp_sink_audio_state_cb;

	(*btmg_cb)->btmg_avrcp_cb.avrcp_play_state_cb = _bt_avrcp_play_state_cb;
	(*btmg_cb)->btmg_avrcp_cb.avrcp_track_changed_cb = _bt_avrcp_track_changed_cb;
	(*btmg_cb)->btmg_avrcp_cb.avrcp_play_position_cb = _bt_avrcp_play_position_cb;

	return 0;
}

void btmg_init_test(char *data)
{
	bt_manager_preinit(&g_btmg_test_cb);
	bt_manager_init(g_btmg_test_cb);
	bt_manager_enable(true);
}

void btmg_deinit_test(char *data)
{
	bt_manager_deinit(g_btmg_test_cb);
	g_btmg_test_cb = NULL;
}

void btmg_set_scan_none_test(char *data)
{
	bt_manager_set_discovery_mode(BTMG_SCAN_MODE_NONE);
}

void btmg_connectable_test(char *data)
{
	bt_manager_set_discovery_mode(BTMG_SCAN_MODE_CONNECTABLE);
}

void btmg_connectable_discoverable_test(char *data)
{
	bt_manager_set_discovery_mode(BTMG_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
}

void btmg_set_device_name_test(char *data)
{
	printf("%s: device name: %s\n", __func__, data);
	bt_manager_set_name(data);
}

void btmg_get_device_name_test(char *data)
{
	char name[256];
	memset(name, 0, 256);
	bt_manager_get_name(name, 256);
	printf("%s: local device name: %s\n", __func__, name);
}

void btmg_get_device_addr_test(char *data)
{
	char addr[18];
	memset(addr, 0, 18);
	bt_manager_get_address(addr, 18);
	printf("%s: local device addr: %s\n", __func__, addr);
}

void btmg_pair_by_addr_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	bt_manager_pair(data);
}

void btmg_unpair_by_addr_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	bt_manager_unpair(data);
}

void btmg_get_paired_devices_test(char *data)
{
	int i, count;
	bt_paried_device *dev_tmp = NULL;

	bt_manager_get_paired_devices(&g_dev_list_test, &count);

	printf("%s: current paired devices count: %d\n", __func__, count);
	dev_tmp = g_dev_list_test;
	for(i = 0; i < count; i++) {
		printf("device %d\n", i);
		printf("	remote_address: %s\n", dev_tmp->remote_address);
		printf("	remote_name: %s\n", dev_tmp->remote_name);
		printf("	is_connected: %d\n", dev_tmp->is_connected);
		dev_tmp = dev_tmp->next;
	}
}

void btmg_free_paired_devices_test(char *data)
{
	bt_manager_free_paired_devices(&g_dev_list_test);
}

//sink test
void btmg_sink_connect_by_addr_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	bt_manager_a2dp_sink_connect(data);
}

void btmg_sink_disconnect_by_addr_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	bt_manager_a2dp_sink_disconnect(data);
}

void btmg_sink_avrcp_play_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	if(data) {
		bt_manager_avrcp_command(data, BTMG_AVRCP_PLAY);
	} else {
		char bd_addr[18];
		memset(bd_addr, 0, 18);
		rk_bt_sink_get_default_dev_addr(bd_addr, 18);
		bt_manager_avrcp_command(bd_addr, BTMG_AVRCP_PLAY);
	}
}

void btmg_sink_avrcp_pause_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	if(data) {
		bt_manager_avrcp_command(data, BTMG_AVRCP_PAUSE);
	} else {
		char bd_addr[18];
		memset(bd_addr, 0, 18);
		rk_bt_sink_get_default_dev_addr(bd_addr, 18);
		bt_manager_avrcp_command(bd_addr, BTMG_AVRCP_PAUSE);
	}
}

void btmg_sink_avrcp_stop_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	if(data) {
		bt_manager_avrcp_command(data, BTMG_AVRCP_STOP);
	} else {
		char bd_addr[18];
		memset(bd_addr, 0, 18);
		rk_bt_sink_get_default_dev_addr(bd_addr, 18);
		bt_manager_avrcp_command(bd_addr, BTMG_AVRCP_STOP);
	}
}

void btmg_sink_avrcp_next_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	if(data) {
		bt_manager_avrcp_command(data, BTMG_AVRCP_BACKWARD);
	} else {
		char bd_addr[18];
		memset(bd_addr, 0, 18);
		rk_bt_sink_get_default_dev_addr(bd_addr, 18);
		bt_manager_avrcp_command(bd_addr, BTMG_AVRCP_BACKWARD);
	}
}

void btmg_sink_avrcp_previous_test(char *data)
{
	printf("%s: device addr: %s\n", __func__, data);
	if(data) {
		bt_manager_avrcp_command(data, BTMG_AVRCP_FORWARD);
	} else {
		char bd_addr[18];
		memset(bd_addr, 0, 18);
		rk_bt_sink_get_default_dev_addr(bd_addr, 18);
		bt_manager_avrcp_command(bd_addr, BTMG_AVRCP_FORWARD);
	}
}

//rk api, for test pair and unpair
void btmg_enable_reconnect_test(char *data)
{
	rk_bt_enable_reconnect(1);
}

void btmg_disable_reconnect_test(char *data)
{
	rk_bt_enable_reconnect(0);
}

void btmg_start_discovery_test(char *data)
{
	rk_bt_start_discovery(5000); //5s
}

void btmg_cancel_discovery_test(char *data)
{
	rk_bt_cancel_discovery();
}

void btmg_is_discovering_test(char *data)
{
	bool ret = rk_bt_is_discovering();
	printf("the device discovery procedure is active? %s\n", (ret == true) ? "yes" : "no");
}

void btmg_display_devices_test(char *data)
{
	rk_bt_display_devices();
}

void btmg_display_paired_devices_test(char *data)
{
	rk_bt_display_paired_devices();
}
#endif