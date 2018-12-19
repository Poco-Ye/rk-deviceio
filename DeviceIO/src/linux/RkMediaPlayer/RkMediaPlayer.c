#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <gst/gst.h>

#include "DeviceIo/RkMediaPlayer.h"

/* Structure to contain all our information, so we can pass it around */
typedef struct _RkMediaPlayer {
	GstElement *playbin;   /* Our one and only element */
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean stoped;      /* Are we in the STOPED state? */
	gboolean terminate;    /* Should we terminate execution? */
	gboolean seek_enabled; /* Is seeking enabled for this media? */
	gboolean is_buffering; /* Is the player in buffer? */
	gboolean is_live;      /* live stream*/
	pthread_t thread_id;   /* Thread id */
	RK_media_event_callback callback; /* Call back function */
	void *userdata;        /* Callback arg */
} RkMediaPlayer;

/* Forward definition of the message processing function */
static void handle_message (GstMessage *msg, RkMediaPlayer *c_player)
{
	GError *err;
	gchar *debug_info;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_ERROR:
			gst_message_parse_error (msg, &err, &debug_info);
			g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
			g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
			g_clear_error (&err);
			g_free (debug_info);
			/* Send MediaEvent by callback */
			if (c_player->callback)
				(*c_player->callback)(c_player->userdata, RK_MediaEvent_Error);

			break;
		case GST_MESSAGE_EOS:
			g_print ("End-Of-Stream reached.\n");
			/* Send MediaEvent by callback */
			if (c_player->callback)
				(*c_player->callback)(c_player->userdata, RK_MediaEvent_End);

			break;
		case GST_MESSAGE_DURATION:
			/* Send MediaEvent by callback */
			if (c_player->callback)
				(*c_player->callback)(c_player->userdata, RK_MediaEvent_Duration);

			break;
		case GST_MESSAGE_BUFFERING:
		{
			gint percent = 0;
			/* If the stream is live, we do not care about buffering. */
			if (c_player->is_live || c_player->stoped)
				break;
			gst_message_parse_buffering (msg, &percent);
			/* Send MediaEvent by callback */
			if (c_player->callback) {
				if ((percent < 100) && (!c_player->is_buffering)) {
					(*c_player->callback)(c_player->userdata, RK_MediaEvent_BufferStart);
					c_player->is_buffering = TRUE;
					gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
				} else if (percent >= 100) {
					(*c_player->callback)(c_player->userdata, RK_MediaEvent_BufferEnd);
					c_player->is_buffering = FALSE;
					gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
				}
			}
			break;
		}
		case GST_MESSAGE_STATE_CHANGED: {
			GstState old_state, new_state, pending_state;
			gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

			if (GST_MESSAGE_SRC (msg) == GST_OBJECT (c_player->playbin)) {
				switch (new_state) {
					case GST_STATE_PLAYING:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Play);
						break;
					case GST_STATE_PAUSED:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Pause);
						break;
					case GST_STATE_READY:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Ready);
						break;
					case GST_STATE_NULL:
						/* Send MediaEvent by callback */
						if (c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_Pause);
						break;
					default:
						break;
				}

				/* Remember whether we are in the PLAYING state or not */
				c_player->playing = (new_state == GST_STATE_PLAYING);
				if (c_player->playing) {
					/* We just moved to PLAYING. Check if seeking is possible */
					GstQuery *query;
					gint64 start, end;
					query = gst_query_new_seeking (GST_FORMAT_TIME);
					if (gst_element_query (c_player->playbin, query)) {
						gst_query_parse_seeking (query, NULL, &c_player->seek_enabled, &start, &end);
						/* Send MediaEvent by callback */
						if (c_player->seek_enabled && c_player->callback)
							(*c_player->callback)(c_player->userdata, RK_MediaEvent_SeekEnable);
					} else {
						g_printerr ("Seeking query failed.");
					}
					gst_query_unref (query);
				}
			}
			break;
		}
		case GST_MESSAGE_CLOCK_LOST: {
			/* Get a new clock */
			gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
			gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
			break;
		}
		default:
			/* We should not reach here */
			g_printerr ("Unexpected message received.\n");
			break;
	}
	gst_message_unref (msg);
}

static void *listen_playbin_bus(void *arg)
{
	GstBus *bus;
	GstMessage *msg;
	GstMessageType listen_flag;
	RkMediaPlayer *c_player = (RkMediaPlayer *)arg;

	listen_flag = (GstMessageType)(GST_MESSAGE_STATE_CHANGED |
								   GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
								   GST_MESSAGE_DURATION | GST_MESSAGE_BUFFERING);
	/* Listen to the bus */
	bus = gst_element_get_bus (c_player->playbin);
	do {
		msg = gst_bus_timed_pop_filtered (bus, 100 * GST_MSECOND, listen_flag);
		/* Parse message */
		if (msg != NULL)
			handle_message (msg, c_player);

	} while (!c_player->terminate);

	/* Free resources */
	gst_object_unref (bus);
	gst_element_set_state (c_player->playbin, GST_STATE_NULL);
	gst_object_unref (c_player->playbin);
	c_player->playbin = NULL;
}

