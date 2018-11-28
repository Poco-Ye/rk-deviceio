#include <string.h>
#include <unistd.h>

#include "gst_player.h"
#include "DeviceIo/BtsrcParameter.h"
#include "DeviceIo/DeviceIo.h"

using DeviceIOFramework::DeviceIo;
using DeviceIOFramework::DeviceInput;

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomPlayer {
    GstElement *playbin;   /* Our one and only element */
    gboolean playing;      /* Are we in the PLAYING state? */
    gboolean terminate;    /* Should we terminate execution? */
    gboolean seek_enabled; /* Is seeking enabled for this media? */
    gboolean seek_done;    /* Have we performed the seek already? */
    gint64 duration;       /* How long does this media last, in nanoseconds */
    gint64 current;        /* Current position */
} CustomPlayer;

static CustomPlayer g_player;

static void report_player_event(DeviceInput event, void *data, int len) {
    if (DeviceIo::getInstance()->getNotify())
        DeviceIo::getInstance()->getNotify()->callback(event, data, len);
}

/* Forward definition of the message processing function */
static void handle_message (GstMessage *msg)
{
    GError *err;
    gchar *debug_info;
    char err_buff[1024] = {'\0'};

    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error (msg, &err, &debug_info);
            g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
            g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
            sprintf(err_buff, "Error received from element %s: %s",
                    GST_OBJECT_NAME (msg->src), err->message);
            report_player_event(DeviceInput::GST_PLAYER_ERROR, err_buff, strlen(err_buff));
            g_clear_error (&err);
            g_free (debug_info);
            g_player.terminate = TRUE;
            break;
        case GST_MESSAGE_EOS:
            g_print ("End-Of-Stream reached.\n");
            report_player_event(DeviceInput::GST_PLAYER_EOS, NULL, 0);
            g_player.terminate = TRUE;
            break;
        case GST_MESSAGE_DURATION:
            /* The duration has changed, mark the current one as invalid */
            g_player.duration = GST_CLOCK_TIME_NONE;
            report_player_event(DeviceInput::GST_PLAYER_DURATION, NULL, 0);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

            if (GST_MESSAGE_SRC (msg) == GST_OBJECT (g_player.playbin)) {
                g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));

                switch (new_state) {
                    case GST_STATE_PLAYING:
                        report_player_event(DeviceInput::GST_PLAYER_PLAYING, NULL, 0);
                        break;
                    case GST_STATE_PAUSED:
                        report_player_event(DeviceInput::GST_PLAYER_PAUSED, NULL, 0);
                        break;
                    case GST_STATE_READY:
                        report_player_event(DeviceInput::GST_PLAYER_READY, NULL, 0);
                        break;
                    default:
                        break;
                }

                /* Remember whether we are in the PLAYING state or not */
                g_player.playing = (new_state == GST_STATE_PLAYING);

                if (g_player.playing) {
                    /* We just moved to PLAYING. Check if seeking is possible */
                    GstQuery *query;
                    gint64 start, end;
                    query = gst_query_new_seeking (GST_FORMAT_TIME);
                    if (gst_element_query (g_player.playbin, query)) {
                        gst_query_parse_seeking (query, NULL, &g_player.seek_enabled, &start, &end);
                        if (g_player.seek_enabled)
                            report_player_event(DeviceInput::GST_PLAYER_SEEKABLE, NULL, 0);
                        else
                            g_print ("Seeking is DISABLED for this stream.\n");
                    } else {
                        g_printerr ("Seeking query failed.");
                    }
                    gst_query_unref (query);
                }
            }
            break;
        }
        default:
            /* We should not reach here */
            g_printerr ("Unexpected message received.\n");
            break;
    }
    gst_message_unref (msg);
}

