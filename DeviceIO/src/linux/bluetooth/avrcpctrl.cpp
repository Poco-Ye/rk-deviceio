#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/signalfd.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <readline/readline.h>
#include <readline/history.h>
#include <glib.h>
#include "avrcpctrl.h"
#include "gdbus.h"

#include "DeviceIo/DeviceIo.h"
#include "../Timer.h"

using DeviceIOFramework::Timer;
using DeviceIOFramework::TimerManager;
using DeviceIOFramework::TimerNotify;
using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

static void report_avrcp_event(DeviceInput event, void *data, int len) {
    if (DeviceIo::getInstance()->getNotify())
        DeviceIo::getInstance()->getNotify()->callback(event, data, len);
}

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

static const char *last_device_path;
static char last_obj_path[] = "/org/bluez/hci0/dev_xx_xx_xx_xx_xx_xx";
static GDBusProxy *last_connected_device_proxy;
static GList *device_list;
static guint reconnect_timer;
#define STORAGE_PATH "/data/cfg/lib/bluetooth"


#define BLUEZ_MEDIA_PLAYER_INTERFACE "org.bluez.MediaPlayer1"
#define BLUEZ_MEDIA_FOLDER_INTERFACE "org.bluez.MediaFolder1"
#define BLUEZ_MEDIA_ITEM_INTERFACE "org.bluez.MediaItem1"

DBusConnection *dbus_conn;
GDBusProxy *default_player;
GSList *players = NULL;
GSList *folders = NULL;
GSList *items = NULL;
GDBusClient *client;
GMainLoop *main_loop = NULL;
static int first_ctrl = 1;
void a2dp_sink_cmd_power(bool powered);

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

void rkbt_inquiry_scan(bool tf)
{
	if (tf)
		system_command("hciconfig hci0 piscan");
	else
		system_command("hciconfig hci0 noscan");
}

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

static struct adapter *default_ctrl;
static GList *ctrl_list;
static GDBusProxy *default_dev;
static GDBusProxy *default_attr;

static void print_fixed_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t *valbool;
	dbus_uint32_t *valu32;
	dbus_uint16_t *valu16;
	dbus_int16_t *vals16;
	unsigned char *byte;
	int len;

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_fixed_array(iter, &valbool, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)valbool, len * sizeof(*valbool));

		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_fixed_array(iter, &valu32, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)valu32, len * sizeof(*valu32));

		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_fixed_array(iter, &valu16, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)valu16, len * sizeof(*valu16));

		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_fixed_array(iter, &vals16, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)vals16, len * sizeof(*vals16));

		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_fixed_array(iter, &byte, &len);

		if (len <= 0)
			return;

		bt_shell_printf("%s%s:\n", label, name);
		//bt_shell_hexdump((void *)byte, len * sizeof(*byte));

		break;
	default:
		return;
	};
}

