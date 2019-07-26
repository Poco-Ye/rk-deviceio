#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "gdbus.h"
#include "avrcpctrl.h"
#include "a2dp_source/a2dp_masterctrl.h"
#include "DeviceIo/DeviceIo.h"
#include "DeviceIo/Rk_shell.h"
//#include "../Timer.h"

//using DeviceIOFramework::Timer;
//using DeviceIOFramework::TimerManager;
//using DeviceIOFramework::TimerNotify;
using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

#define COLOR_OFF	"\x1B[0m"
#define COLOR_RED	"\x1B[0;91m"
#define COLOR_GREEN	"\x1B[0;92m"
#define COLOR_YELLOW	"\x1B[0;93m"
#define COLOR_BLUE	"\x1B[0;94m"
#define COLOR_BOLDGRAY	"\x1B[1;30m"
#define COLOR_BOLDWHITE	"\x1B[1;37m"

/* String display constants */
#define COLORED_NEW	COLOR_GREEN "NEW" COLOR_OFF
#define COLORED_CHG	COLOR_YELLOW "CHG" COLOR_OFF
#define COLORED_DEL	COLOR_RED "DEL" COLOR_OFF

#define PROMPT_ON	COLOR_BLUE "[bluetooth]" COLOR_OFF "# "
#define PROMPT_OFF	"[bluetooth]# "