void *listen_playbin_bus(void *)
{
    GstBus *bus;
    GstMessage *msg;
    GstMessageType listen_flag;

    listen_flag = (GstMessageType)(GST_MESSAGE_STATE_CHANGED |
                                   GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
                                   GST_MESSAGE_DURATION);
    /* Listen to the bus */
    bus = gst_element_get_bus (g_player.playbin);
    do {
        msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND, listen_flag);
        /* Parse message */
        if (msg != NULL) {
            handle_message (msg);
            continue;
        }
        /* We got no message, this means the timeout expired */
        if (g_player.playing) {
            /* Query the current position of the stream */
            if (!gst_element_query_position (g_player.playbin, GST_FORMAT_TIME, &g_player.current)) {
                g_printerr ("Could not query current position.\n");
                g_player.current = -1;
            }

            /* If we didn't know it yet, query the stream duration */
            if (!GST_CLOCK_TIME_IS_VALID (g_player.duration)) {
                if (!gst_element_query_duration (g_player.playbin, GST_FORMAT_TIME, &g_player.duration)) {
                    g_printerr ("Could not query current duration.\n");
                    g_player.duration = GST_CLOCK_TIME_NONE;
                }
            }
        }
    } while (!g_player.terminate);

    /* Free resources */
    gst_object_unref (bus);
    gst_element_set_state (g_player.playbin, GST_STATE_NULL);
    gst_object_unref (g_player.playbin);
    g_player.playbin = NULL;
}

int gst_player_create(char *uri)
{
    g_player.playing = FALSE;
    g_player.terminate = FALSE;
    g_player.seek_enabled = FALSE;
    g_player.seek_done = FALSE;
    g_player.duration = GST_CLOCK_TIME_NONE;
    g_player.current = -1;

    if (g_player.playbin) {
        gst_object_unref (g_player.playbin);
        g_player.playbin = NULL;
    }

    if (!uri) {
        g_printerr ("Indeed parameter: uri.\n");
        return -1;
    }

    /* Initialize GStreamer */
    gst_init (NULL, NULL);
    /* Create the elements */
    g_player.playbin = gst_element_factory_make ("playbin", "playbin");
    if (!g_player.playbin) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    }

    /* Set the URI to play */
    g_object_set (g_player.playbin, "uri", uri, NULL);

    return 0;
}

int gst_player_start()
{
    GstStateChangeReturn ret;
    pthread_t gst_player_thread;

    if (!g_player.playbin) {
        g_printerr ("Please create a playbin first.\n");
        return -1;
    }

    /* Start playing */
    ret = gst_element_set_state (g_player.playbin, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        gst_object_unref (g_player.playbin);
        return -1;
    }

    pthread_create(&gst_player_thread, NULL, listen_playbin_bus, NULL);

    return 0;
}

int gst_player_seek(int sec)
{
    int ret = -1;

    if (!g_player.playbin) {
        g_printerr ("Please create a playbin first.\n");
        return -1;
    }

    /* If seeking is enabled, we have not done it yet, and the time is right, seek */
    if (g_player.seek_enabled) {
        g_print ("\nSeek is enabled, performing seek...\n");
        gst_element_seek_simple (g_player.playbin, GST_FORMAT_TIME,
                                 (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
                                 sec * GST_SECOND);
        g_player.seek_done = TRUE;
        ret = 0;
    } else {
        g_print ("\nSeek is not enabled!\n");
    }

    return ret;
}

gint64 gst_player_get_position()
{
    return g_player.current;
}

gint64 gst_player_get_druation()
{
    if (g_player.duration == GST_CLOCK_TIME_NONE)
        return -1;

    return g_player.duration;
}

int gst_player_pause()
{
    GstStateChangeReturn ret;

    if (!g_player.playbin) {
        g_printerr ("Please create a playbin first.\n");
        return -1;
    }

    ret = gst_element_set_state (g_player.playbin, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the pause state.\n");
        return -1;
    }

    return 0;
}

int gst_player_resume()
{
    GstStateChangeReturn ret;

    if (!g_player.playbin) {
        g_printerr ("Please create a playbin first.\n");
        return -1;
    }

    ret = gst_element_set_state (g_player.playbin, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("Unable to set the pipeline to the playing state.\n");
        return -1;
    }

    return 0;
}

int gst_player_close()
{
    g_player.terminate = TRUE;
    usleep(200000); /* 200ms */

    if (g_player.playbin) {
        gst_element_set_state (g_player.playbin, GST_STATE_NULL);
        gst_object_unref (g_player.playbin);
    }

    g_player.playing = FALSE;
    g_player.terminate = FALSE;
    g_player.seek_enabled = FALSE;
    g_player.seek_done = FALSE;
    g_player.duration = GST_CLOCK_TIME_NONE;
    g_player.current = -1;
    g_player.playbin = NULL;

    return 0;
}