static void print_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
	unsigned char byte;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;
	int value = 0;

	if (iter == NULL) {
		bt_shell_printf("%s%s is nil\n", label, name);
		return;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		bt_shell_printf("%s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		bt_shell_printf("%s%s: %s\n", label, name, valstr);
		if (!strncmp(name, "Status", 6))
		{
			if (strstr(valstr, "playing"))
				report_avrcp_event(DeviceInput::BT_START_PLAY, &value, sizeof(value));
			if (strstr(valstr, "paused"))
				report_avrcp_event(DeviceInput::BT_PAUSE_PLAY, &value, sizeof(value));
		}
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		bt_shell_printf("%s%s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
		value = valbool ? 1 : 0;
		if (!strncmp(name, "Connected", 9)) {//ServicesResolved
			if (value) {
				rkbt_inquiry_scan(0);
				report_avrcp_event(DeviceInput::BT_CONNECT, &value, sizeof(value));
			} else {
				rkbt_inquiry_scan(1);
				report_avrcp_event(DeviceInput::BT_DISCONNECT, &value, sizeof(value));
			}
		}
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		bt_shell_printf("%s%s: 0x%08x\n", label, name, valu32);
		if (!strncmp(name, "Position", 8)) {
			if (valu32 == 0)
				report_avrcp_event(DeviceInput::BT_STOP_PLAY, &value, sizeof(value));
		}	
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		bt_shell_printf("%s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		bt_shell_printf("%s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		bt_shell_printf("%s%s: 0x%02x\n", label, name, byte);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);

		if (dbus_type_is_fixed(
				dbus_message_iter_get_arg_type(&subiter))) {
			print_fixed_iter(label, name, &subiter);
			break;
		}

		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		print_iter(label, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		print_iter(label, entry, &subiter);
		g_free(entry);
		break;
	default:
		bt_shell_printf("%s%s has unsupported type\n", label, name);
		break;
	}
}

static gboolean reconn_device(void *user_data);
gboolean reconn_last(void);

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

static struct adapter *find_ctrl(GList *source, const char *path)
{
	GList *list;

	for (list = g_list_first(source); list; list = g_list_next(list)) {
		struct adapter *adapter = (struct adapter *)list->data;

		if (!strcasecmp(g_dbus_proxy_get_path(adapter->proxy), path))
			return adapter;
	}

	return NULL;
}

static void set_default_device(GDBusProxy *proxy, const char *attribute)
{
	char *desc = NULL;
	DBusMessageIter iter;
	const char *path;

	default_dev = proxy;

	if (proxy == NULL) {
		default_attr = NULL;
		goto done;
	}

	if (!g_dbus_proxy_get_property(proxy, "Alias", &iter)) {
		if (!g_dbus_proxy_get_property(proxy, "Address", &iter))
			goto done;
	}

	path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(&iter, &desc);
	desc = g_strdup_printf("[%s%s%s]# ", desc,
				attribute ? ":" : "",
				attribute ? attribute + strlen(path) : "");
	printf("%s desc: %s\n", __func__, desc);

done:
	g_free(desc);
}

static struct adapter *adapter_new(GDBusProxy *proxy)
{
	struct adapter *adapter = (struct adapter *)g_malloc0(sizeof(struct adapter));

	ctrl_list = g_list_append(ctrl_list, adapter);

	if (!default_ctrl)
		default_ctrl = adapter;

	return adapter;
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
	players = g_slist_append(players, proxy);

	if (default_player == NULL){
	    printf("set default player");
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

static void adapter_added(GDBusProxy *proxy)
{
	struct adapter *adapter;
	adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
	if (!adapter)
		adapter = adapter_new(proxy);

	adapter->proxy = proxy;
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
	static int count = 3;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {

		if (strstr(error.name, "Failed"))
			printf("disconn_device_reply failed\n");
		dbus_error_free(&error);
	}
}

#define RECONN_INTERVAL	2
static void reconn_device_reply(DBusMessage * message, void *user_data)
{

	DBusError error;
	static int count = 3;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {

		if (strstr(error.name, "Failed") && (count > 0)) {
			pr_info("Retry to connect, count %d", count);
			count--;
			reconnect_timer = g_timeout_add_seconds(RECONN_INTERVAL,
					reconn_device, user_data);
		}

		dbus_error_free(&error);
	}
}

static void reconn_last_device_reply(DBusMessage * message, void *user_data)
{

	DBusError error;
	static int count = 3;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {

		if (strstr(error.name, "Failed") && (count > 0)) {
			pr_info("Retry to reconn_last connect, count %d", count);
			count--;
			reconnect_timer = g_timeout_add_seconds(RECONN_INTERVAL,
					reconn_last, user_data);
		}

		dbus_error_free(&error);
	}
}


gboolean disconn_device(void)
{
	GDBusProxy *proxy = last_connected_device_proxy;
	DBusMessageIter iter;

	if (!proxy) {
		error("Invalid proxy, stop disconnecting");
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

	return FALSE;
}

gboolean reconn_last(void)
{
	GDBusProxy *proxy = last_connected_device_proxy;
	DBusMessageIter iter;

	if (reconnect_timer) {
		g_source_remove(reconnect_timer);
		reconnect_timer = 0;
	}

	if (!proxy) {
		error("Invalid proxy, stop reconnecting");
		return FALSE;
	}

	if (g_list_length(device_list) > 0) {
		error("Device already connected");
		return FALSE;

	}

	pr_info("reconn_last target device: %s", g_dbus_proxy_get_path(proxy));

	if (g_dbus_proxy_method_call(proxy,
				     "Connect",
				     NULL,
				     reconn_last_device_reply,
				     last_connected_device_proxy, NULL) == FALSE) {
		error("Failed to call org.bluez.Device1.Connect");
	}

	return FALSE;
}


static gboolean reconn_device(void *user_data)
{
	GDBusProxy *proxy = (GDBusProxy *)user_data;
	DBusMessageIter iter;

	if (reconnect_timer) {
		g_source_remove(reconnect_timer);
		reconnect_timer = 0;
	}

	if (!proxy) {
		error("Invalid proxy, stop reconnecting");
		return FALSE;
	}

	if (g_list_length(device_list) > 0) {
		error("Device already connected");
		return FALSE;

	}

	pr_info("Connect target device: %s", g_dbus_proxy_get_path(proxy));

	if (g_dbus_proxy_method_call(proxy,
				     "Connect",
				     NULL,
				     reconn_device_reply,
				     user_data, NULL) == FALSE) {
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

static void device_added(GDBusProxy *proxy)
{
	const char *path = g_dbus_proxy_get_path(proxy);
	dbus_bool_t connected;
	static int first = 1;
	DBusMessageIter iter;
	struct adapter *adapter = find_parent(proxy);

	if (!adapter) {
		/* TODO: Error */
		return;
	}

	adapter->devices = g_list_append(adapter->devices, proxy);

	if (g_dbus_proxy_get_property(proxy, "Connected", &iter)) {
		dbus_message_iter_get_basic(&iter, &connected);

		printf("%s, path: %s, connected: %d, adapter_is_powered: %d.\n", __func__,
				path, connected, adapter_is_powered(default_ctrl->proxy));
	
		if (connected) {
			device_list = g_list_append(device_list, proxy);
			last_connected_device_proxy = proxy;
			device_connected_post(proxy);
			return;
		}

		/* Device will be connected when adapter is powered */
		if (!adapter_is_powered(default_ctrl->proxy)) {
			return;
		}

		if (last_device_path && !strcmp(path, last_device_path) && first) {
			pr_info("Reconnecting to last connected device");
			first = 0;
			reconnect_timer = g_timeout_add_seconds(RECONN_INTERVAL,
						reconn_device, (void *)proxy);
		}
	}
}

void proxy_added(GDBusProxy *proxy, void *user_data)
{
	printf("proxy_added \n");
	const char *interface;
	interface = g_dbus_proxy_get_interface(proxy);

	printf("proxy_added interface:%s \n", interface);
	 
	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_added(proxy);

	proxy_list = g_list_append(proxy_list, proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
		device_added(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		DBusMessageIter iter;
		const char *str;

		adapter_added(proxy);

		if (!first_ctrl)
			return;

		if (!g_dbus_proxy_get_property(proxy, "Address", &iter)) {
			pr_err("Failed to get adapter address");
			return;
		}

		dbus_message_iter_get_basic(&iter, &str);
		//a2dp_sink_cmd_power(TRUE);

		first_ctrl = 0;
		/* Load previous connected device */
		last_device_path = load_connected_device(str);
	}

 }

 void player_removed(GDBusProxy *proxy)
{
	print_player(proxy, COLORED_DEL);

	if (default_player == proxy)
		default_player = NULL;

	players = g_slist_remove(players, proxy);
}

void item_removed(GDBusProxy *proxy)
{
	items = g_slist_remove(items, proxy);

	print_item(proxy, COLORED_DEL);
}

 void proxy_removed(GDBusProxy *proxy, void *user_data)
{
   printf("proxy_removed \n");
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_removed(proxy);
}

void player_property_changed(GDBusProxy *proxy, const char *name,
                     DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Player", COLORED_CHG);
	printf("player_property_changed: str: %s, name: %s\n", str, name);

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

static GDBusProxy *proxy_lookup(GList *list, int *index, const char *path,
						const char *interface)
{
	GList *l;

	if (!interface)
		return NULL;

	for (l = g_list_nth(list, index ? *index : 0); l; l = g_list_next(l)) {
		GDBusProxy *proxy = l->data;
		const char *proxy_iface = g_dbus_proxy_get_interface(proxy);
		const char *proxy_path = g_dbus_proxy_get_path(proxy);

		if (index)
			(*index)++;

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

static void device_changed(GDBusProxy *proxy, DBusMessageIter *iter,
			   void *user_data)
{
	dbus_bool_t val;
	const char *object_path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(iter, &val);

	pr_info("%s connect status changed to %s", object_path,
		val ? "TRUE" : "FALSE");
	if (val) {
		device_list = g_list_append(device_list, proxy);
		device_connected_post(proxy);
	} else {
		/* Device has been stored when being connected */
		device_list = g_list_remove(device_list, proxy);
	}
}

static void adapter_changed(GDBusProxy *proxy, DBusMessageIter *iter,
			   void *user_data)
{
	dbus_bool_t val;
	const char *object_path = g_dbus_proxy_get_path(proxy);

	dbus_message_iter_get_basic(iter, &val);

	pr_info("Adapter powered changed to %s", val ? "TRUE" : "FALSE");
	if (val) {
		GDBusProxy *device_proxy;
		char *device_interface = "org.bluez.Device1";

		if (reconnect_timer) {
			g_source_remove(reconnect_timer);
			reconnect_timer = 0;
		}

		if (!last_device_path)
			return;

		pr_info("Reconnecting %s", last_device_path);

		/* Check if the device exists */
		device_proxy = proxy_lookup_client(client, NULL,
						   last_device_path,
						   device_interface);
		if (!device_proxy) {
			pr_err("No device proxy");
			return;
		}

		reconn_device(device_proxy);
	}
}

void property_changed(GDBusProxy *proxy, const char *name,
                     DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	printf("property_changed %s\n", interface);

	if (!strcmp(interface, "org.bluez.Device1")) {
		if (default_ctrl && device_is_child(proxy,
				 default_ctrl->proxy) == TRUE) {
			DBusMessageIter addr_iter;
			char *str;

			if (g_dbus_proxy_get_property(proxy, "Address",
						 &addr_iter) == TRUE) {
				const char *address;

				dbus_message_iter_get_basic(&addr_iter,
							 &address);
				str = g_strdup_printf("[CHG] Device %s ", address);
			} else
				str = g_strdup("");

			if (strcmp(name, "Connected") == 0) {
				dbus_bool_t connected;

				dbus_message_iter_get_basic(iter, &connected);

				if (connected && default_dev == NULL)
					set_default_device(proxy, NULL);
				else if (!connected && default_dev == proxy)
					set_default_device(NULL, NULL);
			}

			if (!strcmp(name, "Connected"))
				device_changed(proxy, iter, user_data);

			printf("Device1 str: %s, name: %s\n", str, name);
			print_iter(str, name, iter);
			g_free(str);
		}
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		DBusMessageIter addr_iter;
		char *str;

		if (g_dbus_proxy_get_property(proxy, "Address",
				 &addr_iter) == TRUE) {
			const char *address;

			dbus_message_iter_get_basic(&addr_iter, &address);
			str = g_strdup_printf("[CHG] Controller %s ", address);
		} else
			str = g_strdup("");

		if (!strcmp(name, "Powered"))
			adapter_changed(proxy, iter, user_data);

		print_iter(str, name, iter);
		g_free(str);
	}

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_property_changed(proxy, name, iter);
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
     //printf("No default player available\n");
     return FALSE;
    }
    //printf(" player ok\n");

    return TRUE;
}

static void generic_callback(const DBusError *error, void *user_data)
{
    char *str = (char *)user_data;

    if (dbus_error_is_set(error)) {
        printf("Failed to set %s: %s\n", str, error->name);
        return ;
    } else {
        printf("Changing %s succeeded\n", str);
        return ;
    }
}

static gboolean check_default_ctrl(void)
{
	int retry = 20;

    while ((!default_ctrl) && (--retry)) {
		usleep(100000);
    }

	if (!default_ctrl) {
		printf("No default controller available\n");
		return FALSE;
	}

    return TRUE;
}

void a2dp_sink_cmd_power(bool ispowered)
{
    dbus_bool_t powered = ispowered;
    char *str;
	printf("=== a2dp_sink_cmd_power ===\n");
	if (check_default_ctrl() == FALSE)
		return;

    str = g_strdup_printf("power %s", powered == TRUE ? "on" : "off");
	printf("=== a2dp_sink_cmd_power str: %s ===\n", str);

    if (g_dbus_proxy_set_property_basic(default_ctrl->proxy, "Powered",
                    DBUS_TYPE_BOOLEAN, &powered,
                    generic_callback, str, g_free) == TRUE)
        return;

    g_free(str);
}

gboolean option_version = FALSE;

GOptionEntry options[] = {
	{ "version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
	         "Show version information and exit" },
	{ NULL },
};

static void a2dp_sink_clean(void)
{
	default_ctrl = NULL;
	ctrl_list = NULL;
	default_dev = NULL;
	default_attr = NULL;
	dbus_conn = NULL;
}

void *init_avrcp(void *)
{
	GError *error = NULL;

	a2dp_sink_clean();

	dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);
	printf("init_avrcp start \n");


	if (NULL== dbus_conn) {
		printf("dbus init fail!");
		return NULL;
	}

	client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");
	if (NULL == client) {
		printf("client inti fail");
		dbus_connection_unref(dbus_conn);
		return NULL;
	}
	main_loop = g_main_loop_new(NULL, FALSE);

	g_dbus_client_set_connect_watch(client, connect_handler, NULL);
	g_dbus_client_set_disconnect_watch(client, disconnect_handler, NULL);

	g_dbus_client_set_proxy_handlers(client, proxy_added, proxy_removed,
	                      property_changed, NULL);
	printf("init ok\n");
	g_main_loop_run(main_loop);
}

int init_avrcp_ctrl(void)
{
	pthread_t avrcp_thread;
	printf("call avrcp_thread init_avrcp ...\n");

	pthread_create(&avrcp_thread, NULL, init_avrcp, NULL);
	return 1;
}

int release_avrcp_ctrl(void)
{
	g_main_loop_quit(main_loop);
    g_dbus_client_unref(client);    
    dbus_connection_unref(dbus_conn);
    g_main_loop_unref(main_loop);
	a2dp_sink_clean();
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