#define pr_info(fmt, ...) \
	printf(fmt"\n", ## __VA_ARGS__)
#define pr_warn(fmt, ...) \
	printf(fmt"\n", ## __VA_ARGS__)
#define pr_err(fmt, ...) \
	printf(fmt"\n", ## __VA_ARGS__)

#define error(fmt, ...) \
	printf(fmt"\n", ## __VA_ARGS__)

#undef bt_shell_printf
#define bt_shell_printf printf

static GList *proxy_list;

static const char *last_device_path = NULL;
static char last_obj_path[] = "/org/bluez/hci0/dev_xx_xx_xx_xx_xx_xx";
static GDBusProxy *last_connected_device_proxy = NULL;
static GDBusProxy *last_temp_connected_device_proxy = NULL;
static GList *device_list;
static guint reconnect_timer;
#define STORAGE_PATH "/data/cfg/lib/bluetooth"
static void device_connected_post(GDBusProxy *proxy);

#define BLUEZ_MEDIA_PLAYER_INTERFACE "org.bluez.MediaPlayer1"
#define BLUEZ_MEDIA_FOLDER_INTERFACE "org.bluez.MediaFolder1"
#define BLUEZ_MEDIA_ITEM_INTERFACE "org.bluez.MediaItem1"
#define BLUEZ_MEDIA_TRANSPORT_INTERFACE "org.bluez.MediaTransport1"

extern DBusConnection *dbus_conn;
static GDBusProxy *default_player;
static GSList *players = NULL;
static GSList *folders = NULL;
static GSList *items = NULL;

static int first_ctrl = 1;
extern volatile bool A2DP_SINK_FLAG;
extern GDBusClient *btsrc_client;

static int g_btsrc_connect_status = RK_BT_SINK_STATE_IDLE;

extern void print_iter(const char *label, const char *name, DBusMessageIter *iter);

static RK_BT_SINK_CALLBACK g_btsink_cb = NULL;
static RK_BT_AVRCP_TRACK_CHANGE_CB g_avrcp_track_cb = NULL;
static RK_BT_AVRCP_PLAY_POSITION_CB g_avrcp_position_cb = NULL;

static char track_key[256];
static int current_song_len = 0;

static void bt_sink_state_send(RK_BT_SINK_STATE state)
{
	if (g_btsink_cb)
		(g_btsink_cb)(state);
}

void report_avrcp_event(DeviceInput event, void *data, int len) {
	if (DeviceIo::getInstance()->getNotify())
		DeviceIo::getInstance()->getNotify()->callback(event, data, len);

	printf("[AVRCP DEBUG]: event: %d \n", event);

	switch(event) {
		case DeviceInput::BT_SINK_ENV_CONNECT:
			g_btsrc_connect_status = RK_BT_SINK_STATE_CONNECT;
			bt_sink_state_send(RK_BT_SINK_STATE_CONNECT);
			break;
		case DeviceInput::BT_SINK_ENV_DISCONNECT:
			g_btsrc_connect_status = RK_BT_SINK_STATE_DISCONNECT;
			bt_sink_state_send(RK_BT_SINK_STATE_DISCONNECT);
			break;
		case DeviceInput::BT_START_PLAY:
			bt_sink_state_send(RK_BT_SINK_STATE_PLAY);
			break;
		case DeviceInput::BT_PAUSE_PLAY:
			bt_sink_state_send(RK_BT_SINK_STATE_PAUSE);
			break;
		case DeviceInput::BT_STOP_PLAY:
			bt_sink_state_send(RK_BT_SINK_STATE_STOP);
			break;
		default:
			break;
	}
}

bool system_command(const char* cmd)
{
	pid_t status = 0;
	bool ret_value = false;

	status = system(cmd);

	if (-1 == status) {
	} else {
		if (WIFEXITED(status)) {
			if (0 == WEXITSTATUS(status)) {
				ret_value = true;
			} else {
			}
		} else {
		}
	}

	return ret_value;
}

void rkbt_inquiry_scan(bool scan)
{
	if (scan)
		system_command("hciconfig hci0 piscan");
	else
		system_command("hciconfig hci0 noscan");
}

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

extern struct adapter *default_ctrl;
extern GList *ctrl_list;

static gboolean device_is_child(GDBusProxy *device, GDBusProxy *master)
{
	DBusMessageIter iter;
	const char *adapter, *path;

	if (!master)
		return FALSE;

	if (g_dbus_proxy_get_property(device, "Adapter", &iter) == FALSE)
		return FALSE;

	dbus_message_iter_get_basic(&iter, &adapter);
	path = g_dbus_proxy_get_path(master);

	if (!strcmp(path, adapter))
		return TRUE;

	return FALSE;
}

static struct adapter *find_parent(GDBusProxy *device)
{
	GList *list;

	for (list = g_list_first(ctrl_list); list; list = g_list_next(list)) {
		struct adapter *adapter = list->data;

		if (device_is_child(device, adapter->proxy) == TRUE)
			return adapter;
	}

	return NULL;
}

void connect_handler(DBusConnection *connection, void *user_data)
{
	printf("%s \n", __func__);
}

void disconnect_handler(DBusConnection *connection, void *user_data)
{
	printf("%s \n", __func__);
}

void print_folder(GDBusProxy *proxy, const char *description)
{
	const char *path;

	path = g_dbus_proxy_get_path(proxy);
}

void folder_removed(GDBusProxy *proxy)
{
	folders = g_slist_remove(folders, proxy);

	print_folder(proxy, COLORED_DEL);
}

char *proxy_description(GDBusProxy *proxy, const char *title,
						const char *description)
{
	const char *path;

	path = g_dbus_proxy_get_path(proxy);

	return g_strdup_printf("%s%s%s%s %s ",
					description ? "[" : "",
					description ? : "",
					description ? "] " : "",
					title, path);
}

void print_player(GDBusProxy *proxy, const char *description)
{
	char *str;
	char strplay[256];
	str = proxy_description(proxy, "Player", description);

	memset(strplay, 0x00, 256);
	sprintf(strplay,"%s%s\n", str, (default_player == proxy ? "[default]" : ""));
	printf(strplay);

	g_free(str);
}

void player_added(GDBusProxy *proxy)
{
	printf("player_added \n");

	if (g_slist_length(players) == 0) {
		printf("=== add set last_connected_device_proxy 0x%p \n", proxy);
		last_connected_device_proxy = last_temp_connected_device_proxy;
		device_connected_post(last_connected_device_proxy);
		report_avrcp_event(DeviceInput::BT_SINK_ENV_CONNECT, NULL, 0);
		printf("[D: %s]: BT_SNK_DEVICE CONNECTED\n", __func__);
		system("hciconfig hci0 noscan");
		system("hciconfig hci0 noscan");
	}

	players = g_slist_append(players, proxy);

	if (default_player == NULL) {
		printf("set default player\n");
		default_player = proxy;
	}

	print_player(proxy, COLORED_NEW);
}
void print_item(GDBusProxy *proxy, const char *description)
{
	const char *path, *name;
	DBusMessageIter iter;

	path = g_dbus_proxy_get_path(proxy);

	if (g_dbus_proxy_get_property(proxy, "Name", &iter))
	 dbus_message_iter_get_basic(&iter, &name);
	else
	 name = "<unknown>";
}

void item_added(GDBusProxy *proxy)
{
	items = g_slist_append(items, proxy);

	print_item(proxy, COLORED_NEW);
}

void folder_added(GDBusProxy *proxy)
{
	folders = g_slist_append(folders, proxy);

	print_folder(proxy, COLORED_NEW);
}

static GDBusProxy *proxy_lookup(GList *list, int *index, const char *path,
						const char *interface)
{
	GList *l;

	if (!interface)
		return NULL;

	for (l = g_list_nth(list, index ? *index : 0); l; l = g_list_next(l)) {
		GDBusProxy *proxy = (GDBusProxy *)(l->data);
		const char *proxy_iface = g_dbus_proxy_get_interface(proxy);
		const char *proxy_path = g_dbus_proxy_get_path(proxy);

		if (index)
			(*index)++;

		/* Proxy info mybe error. */
		if (!proxy_iface || !proxy_path) {
			printf("ERROR: %s proxy info error! proxy_iface:%s, proxy_path:%s\n",
				__func__, proxy_iface, proxy_path);
			return NULL;
		}

		if (g_str_equal(proxy_iface, interface) == TRUE &&
			g_str_equal(proxy_path, path) == TRUE)
			return proxy;
		}

	return NULL;
}

static GDBusProxy *proxy_lookup_client(GDBusClient *client, int *index,
					   const char* path,
					   const char *interface)
{
	return proxy_lookup(proxy_list, index, path, interface);
}

static const char *load_connected_device(const char *str)
{
	int fd;
	int result;
	char path[64];

	sprintf(path, "%s/%s/reconnect", STORAGE_PATH, str);

	pr_info("Load path %s", path);

	result = access(path, F_OK);
	if (result == -1) {
		pr_info("%s doesnot exist", path);
		return NULL;
	}

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		error("Open %s error: %s", path,
			  strerror(errno));
		return NULL;
	}

	result = read(fd, last_obj_path, sizeof(last_obj_path) - 1);
	close(fd);

	if (result > 0) {
		pr_info("Previous device path: %s", last_obj_path);
		return last_obj_path;
	} else {
		error("Read %s error: %s", path,
			  strerror(errno));
		return NULL;
	}
}

static void store_connected_device(GDBusProxy *proxy)
{
	int fd;
	int result;
	const char *object_path;
	struct adapter *adapter = find_parent(proxy);
	char path[64];
	DBusMessageIter iter;
	const char *str;

	if (!adapter)
		return;

	if (g_dbus_proxy_get_property(adapter->proxy,
					  "Address", &iter) == FALSE) {
		pr_err("Get adapter address error");
		return;
	}

	dbus_message_iter_get_basic(&iter, &str);

	sprintf(path, "%s/%s/reconnect", STORAGE_PATH, str);
	pr_info("Store path: %s", path);

	object_path = g_dbus_proxy_get_path(proxy);

	pr_info("Connected device object path: %s", object_path);

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		error("Open %s error: %s", path,
			  strerror(errno));
		return;
	}

	result = write(fd, object_path, strlen(object_path) + 1);
	close(fd);

	if (result > 0)
		memcpy(last_obj_path, object_path, sizeof(last_obj_path) - 1);
	else
		error("Write %s error: %s", path, strerror(errno));
}

static void device_connected_post(GDBusProxy *proxy)
{
	int device_num = g_list_length(device_list);

	pr_info("Connected device number %d", device_num);

	if (!device_num)
		return;

	store_connected_device(proxy);
}

static void disconn_device_reply(DBusMessage * message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {

		if (strstr(error.name, "Failed"))
			printf("disconn_device_reply failed\n");
		dbus_error_free(&error);
	}
}

#define RECONN_INTERVAL	2
static void reconn_last_device_reply(DBusMessage * message, void *user_data)
{

	DBusError error;
	static int count = 1;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {

		if (strstr(error.name, "Failed") && (count > 0)) {
			pr_info("Retry to reconn_last connect, count %d", count);
			count--;
			reconnect_timer = g_timeout_add_seconds(RECONN_INTERVAL,
					reconn_last, NULL);
			dbus_error_free(&error);
			return;
		}

		report_avrcp_event(DeviceInput::BT_SINK_ENV_CONNECT_FAIL, NULL, 0);
		dbus_error_free(&error);
	}
	count = 1;
}

bool disconn_device(void)
{
	GDBusProxy *proxy = last_connected_device_proxy;
	DBusMessageIter iter, addr_iter;
	dbus_bool_t connected;

	if (!proxy) {
		error("Invalid proxy, stop disconnecting\n");
		return FALSE;
	}

	pr_info("disconnect g_dbus_proxy_get_path [0x%p]: %s\n", proxy, g_dbus_proxy_get_path(proxy));

	if (g_dbus_proxy_get_property(proxy, "Address",
				 &addr_iter) == TRUE) {
		const char *address;

		dbus_message_iter_get_basic(&addr_iter,
					 &address);
		pr_info("disconn_device addrs %s ", address);
	}

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_message_iter_get_basic(&iter, &connected);
		if (!connected)
			return FALSE;
	}

	if (g_list_length(device_list) <= 0) {
		error("Device already disconnected");
		return FALSE;
	}

	pr_info("disconnect target device: %s", g_dbus_proxy_get_path(proxy));

	if (g_dbus_proxy_method_call(proxy,
					 "Disconnect",
					 NULL,
					 disconn_device_reply,
					 last_connected_device_proxy, NULL) == FALSE) {
		error("Failed to call org.bluez.Device1.Disonnect");
	}

	return TRUE;
}

bool reconn_last(void)
{
	GDBusProxy *proxy = last_connected_device_proxy;
	DBusMessageIter addr_iter, addrType_iter;
	int fd, ret, reconnect = 1;
	char buff[100] = {0};

	fd = open("/userdata/cfg/bt_reconnect", O_RDONLY);
	if (fd > 0) {
		ret = read(fd, buff, sizeof(buff));
		if (ret > 0) {
			if (strstr(buff, "bluez-reconnect:disable"))
				reconnect = 0;
		}
		close(fd);
	}

	if (reconnect == 0) {
		printf("%s: automatic reconnection is disabled!", __func__);
		return 0;
	}

	if (bt_is_connected()) {
		printf("%s: The device is connected and does not need to be reconnected!", __func__);
		return 0;
	}

	if (reconnect_timer) {
		g_source_remove(reconnect_timer);
		reconnect_timer = 0;
	}

	printf("%s: lcdp: 0x%p, last_device_path: %s.\n",
		__func__, last_connected_device_proxy, last_device_path);

	if ((!last_connected_device_proxy) && last_device_path) {
		/* Check if the device exists */
		last_connected_device_proxy = proxy_lookup_client(
							btsrc_client, NULL,
							last_device_path,
							"org.bluez.Device1");
		proxy = last_connected_device_proxy;
	}

	if (!proxy) {
		error("Invalid proxy, stop reconnecting\n");
		return FALSE;
	}

	if (g_list_length(device_list) > 0) {
		error("Device device_list: %d.\n", g_list_length(device_list));
		//return FALSE;

	}

	pr_info("reconn_last target device: %s", g_dbus_proxy_get_path(proxy));
	if (g_dbus_proxy_get_property(proxy, "Address",
				 &addr_iter) == TRUE) {
		const char *address;

		dbus_message_iter_get_basic(&addr_iter,
					 &address);
		pr_info("disconn_device addrs %s ", address);
	}

	if (g_dbus_proxy_get_property(proxy, "AddressType",
			  &addrType_iter) == TRUE) {
		const char *addrType;

		dbus_message_iter_get_basic(&addrType_iter,
				  &addrType);
		pr_info("addrType %s ", addrType);
		if (strcmp(addrType, "random") == 0)
			return 0;
	}

	if (g_dbus_proxy_method_call(proxy,
					 "Connect",
					 NULL,
					 reconn_last_device_reply,
					 last_connected_device_proxy, NULL) == FALSE) {
		error("Failed to call org.bluez.Device1.Connect");
	}

	return FALSE;
}

static int adapter_is_powered(GDBusProxy *proxy)
{
	DBusMessageIter iter;
	dbus_bool_t powered;

	if (g_dbus_proxy_get_property(proxy, "Powered", &iter)) {
		dbus_message_iter_get_basic(&iter, &powered);
		if (powered)
			return 1;
	}

	return 0;
}

void a2dp_sink_device_added(GDBusProxy *proxy)
{
	const char *path = g_dbus_proxy_get_path(proxy);
	dbus_bool_t connected = 0;
	static int first = 1;
	DBusMessageIter iter;

	proxy_list = g_list_append(proxy_list, proxy);

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_message_iter_get_basic(&iter, &connected);

		if (connected) {
			device_list = g_list_append(device_list, proxy);
			printf("=== BT SINK set last_temp_connected_device_proxy %p \n", proxy);
			last_temp_connected_device_proxy = proxy;
			return;
		}
	}
}

