#ifndef __GST_PLAYER_H__
#define __GST_PLAYER_H__

#include <gst/gst.h>

int gst_player_create(char *uri);
int gst_player_start();
int gst_player_seek(int sec);
gint64 gst_player_get_position();
gint64 gst_player_get_druation();
int gst_player_pause();
int gst_player_resume();
int gst_player_close();

#endif /* __GST_PLAYER_H__ */