int RK_mediaplayer_create(int *pHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)malloc(sizeof(RkMediaPlayer));

	if (c_player == NULL)
		return -ENOSPC;

	c_player->playing = FALSE;
	c_player->terminate = FALSE;
	c_player->seek_enabled = FALSE;
	c_player->is_buffering = FALSE;
	c_player->callback = NULL;
	c_player->userdata = NULL;
	c_player->thread_id = 0;

	/* Initialize GStreamer */
	if (!gst_is_initialized())
		gst_init (NULL, NULL);

	/* Create the elements */
	c_player->playbin = gst_element_factory_make ("playbin", "playbin");
	if (!c_player->playbin) {
		g_printerr ("Not all elements could be created.\n");
		return -1;
	}

	*pHandle = (int)c_player;
	return 0;
}

int RK_mediaplayer_destroy(int iHandle)
{
	void *status;
	int ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player)
		return 0;

	/* Wait for thread exit */
	c_player->terminate = TRUE;
	ret = pthread_join(c_player->thread_id, &status);
	if (ret)
		usleep(200000); /* 200ms */

	if (c_player->playbin) {
		gst_element_set_state (c_player->playbin, GST_STATE_NULL);
		gst_object_unref (c_player->playbin);
		c_player->playbin = NULL;
	}

	free(c_player);
	return 0;
}

int RK_mediaplayer_play(int iHandle, const char *uri)
{
	GstStateChangeReturn ret;
	pthread_t gst_player_thread;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	int err = 0;
	pthread_attr_t attr;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player || !c_player->playbin || !uri)
		return -EINVAL;

	/* Stop playing */
	ret = gst_element_set_state (c_player->playbin, GST_STATE_NULL);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the null state.\n");
		return -1;
	}

	/* Set the URI to play */
	g_object_set (c_player->playbin, "uri", uri, NULL);
	/* Start playing */
	ret = gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the pipeline to the playing state.\n");
		if (c_player->callback)
			(*c_player->callback)(c_player->userdata, RK_MediaEvent_URLInvalid);

		return -1;
	} else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
		c_player->is_live = TRUE;
	} else {
		c_player->is_live = FALSE;
	}
	c_player->stoped = FALSE;

	if (c_player->thread_id == 0) {
		/* Set thread joineable. */
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
		err = pthread_create(&c_player->thread_id, NULL, listen_playbin_bus, c_player);
		if (err) {
			g_printerr("Unable to create thread to listen player status, error:%s\n",
					   strerror(err));
			pthread_attr_destroy(&attr);
			return -1;
		}
		pthread_attr_destroy(&attr);
	}

	return 0;
}

int RK_mediaplayer_pause(int iHandle)
{
	GstStateChangeReturn ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	ret = gst_element_set_state (c_player->playbin, GST_STATE_PAUSED);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the player to the pause state.\n");
		return -1;
	}

	return 0;
}

int RK_mediaplayer_resume(int iHandle)
{
	GstStateChangeReturn ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	g_printerr ("#### %s, %x\n",__func__, iHandle);

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	ret = gst_element_set_state (c_player->playbin, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the player to the playing state.\n");
		return -1;
	}

	return 0;
}

int RK_mediaplayer_get_position(int iHandle)
{
	gint64 current = 0;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	if (!c_player->playing)
		return -EPERM;

	/* Query the current position of the stream */
	if (!gst_element_query_position (c_player->playbin, GST_FORMAT_TIME, &current))
		return -EAGAIN;

	return (int)(current / 1000000); // ms
}

int RK_mediaplayer_get_duration(int iHandle)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;
	gint64 duration = 0;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	if (!c_player->playing)
		return -EPERM;

	/* query the stream duration */
	if (!gst_element_query_duration (c_player->playbin, GST_FORMAT_TIME, &duration))
		return -EAGAIN;

	return (int)(duration / 1000000); // ms
}

int RK_mediaplayer_seek(int iHandle, int iMs)
{
	int ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	/* If seeking is enabled, we have not done it yet, and the time is right, seek */
	if (c_player->seek_enabled) {
		g_print ("\nSeek is enabled, performing seek...\n");
		gst_element_seek_simple (c_player->playbin, GST_FORMAT_TIME,
								 (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
								 iMs * GST_MSECOND);
		ret = 0;
	} else {
		g_print ("\nSeek is not enabled!\n");
		ret = -EAGAIN;
	}

	return ret;
}

int RK_mediaplayer_stop(int iHandle)
{
	g_printerr ("#### %s, %x\n",__func__, iHandle);
	GstStateChangeReturn ret;
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player || !c_player->playbin)
		return -EINVAL;

	c_player->stoped = TRUE;

	ret = gst_element_set_state (c_player->playbin, GST_STATE_NULL);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr ("Unable to set the player to the null state.\n");
		return -1;
	}

	if (c_player->callback)
		(*c_player->callback)(c_player->userdata, RK_MediaEvent_Stop);

	return 0;
}

int RK_mediaplayer_register_callback(int iHandle, void *userdata, RK_media_event_callback cb)
{
	RkMediaPlayer *c_player = (RkMediaPlayer *)iHandle;

	if (!c_player)
		return -EINVAL;

	c_player->callback = cb;
	c_player->userdata = userdata;

	return 0;
}