void a2dp_sink_proxy_added(GDBusProxy *proxy, void *user_data)
{
	printf("BT SINK proxy_added A2DP_SINK_FLAG: %d\n", A2DP_SINK_FLAG);
	const char *interface;
	interface = g_dbus_proxy_get_interface(proxy);

	printf("BT SINK proxy_added interface:%s \n", interface);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_added(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
		//a2dp_sink_device_added(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		DBusMessageIter iter;
		const char *str;

		if (!g_dbus_proxy_get_property(proxy, "Address", &iter)) {
				pr_err("Failed to get adapter address");
				return;
		}

		dbus_message_iter_get_basic(&iter, &str);

		if (!first_ctrl)
			return;

		first_ctrl = 0;
		/* Load previous connected device */
		last_device_path = load_connected_device(str);
	}
}

 void player_removed(GDBusProxy *proxy)
{
	print_player(proxy, COLORED_DEL);

	if (default_player == proxy) {
		default_player = NULL;
	}

	players = g_slist_remove(players, proxy);
	if (g_slist_length(players) == 0) {
		printf("[D: %s]: BT_SNK_DEVICE DISCONNECTED\n", __func__);
		report_avrcp_event(DeviceInput::BT_SINK_ENV_DISCONNECT, NULL, 0);
		system("hciconfig hci0 piscan");
		system("hciconfig hci0 piscan");
	}
}

