#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/signalfd.h>

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

bool system_command(const char* cmd) {
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

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

static struct adapter *default_ctrl;
static GList *ctrl_list;
static GDBusProxy *default_dev;
static GDBusProxy *default_attr;

#undef bt_shell_printf
#define bt_shell_printf printf

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
		if (!strncmp(name, "Connected", 9)) {
			if (value)
				report_avrcp_event(DeviceInput::BT_CONNECT, &value, sizeof(value));
			else
				report_avrcp_event(DeviceInput::BT_DISCONNECT, &value, sizeof(value));
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

done:
	g_free(desc);
}

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

	/*Log::debug("%s%s%sFolder %s\n", description ? "[" : "",
					description ? : "",
					description ? "] " : "",
					path);*/
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
 
     /*Log::debug("%s%s%sItem %s %s\n", description ? "[" : "",
                     description ? : "",
                     description ? "] " : "",
                     path, name);*/
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

 void proxy_added(GDBusProxy *proxy, void *user_data)
 {
    printf("proxy_added \n");
     const char *interface;
     interface = g_dbus_proxy_get_interface(proxy);

     printf("proxy_added interface:%s \n", interface);
	 
	if (!strcmp(interface, "org.bluez.Adapter1")) {
		printf("	 new org.bluez.Adapter1\n");
		{
			struct adapter *adapter;
			adapter = find_ctrl(ctrl_list, g_dbus_proxy_get_path(proxy));
			if (!adapter)
			adapter = adapter_new(proxy);
			adapter->proxy = proxy;
		}
	}

     if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
         player_added(proxy);
     else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
         folder_added(proxy);
     else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
         item_added(proxy);
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
        if(NULL != players) {
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
 gboolean option_version = FALSE;
 
 GOptionEntry options[] = {
     { "version", 'v', 0, G_OPTION_ARG_NONE, &option_version,
                 "Show version information and exit" },
     { NULL },
 };

void *init_avrcp(void *)
{
	GError *error = NULL;
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

 int init_avrcp_ctrl() {
    pthread_t avrcp_thread;
    
    pthread_create(&avrcp_thread, NULL, init_avrcp, NULL);
    return 1;
}

int release_avrcp_ctrl() {
    g_dbus_client_unref(client);    
    dbus_connection_unref(dbus_conn);
    g_main_loop_unref(main_loop);
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

int play_avrcp(){
	{
	    if (!check_default_player())
	        return -1;
	    if (g_dbus_proxy_method_call(default_player, "Play", NULL, play_reply,
	                  NULL, NULL) == FALSE) {
	        printf("Failed to play\n");
	        return -1;
	    }
	    printf("Attempting to play\n");
	}
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

int pause_avrcp(){
	{
		if (!check_default_player())
			return -1;
		if (g_dbus_proxy_method_call(default_player, "Pause", NULL,
						pause_reply, NULL, NULL) == FALSE) {
			printf("Failed to pause\n");
			return -1;
		}
		printf("Attempting to pause\n");
	}
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

void volumedown_avrcp(){
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


void volumeup_avrcp(){
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

int stop_avrcp(){
	{
	    if (!check_default_player())
	            return -1;
	    if (g_dbus_proxy_method_call(default_player, "Stop", NULL, stop_reply,
	                            NULL, NULL) == FALSE) {
	        printf("Failed to stop\n");
	        return -1;
	    }
	    printf("Attempting to stop\n");
	}

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

int next_avrcp()
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

int previous_avrcp()
{
	{
		if (!check_default_player())
			return -1;
		if (g_dbus_proxy_method_call(default_player, "Previous", NULL,
						previous_reply, NULL, NULL) == FALSE) {
			printf("Failed to jump to previous\n");
			return -1;
		}
		printf("Attempting to jump to previous\n");
	}

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

int getstatus_avrcp()
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