void item_removed(GDBusProxy *proxy)
{
	items = g_slist_remove(items, proxy);

	print_item(proxy, COLORED_DEL);
}

 void a2dp_sink_proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	printf("BT SINK proxy_removed A2DP_SINK_FLAG: %d\n", A2DP_SINK_FLAG);
	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_removed(proxy);
}

static void save_track_info(BtTrackInfo *track, const char *valstr, unsigned int valu32)
{
	if(!strcmp(track_key, "Title"))
		memcpy(track->title, valstr, strlen(valstr));
	else if(!strcmp(track_key, "Album"))
		memcpy(track->album, valstr, strlen(valstr));
	else if(!strcmp(track_key, "Artist"))
		memcpy(track->artist, valstr, strlen(valstr));
	else if(!strcmp(track_key, "Genre"))
		memcpy(track->genre, valstr, strlen(valstr));
	else if(!strcmp(track_key, "TrackNumber"))
		sprintf(track->track_num, "%d", valu32);
	else if(!strcmp(track_key, "NumberOfTracks"))
		sprintf(track->num_tracks, "%d", valu32);
	else if(!strcmp(track_key, "Duration")) {
		sprintf(track->playing_time, "%d", valu32);
		current_song_len = valu32;
	}

	memset(track_key, 0, 256);
}

static void avrcp_get_track_info(BtTrackInfo *track, const char *name,
					 DBusMessageIter *iter)
{
	dbus_uint32_t valu32;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_STRING:
		dbus_message_iter_get_basic(iter, &valstr);
		if(!strncmp(name, "Track Key", 9))
			memcpy(track_key, valstr, strlen(valstr));
		else if(!strncmp(name, "Track Value", 11))
			save_track_info(track, valstr, 0);
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		if(!strncmp(name, "Track Value", 11))
			save_track_info(track, NULL, (unsigned int)valu32);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		avrcp_get_track_info(track, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);
		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			avrcp_get_track_info(track, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		avrcp_get_track_info(track, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		avrcp_get_track_info(track, entry, &subiter);
		g_free(entry);
		break;
	}
}

static void avrcp_track_info_send(const char *name, DBusMessageIter *iter)
{
	char addr[18];
	DBusMessageIter addr_iter;
	BtTrackInfo track;

	if(strncmp(name, "Track", 5))
		return;

	memset(addr, 0, 18);
	bt_get_default_dev_addr(addr, 18);

	memset(track_key, 0, 256);
	memset(&track, 0, sizeof(BtTrackInfo));
	avrcp_get_track_info(&track, name, iter);

	if(g_avrcp_track_cb) {
		g_avrcp_track_cb(addr, track);
    }
}

static void avrcp_position_send(const char *name, DBusMessageIter *iter)
{
	dbus_uint32_t valu32;
	char addr[18];

	if(strncmp(name, "Position", 8))
		return;

	memset(addr, 0, 18);
	bt_get_default_dev_addr(addr, 18);

	dbus_message_iter_get_basic(iter, &valu32);

	if(g_avrcp_position_cb)
		g_avrcp_position_cb(addr, current_song_len, valu32);
}

void player_property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Player", COLORED_CHG);
	printf("player_property_changed: str: %s, name: %s\n", str, name);

	avrcp_track_info_send(name, iter);
	avrcp_position_send(name, iter);

	print_iter(str, name, iter);
	g_free(str);
}

void folder_property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Folder", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

void item_property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Item", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

void transport_property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter)
{
	char *str;
	const char *valstr;

	str = proxy_description(proxy, "MediaTransport1", COLORED_CHG);

	dbus_message_iter_get_basic(iter, &valstr);
	if (!strncmp(name, "State", 5)) {
		if (strstr(valstr, "active"))
			bt_sink_state_send(RK_BT_A2DP_SINK_STARTED);
		else if (strstr(valstr, "idle"))
			bt_sink_state_send(RK_BT_A2DP_SINK_SUSPENDED);
	}

	print_iter(str, name, iter);
	g_free(str);
}

void device_changed(GDBusProxy *proxy, DBusMessageIter *iter,
			   void *user_data)
{
	dbus_bool_t val;
	const char *object_path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(iter, &val);

	pr_info("%s connect status changed to %s", object_path,
		val ? "TRUE" : "FALSE");
	if (val) {
		device_list = g_list_append(device_list, proxy);
		last_temp_connected_device_proxy = proxy;
	} else {
		/* Device has been stored when being connected */
		device_list = g_list_remove(device_list, proxy);
	}
}

void adapter_changed(GDBusProxy *proxy, DBusMessageIter *iter,
			   void *user_data)
{
	dbus_bool_t val;

	dbus_message_iter_get_basic(iter, &val);

	pr_info("Adapter powered changed to %s", val ? "TRUE" : "FALSE");
}

void a2dp_sink_property_changed(GDBusProxy *proxy, const char *name,
					 DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	printf("BT SINK: property_changed %s\n", interface);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_TRANSPORT_INTERFACE))
		transport_property_changed(proxy, name, iter);
}

bool check_default_player(void)
{
	if (!default_player) {
		if (NULL != players) {
			GSList *l;
			l = players;
			GDBusProxy *proxy = (GDBusProxy *)l->data;
			default_player = proxy;
			printf("set default player\n");
			return TRUE;
		}
		printf("No default player available\n");
		return FALSE;
	}
	printf(" player ok\n");

	return TRUE;
}

gboolean option_version = FALSE;

GOptionEntry options[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
			 "Show version information and exit" },
	{ NULL },
};

int init_avrcp_ctrl(void)
{
	proxy_list = NULL;
	last_device_path = NULL;
	last_connected_device_proxy = NULL;
	device_list = NULL;
	reconnect_timer = 0;

	return 1;
}

extern volatile bool A2DP_SRC_FLAG;
int a2dp_sink_open(void)
{
	printf("call avrcp_thread init_avrcp\n");
	g_btsrc_connect_status = RK_BT_SINK_STATE_IDLE;
	A2DP_SINK_FLAG = true;
	A2DP_SRC_FLAG = 0;
	system("hciconfig hci0 piscan");
	system("hciconfig hci0 piscan");

	return 1;
}

int a2dp_sink_colse_coexist()
{
	A2DP_SINK_FLAG = false;
	return 0;
}

int release_avrcp_ctrl(void)
{
	//g_main_loop_quit(main_loop);
	printf("=== release_avrcp_ctrl ===\n");
	A2DP_SINK_FLAG = false;
	g_btsrc_connect_status = RK_BT_SINK_STATE_IDLE;
	system("hciconfig hci0 noscan");
	system("hciconfig hci0 noscan");
	return 0;
}

int release_avrcp_ctrl2(void)
{
	printf("=== release_avrcp_ctrl2 ===\n");
	A2DP_SINK_FLAG = false;
	g_btsrc_connect_status = RK_BT_SINK_STATE_IDLE;
	return 0;
}

void play_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
	  printf("Failed to play\n");
	  dbus_error_free(&error);
	  return;
	}

	printf("Play successful\n");
}

int play_avrcp(void)
{
	if (!check_default_player())
		return -1;
	if (g_dbus_proxy_method_call(default_player, "Play", NULL, play_reply,
				  NULL, NULL) == FALSE) {
		printf("Failed to play\n");
		return -1;
	}
	printf("Attempting to play\n");
	return 0;
}

void pause_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to pause: %s\n", __func__);
		dbus_error_free(&error);
		return;
	}

	printf("Pause successful\n");
}

int pause_avrcp(void)
{
	if (!check_default_player())
		return -1;
	if (g_dbus_proxy_method_call(default_player, "Pause", NULL,
					pause_reply, NULL, NULL) == FALSE) {
		printf("Failed to pause\n");
		return -1;
	}
	printf("Attempting to pause\n");
	return 0;
}

void volumedown_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to volume down\n");
		dbus_error_free(&error);
		return;
	}

	printf("volumedown successful\n");
}

void volumedown_avrcp(void)
{
	if (!check_default_player())
				return;
	if (g_dbus_proxy_method_call(default_player, "VolumeDown", NULL, volumedown_reply,
							NULL, NULL) == FALSE) {
		printf("Failed to volumeup\n");
		return;
	}
	printf("Attempting to volumeup\n");
}

void volumeup_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to volumeup\n");
		dbus_error_free(&error);
		return;
	}

	printf("volumeup successful\n");
}


void volumeup_avrcp()
{
	if (!check_default_player())
				return;
	if (g_dbus_proxy_method_call(default_player, "VolumeUp", NULL, volumeup_reply,
							NULL, NULL) == FALSE) {
		printf("Failed to volumeup\n");
		return;
	}
	printf("Attempting to volumeup\n");
}

void stop_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		//rl_printf("Failed to stop: %s\n", error.name);
		dbus_error_free(&error);
		return;
	}

	printf("Stop successful\n");
}

int stop_avrcp()
{
	if (!check_default_player())
			return -1;
	if (g_dbus_proxy_method_call(default_player, "Stop", NULL, stop_reply,
							NULL, NULL) == FALSE) {
		printf("Failed to stop\n");
		return -1;
	}
	printf("Attempting to stop\n");

	return 0;
}

void next_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to jump to next\n");
		dbus_error_free(&error);
		return;
	}

	printf("Next successful\n");
}

int next_avrcp(void)
{
	{
		if (!check_default_player())
			return -1;
		if (g_dbus_proxy_method_call(default_player, "Next", NULL, next_reply,
								NULL, NULL) == FALSE) {
			printf("Failed to jump to next\n");
			return -1;
		}
		printf("Attempting to jump to next\n");
	}

	return 0;
}

void previous_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to jump to previous\n");
		dbus_error_free(&error);
		return;
	}

	printf("Previous successful\n");
}

int previous_avrcp(void)
{

	if (!check_default_player())
		return -1;
	if (g_dbus_proxy_method_call(default_player, "Previous", NULL,
					previous_reply, NULL, NULL) == FALSE) {
		printf("Failed to jump to previous\n");
		return -1;
	}
	printf("Attempting to jump to previous\n");

	return 0;
}

void fast_forward_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to fast forward\n");
		dbus_error_free(&error);
		return;
	}

	printf("FastForward successful\n");
}

void fast_forward_avrcp() {
	{
		if (!check_default_player())
				return;
		if (g_dbus_proxy_method_call(default_player, "FastForward", NULL,
					fast_forward_reply, NULL, NULL) == FALSE) {
			printf("Failed to jump to previous\n");
			return;
		}
		printf("Fast forward playback\n");
	}
}

void rewind_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to rewind\n");
		dbus_error_free(&error);
		return;
	}

	printf("Rewind successful\n");
}

void rewind_avrcp(){
	{
		if (!check_default_player())
				return;
		if (g_dbus_proxy_method_call(default_player, "Rewind", NULL,
						rewind_reply, NULL, NULL) == FALSE) {
			printf("Failed to rewind\n");
			return;
		}
		printf("Rewind playback\n");
	}
}

int getstatus_avrcp(void)
{
	GDBusProxy *proxy;
	DBusMessageIter iter;
	const char *valstr;
	if (check_default_player() == FALSE)
		return AVRCP_PLAY_STATUS_ERROR; //default player no find
	proxy = default_player;
	if (g_dbus_proxy_get_property(proxy, "Status", &iter) == FALSE)
			return AVRCP_PLAY_STATUS_ERROR; //unkonw status
	dbus_message_iter_get_basic(&iter, &valstr);
	//printf("----getstatus_avrcp,rtl wifi,return %s--\n",valstr);
	if (!strcasecmp(valstr, "stopped"))
		return AVRCP_PLAY_STATUS_STOPPED;
	else if (!strcasecmp(valstr, "playing"))
		return AVRCP_PLAY_STATUS_PLAYING;
	else if (!strcasecmp(valstr, "paused"))
		return AVRCP_PLAY_STATUS_PAUSED;
	else if (!strcasecmp(valstr, "forward-seek"))
		return AVRCP_PLAY_STATUS_FWD_SEEK;
	else if (!strcasecmp(valstr, "reverse-seek"))
		return AVRCP_PLAY_STATUS_REV_SEEK;
	else if (!strcasecmp(valstr, "error"))
		return AVRCP_PLAY_STATUS_ERROR;

	return AVRCP_PLAY_STATUS_ERROR;
}

void a2dp_sink_register_cb(RK_BT_SINK_CALLBACK cb)
{
	g_btsink_cb = cb;
}

void a2dp_sink_register_track_cb(RK_BT_AVRCP_TRACK_CHANGE_CB cb)
{
	g_avrcp_track_cb = cb;
}

void a2dp_sink_register_position_cb(RK_BT_AVRCP_PLAY_POSITION_CB cb)
{
	g_avrcp_position_cb = cb;
}

void a2dp_sink_clear_cb()
{
	g_btsink_cb = NULL;
	g_avrcp_track_cb = NULL;
	g_avrcp_position_cb = NULL;
}

int a2dp_sink_status(RK_BT_SINK_STATE *pState)
{
	int avrcp_status;

	if (!pState)
		return -1;

	avrcp_status = getstatus_avrcp();
	switch (avrcp_status) {
		case AVRCP_PLAY_STATUS_STOPPED:
			*pState = RK_BT_SINK_STATE_STOP;
			break;
		case AVRCP_PLAY_STATUS_REV_SEEK:
		case AVRCP_PLAY_STATUS_FWD_SEEK:
		case AVRCP_PLAY_STATUS_PLAYING:
			*pState = RK_BT_SINK_STATE_PLAY;
			break;
		case AVRCP_PLAY_STATUS_PAUSED:
			*pState = RK_BT_SINK_STATE_PAUSE;
			break;
		default:
			if (g_btsrc_connect_status == RK_BT_SINK_STATE_CONNECT)
				*pState = RK_BT_SINK_STATE_CONNECT;
			else
				*pState = RK_BT_SINK_STATE_IDLE;
			break;
	}

	return 0;
}
