/*****************************************************************************
**
**  Name:           app_hs.c
**
**  Description:    Bluetooth Manager application
**
**  Copyright (c) 2009-2012, Broadcom Corp., All Rights Reserved.
**  Broadcom Bluetooth Core. Proprietary and confidential.
**
**  Copyright (C) 2017 Cypress Semiconductor Corporation
**
*****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "bsa_api.h"

#include "gki.h"
#include "uipc.h"

#include "app_utils.h"
#include "app_xml_param.h"
#include "app_xml_utils.h"

#include "app_disc.h"

#include "app_hs.h"
#include "app_dm.h"
#include "app_wav.h"
#include "btm_api.h"
#include "bta_api.h"
#include "app_manager.h"

#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
#include "alsa/asoundlib.h"
#endif
#endif /* PCM_ALSA */

#ifndef BSA_SCO_ROUTE_DEFAULT
#define BSA_SCO_ROUTE_DEFAULT BSA_SCO_ROUTE_PCM
#endif

/* ui keypress definition */
enum
{
    APP_HS_KEY_OPEN = 1,
    APP_HS_KEY_CLOSE,
    APP_HS_KEY_PRESS,
    APP_HS_KEY_PLAY,
    APP_HS_KEY_RECORD,
    APP_HS_KEY_STOP_RECORDING,
    APP_HS_KEY_QUIT = 99
};

#define APP_HS_SAMPLE_RATE      8000      /* AG Default voice sample rate is 8KHz */
#define APP_HS_WBS_SAMPLE_RATE  16000     /* AG WBS voice sample rate is 16KHz */
#define APP_HS_BITS_PER_SAMPLE  16        /* AG Voice sample size is 16 bits */
#define APP_HS_CHANNEL_NB       1         /* AG Voice sample in mono */

#define APP_HS_FEATURES  ( BSA_HS_FEAT_ECNR | BSA_HS_FEAT_3WAY | BSA_HS_FEAT_CLIP | \
                           BSA_HS_FEAT_VREC | BSA_HS_FEAT_RVOL | BSA_HS_FEAT_ECS | \
                           BSA_HS_FEAT_ECC | BSA_HS_FEAT_CODEC | BSA_HS_FEAT_UNAT )

#define APP_HS_MIC_VOL  7
#define APP_HS_SPK_VOL  7


#define APP_HS_HSP_SERVICE_NAME "BSA Headset"
#define APP_HS_HFP_SERVICE_NAME "BSA Handsfree"

#define APP_HS_MAX_AUDIO_BUF 240

#define APP_HS_XML_REM_DEVICES_FILE_PATH       "data/bsa/config/bt_devices.xml"

/*
 * Types
 */

/* control block (not needed to be stored in NVRAM) */
typedef struct
{
    tAPP_HS_CONNECTION  connections[BSA_HS_MAX_NUM_CONN];
    UINT8               sco_route;
    short               audio_buf[APP_HS_MAX_AUDIO_BUF];
    BOOLEAN             open_pending;
    BD_ADDR             open_pending_bda;
    int                 rec_fd; /* recording file descriptor */
    UINT32              sample_rate;
    BOOLEAN             is_pick_up;
} tAPP_HS_CB;

/*
 * Globales Variables
 */

tAPP_HS_CB  app_hs_cb;

const char *app_hs_service_ind_name[] =
{
    "NO SERVICE",
    "SERVICE AVAILABLE"
};

const char *app_hs_call_ind_name[] =
{
    "NO CALL",
    "ACTIVE CALL"
};

const char *app_hs_callsetup_ind_name[] =
{
    "CALLSETUP DONE",
    "INCOMING CALL",
    "OUTGOING CALL",
    "ALERTING REMOTE"
};

const char *app_hs_callheld_ind_name[] =
{
    "NONE ON-HOLD",
    "ACTIVE+HOLD",
    "ALL ON-HOLD"
};

const char *app_hs_roam_ind_name[] =
{
    "HOME NETWORK",
    "ROAMING"
};

/* application callback */
static tBSA_HS_CBACK *s_pHsCallback = NULL;

#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
#if (BSA_SCO_ROUTE_DEFAULT == BSA_SCO_ROUTE_PCM)
#define APP_HS_PCM_SAMPLE_RATE  16000
#define APP_HS_PCM_CHANNEL_NB  2
#define APP_HS_6MIC_LOOPBACK_CHANNEL_NB 8
#define READ_FRAME  256
#define BUFFER_SIZE 1024
#define PERIOD_SIZE READ_FRAME

#define READ_FRAME_1024  1024
#define BUFFER_SIZE_4096 4096
#define PERIOD_SIZE_1024 READ_FRAME_1024

static const char *alsa_playback_device = "default"; /* ALSA playback device */
static const char *alsa_capture_device = "6mic_loopback"; /* ALSA capture device */
static const char *bt_playback_device = "bluetooth"; /* Bt pcm playback device */
static const char *bt_capture_device = "bluetooth"; /* Bt pcm capture device */
static BOOLEAN alsa_duplex_opened = FALSE;
static BOOLEAN bt_duplex_opened = FALSE;
static pthread_t alsa_tid = 0;
static pthread_t bt_tid = 0;

static int app_hs_open_bt_duplex(void);
static int app_hs_close_bt_duplex(void);
#else
static const char *alsa_device = "default"; /* ALSA playback device */
static snd_pcm_t *alsa_handle_playback = NULL;
static snd_pcm_t *alsa_handle_capture = NULL;
static BOOLEAN alsa_capture_opened = FALSE;
static BOOLEAN alsa_playback_opened = FALSE;
#endif

static int app_hs_open_alsa_duplex(UINT16 handle);
static int app_hs_close_alsa_duplex(void);
#endif
#endif /* PCM_ALSA */

static int app_hs_battery_report[BSA_HS_MAX_NUM_CONN];

static RK_BT_HFP_CALLBACK app_hs_send_cb = NULL;
static void app_hs_send_event(RK_BT_HFP_EVENT event, void *data) {
    if(app_hs_send_cb)
        app_hs_send_cb(event, data);
}

/*
* Local Function
*/
/*******************************************************************************
**
** Function         app_hs_register_cb
**
** Description      Register HS status notify
**
** Parameters       Notify callback
**
** Returns          void
**
*******************************************************************************/
void app_hs_register_cb(RK_BT_HFP_CALLBACK cb)
{
	app_hs_send_cb = cb;
}

/*******************************************************************************
**
** Function         app_hs_deregister_cb
**
** Description      DeRegister HS status notify
**
** Parameters
**
** Returns          void
**
*******************************************************************************/
void app_hs_deregister_cb()
{
    app_hs_send_cb = NULL;
}
/*******************************************************************************
 **
 ** Function         app_hs_num_connections
 **
 ** Description      This function number of connections
 **
 ** Returns          number of connections
 **
 *******************************************************************************/
int app_hs_num_connections()
{
    int iCount = 0;
    int index;
    for (index = 0; index < BSA_HS_MAX_NUM_CONN; index++)
    {
        if (app_hs_cb.connections[index].connection_active == TRUE)
            iCount++;
    }
    return iCount;
}

/*******************************************************************************
 **
 ** Function         app_hs_find_connection_by_bd_addr
 **
 ** Description      This function finds the connection structure by its handle
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_HS_CONNECTION *app_hs_find_connection_by_bd_addr(BD_ADDR bd_addr)
{
    int index;
    for (index = 0; index < BSA_HS_MAX_NUM_CONN; index++)
    {
        if ((bdcmp(app_hs_cb.connections[index].connected_bd_addr, bd_addr) == 0) &&
                (app_hs_cb.connections[index].connection_active == TRUE))
            return &app_hs_cb.connections[index];
    }
    return NULL;
}

/*******************************************************************************
 **
 ** Function         app_hs_find_connection_by_index
 **
 ** Description      This function finds the connection structure by its index
 **
 ** Returns          Pointer to the found structure or NULL
 **
 *******************************************************************************/
tAPP_HS_CONNECTION *app_hs_find_connection_by_index(int index)
{
    if((index < BSA_HS_MAX_NUM_CONN) &&
        (app_hs_cb.connections[index].connection_active))
        return &app_hs_cb.connections[index];

    return NULL;
}

/*******************************************************************************
**
** Function         app_hs_get_default_conn
**
** Description      Find the first active connection control block
**
** Returns          Pointer to the found connection, NULL if not found
*******************************************************************************/
tAPP_HS_CONNECTION *app_hs_get_default_conn(void)
{
    UINT16 cb_index;

    APPL_TRACE_EVENT0("app_hs_get_default_conn");

    for(cb_index = 0; cb_index < BSA_HS_MAX_NUM_CONN; cb_index++)
    {
        if(app_hs_cb.connections[cb_index].connection_active)
            return &app_hs_cb.connections[cb_index];
    }
    return NULL;
}

/*******************************************************************************
**
** Function         app_hs_get_conn_by_handle
**
** Description      Find a connection control block by its handle
**
** Returns          Pointer to the found connection, NULL if not found
*******************************************************************************/
tAPP_HS_CONNECTION *app_hs_get_conn_by_handle(UINT16 handle)
{
    UINT8 cb_index;
    APPL_TRACE_EVENT1("app_hs_get_conn_by_handle: %d", handle);

    for(cb_index = 0; cb_index < BSA_HS_MAX_NUM_CONN; cb_index++)
    {
        if(handle == app_hs_cb.connections[cb_index].handle)
            return &app_hs_cb.connections[cb_index];
    }

    return NULL;
}

/*******************************************************************************
**
** Function         app_hs_get_active_audio_conn
**
** Description      Find a active audio connection control block
**
** Returns          Pointer to the found connection, NULL if not found
*******************************************************************************/
tAPP_HS_CONNECTION *app_hs_get_active_audio_conn(void)
{
    UINT8 cb_index;

    for(cb_index = 0; cb_index < BSA_HS_MAX_NUM_CONN; cb_index++)
    {
        if(app_hs_cb.connections[cb_index].uipc_connected)
            return &app_hs_cb.connections[cb_index];
    }

    return NULL;
}

/*******************************************************************************
 **
 ** Function         app_hs_display_connections
 **
 ** Description      This function displays the connections
 **
 ** Returns          void
 **
 *******************************************************************************/
void app_hs_display_connections(void)
{
    int index;
    tAPP_HS_CONNECTION *conn = NULL;

    int num_conn = app_hs_num_connections();

    if(num_conn == 0)
    {
        APP_INFO0("    No connections");
        return;
    }

    for (index = 0; index < BSA_HS_MAX_NUM_CONN; index++)
    {
        conn = app_hs_find_connection_by_index(index);
        if(conn)
        {
            APP_INFO1("Connection index %d:-%02X:%02X:%02X:%02X:%02X:%02X",
                index,
                conn->connected_bd_addr[0], conn->connected_bd_addr[1],
                conn->connected_bd_addr[2], conn->connected_bd_addr[3],
                conn->connected_bd_addr[4], conn->connected_bd_addr[5]);
        }
    }
}


/*******************************************************************************
**
** Function         app_hs_find_indicator_id
**
** Description      parses the indicator string and finds the position of a field
**
** Returns          index in the string
*******************************************************************************/
static UINT8 app_hs_find_indicator_id(char * ind, const char * field)
{
    UINT16 string_len = strlen(ind);
    UINT8 i, id = 0;
    BOOLEAN skip = FALSE;

    for(i=0; i< string_len ; i++)
    {
        if(ind[i] == '"')
        {
            if(!skip)
            {
                id++;
                if(!strncmp(&ind[i+1], field, strlen(field)) && (ind[i+1+strlen(field)] == '"'))
                {

                    return id;
                }
                else
                {
                    /* skip the next " */
                    skip = TRUE;
                }
            }
            else
            {
                skip = FALSE;
            }
        }
    }
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_decode_indicator_string
**
** Description      process the indicator string and sets the indicator ids
**
** Returns          void
*******************************************************************************/
static void app_hs_decode_indicator_string(tAPP_HS_CONNECTION *p_conn, char * ind)
{
    p_conn->call_ind_id = app_hs_find_indicator_id(ind, "call");
    p_conn->call_setup_ind_id = app_hs_find_indicator_id(ind, "callsetup");
    if(!p_conn->call_setup_ind_id)
    {
        p_conn->call_setup_ind_id = app_hs_find_indicator_id(ind, "call_setup");
    }
    p_conn->service_ind_id = app_hs_find_indicator_id(ind, "service");
    p_conn->battery_ind_id = app_hs_find_indicator_id(ind, "battchg");
    p_conn->callheld_ind_id = app_hs_find_indicator_id(ind, "callheld");
    p_conn->signal_strength_ind_id = app_hs_find_indicator_id(ind, "signal");
    p_conn->roam_ind_id = app_hs_find_indicator_id(ind, "roam");
}

/*******************************************************************************
**
** Function         app_hs_set_initial_indicator_status
**
** Description      sets the current indicator
**
** Returns          void
*******************************************************************************/
static void app_hs_set_initial_indicator_status(tAPP_HS_CONNECTION *p_conn, char * ind)
{
    UINT8 i, pos;

    /* Clear all indicators. Not all indicators will be initialized */
    p_conn->curr_call_ind = 0;
    p_conn->curr_call_setup_ind = 0;
    p_conn->curr_service_ind = 0;
    p_conn->curr_callheld_ind = 0;
    p_conn->curr_signal_strength_ind = 0;
    p_conn->curr_roam_ind = 0;
    p_conn->curr_battery_ind = 0;

    /* skip any spaces in the front */
    while ( *ind == ' ' ) ind++;

    /* get "call" indicator*/
    pos = p_conn->call_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_call_ind = ind[i] - '0';
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    /* get "callsetup" indicator*/
    pos = p_conn->call_setup_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_call_setup_ind = ind[i] - '0';
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    /* get "service" indicator*/
    pos = p_conn->service_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_service_ind = ind[i] - '0';
            /* if there is no service play the designated tone */
            if(!p_conn->curr_service_ind)
            {
                /*
                if(HS_CFG_BEEP_NO_NETWORK)
                {
                    UTL_BeepPlay(HS_CFG_BEEP_NO_NETWORK);
                }*/
            }
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    /* get "callheld" indicator*/
    pos = p_conn->callheld_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_callheld_ind = ind[i] - '0';
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    /* get "signal" indicator*/
    pos = p_conn->signal_strength_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_signal_strength_ind = ind[i] - '0';
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    /* get "roam" indicator*/
    pos = p_conn->roam_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_roam_ind = ind[i] - '0';
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    /* get "battchg" indicator*/
    pos = p_conn->battery_ind_id -1;
    for(i=0; i< strlen(ind) ; i++)
    {
        if(!pos)
        {
            p_conn->curr_battery_ind = ind[i] - '0';
            break;
        }
        else if(ind[i] == ',')
            pos--;
    }

    if(p_conn->curr_callheld_ind != 0)
    {
        BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_3WAY_HELD);
    }
    else if(p_conn->curr_call_ind == BSA_HS_CALL_ACTIVE)
    {
        if(p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_INCOMING)
        {
            BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_WAITCALL);
        }
        else
        {
            BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_CALLACTIVE);
        }
    }
    else if(p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_INCOMING)
    {
        BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_RINGACT);
    }
    else if((p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_OUTGOING) ||
            (p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_ALERTING)
           )
    {
        BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_OUTGOINGCALL);
    }

    /* Dump indicators */
    if(p_conn->curr_service_ind < 2)
    APPL_TRACE_EVENT2("Service: %s,%d", app_hs_service_ind_name[p_conn->curr_service_ind],p_conn->curr_service_ind);

    if(p_conn->curr_call_ind < 2)
    APPL_TRACE_EVENT2("Call: %s,%d", app_hs_call_ind_name[p_conn->curr_call_ind],p_conn->curr_call_ind);

    if(p_conn->curr_call_setup_ind < 4)
    APPL_TRACE_EVENT2("Callsetup: Ind %s,%d", app_hs_callsetup_ind_name[p_conn->curr_call_setup_ind],p_conn->curr_call_setup_ind);

    if(p_conn->curr_callheld_ind < 3)
    APPL_TRACE_EVENT2("Hold: %s,%d", app_hs_callheld_ind_name[p_conn->curr_callheld_ind],p_conn->curr_callheld_ind);

    if(p_conn->curr_roam_ind < 2)
    APPL_TRACE_EVENT2("Roam: %s,%d", app_hs_roam_ind_name[p_conn->curr_roam_ind],p_conn->curr_roam_ind);
}

/*******************************************************************************
**
** Function         app_hs_set_indicator_status
**
** Description      sets the current indicator
**
** Returns          void
*******************************************************************************/
static void app_hs_set_indicator_status(tAPP_HS_CONNECTION *p_conn, char * ind)
{
    UINT8 id;

    /* skip any spaces in the front */
    while ( *ind == ' ' ) ind++;

    /* get ID of the indicator*/
    id = *ind - '0';
    ind += 2;

    if(id == p_conn->call_ind_id)
    {
        p_conn->curr_call_ind = *ind - '0';

        APPL_TRACE_EVENT2("Call: %s,%d", app_hs_call_ind_name[p_conn->curr_call_ind],p_conn->curr_call_ind);
    }
    else if(id == p_conn->call_setup_ind_id)
    {
        p_conn->curr_call_setup_ind = *ind - '0';

        if(p_conn->curr_call_ind == BSA_HS_CALL_ACTIVE)
        {
            if(p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_INCOMING)
            {
                BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_WAITCALL);
            }
            else
            {
                BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_CALLACTIVE);
            }
        }
        else if(p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_INCOMING)
        {
            BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_RINGACT);
        }
        else if((p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_OUTGOING) ||
            (p_conn->curr_call_setup_ind == BSA_HS_CALLSETUP_ALERTING)
           )
        {
            BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_OUTGOINGCALL);
        }

        APPL_TRACE_EVENT2("Callsetup: Ind %s,%d", app_hs_callsetup_ind_name[p_conn->curr_call_setup_ind],p_conn->curr_call_setup_ind);
    }
    else if(id == p_conn->service_ind_id)
    {
        p_conn->curr_service_ind = *ind - '0';

        APPL_TRACE_EVENT2("Service: %s,%d", app_hs_service_ind_name[p_conn->curr_service_ind],p_conn->curr_service_ind);
    }
    else if(id == p_conn->battery_ind_id)
    {
        p_conn->curr_battery_ind = *ind - '0';
    }
    else if(id == p_conn->signal_strength_ind_id)
    {
        p_conn->curr_signal_strength_ind = *ind - '0';
    }
    else if(id == p_conn->callheld_ind_id)
    {
        p_conn->curr_callheld_ind = *ind - '0';
        if(p_conn->curr_callheld_ind != 0)
        {
            BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_3WAY_HELD);
        }

        APPL_TRACE_EVENT2("Hold: %s,%d", app_hs_callheld_ind_name[p_conn->curr_callheld_ind],p_conn->curr_callheld_ind);
    }
    else if(id == p_conn->roam_ind_id)
    {
        p_conn->curr_roam_ind = *ind - '0';

        APPL_TRACE_EVENT2("Roam: %s,%d", app_hs_roam_ind_name[p_conn->curr_roam_ind],p_conn->curr_roam_ind);
    }
}

/*******************************************************************************
 **
 ** Function         app_hs_write_to_file
 **
 ** Description      write SCO IN data to file
 **
 ** Parameters
 **
 ** Returns          number of byte written.
 **
 *******************************************************************************/
static int app_hs_write_to_file(UINT8 *p_buf, int size)
{
#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
    APP_ERROR0("Cannot write to file when PCM_ALSA_DISABLE_HS is not defined");
    return -1;
#endif
#endif /* PCM_ALSA */

    int ret;
    if(app_hs_cb.rec_fd <= 0)
    {
        APP_DEBUG0("no file to write...\n");
        return 0;
    }

    ret = write(app_hs_cb.rec_fd, p_buf, size);

    if(ret != size)
    {
        APP_ERROR1("write failed with code %d, fd %d", ret, app_hs_cb.rec_fd);
    }


    return ret;
}

/*******************************************************************************
 **
 ** Function         app_hs_sco_uipc_cback
 **
 ** Description     uipc audio call back function.
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
static void app_hs_sco_uipc_cback(BT_HDR *p_buf)
{
    UINT8 *pp = (UINT8 *)(p_buf + 1);
    UINT8 pkt_len;

#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
#if (BSA_SCO_ROUTE_DEFAULT == BSA_SCO_ROUTE_HCI)
    snd_pcm_sframes_t alsa_frames;
    snd_pcm_sframes_t alsa_frames_expected;
#endif
#endif
#endif /* PCM_ALSA */

    if (p_buf == NULL)
    {
        return;
    }

    pkt_len = p_buf->len;

    if (app_hs_cb.rec_fd>0)
    {
        app_hs_write_to_file(pp, pkt_len);

    }
#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
#if (BSA_SCO_ROUTE_DEFAULT == BSA_SCO_ROUTE_HCI)
    /* Compute number of PCM samples (contained in pkt_len->len bytes) */
    /* Divide by the number of channel */
    alsa_frames_expected = pkt_len / APP_HS_CHANNEL_NB;
    alsa_frames_expected /= 2; /* 16 bits samples */

    if (alsa_playback_opened != FALSE)
    {
        /*
        * Send PCM samples to ALSA/asound driver (local sound card)
        */
        alsa_frames = snd_pcm_writei(alsa_handle_playback, pp, alsa_frames_expected);
        if (alsa_frames < 0)
        {
            APP_DEBUG1("snd_pcm_recover %d", (int)alsa_frames);
            alsa_frames = snd_pcm_recover(alsa_handle_playback, alsa_frames, 0);
        }
        if (alsa_frames < 0)
        {
            APP_ERROR1("snd_pcm_writei failed: %s", snd_strerror(alsa_frames));
        }
        if (alsa_frames > 0 && alsa_frames < alsa_frames_expected)
        {
            APP_ERROR1("Short write (expected %d, wrote %d)",
                (int)alsa_frames_expected, (int)alsa_frames);
        }
    }
    else
    {
        APP_DEBUG0("alsa_playback_opened NOT");
    }

    if (alsa_capture_opened != FALSE)
    {
        /*
        * Read PCM samples from ALSA/asound driver (local sound card)
        */
        alsa_frames = snd_pcm_readi(alsa_handle_capture, app_hs_cb.audio_buf, alsa_frames_expected);
        if (alsa_frames < 0)
        {
            APP_ERROR1("snd_pcm_readi returns: %d", (int)alsa_frames);
        }
        else if ((alsa_frames > 0) &&
            (alsa_frames < alsa_frames_expected))
        {
            APP_ERROR1("Short read (expected %i, wrote %i)", (int)alsa_frames_expected, (int)alsa_frames);
        }
        /* Send them to UIPC (to Headet) */

        /* for now we just handle one instance */
        /* for multiple instance user should be prompted */
        tAPP_HS_CONNECTION * p_conn = app_hs_get_active_audio_conn();

        if (TRUE != UIPC_Send(p_conn->uipc_channel, 0, (UINT8 *) app_hs_cb.audio_buf, alsa_frames * 2))
        {
            APP_ERROR0("UIPC_Send failed");
        }
    }
    else
    {
        APP_DEBUG0("alsa_capture NOT opened");
    }
#endif
#endif
#endif /* PCM_ALSA */
    GKI_freebuf(p_buf);
}


/*******************************************************************************
 **
 ** Function         app_hs_read_xml_remote_devices
 **
 ** Description      This function is used to read the XML bluetooth remote device file
 **
 ** Parameters
 **
 ** Returns          void
 **
 *******************************************************************************/
int app_hs_read_xml_remote_devices(void)
{
    int status;
    int index;

    for (index = 0; index < APP_NUM_ELEMENTS(app_xml_remote_devices_db); index++)
    {
        app_xml_remote_devices_db[index].in_use = FALSE;
    }

    status = app_xml_read_db(APP_HS_XML_REM_DEVICES_FILE_PATH, app_xml_remote_devices_db,
            APP_NUM_ELEMENTS(app_xml_remote_devices_db));

    if (status < 0)
    {
        APP_ERROR1("app_xml_read_db failed (%d)", status);
        return -1;
    }
    return 0;
}

/* public function */


/*******************************************************************************
**
** Function         app_hs_get_unused_connections
**
** Description      Returns any unused connection control block handle
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_get_unused_connections(void)
{
    UINT16 cb_index;

    APPL_TRACE_EVENT0("app_hs_get_unused_connections");

    for(cb_index = 0; cb_index < BSA_HS_MAX_NUM_CONN; cb_index++)
    {
        if (!app_hs_cb.connections[cb_index].connection_active)
        {
            APPL_TRACE_DEBUG2("app_hs_get_unused_connections index : %d, handle= %d", cb_index, app_hs_cb.connections[cb_index].handle);
            return app_hs_cb.connections[cb_index].handle;
        }
    }

    return APP_HS_INVALID_HANDLE;
}

/*******************************************************************************
**
** Function         app_hs_open
**
** Description      Establishes mono headset connections
**
** Parameter        BD address to connect to. If its NULL, the app will prompt user for device.
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_open(BD_ADDR *bd_addr_in /*= NULL*/)
{
    tBSA_STATUS status = 0;
    BD_ADDR bd_addr;
    int device_index;

    tBSA_HS_OPEN param;

    APP_DEBUG0("Entering");

    if(bd_addr_in == NULL)
    {
        printf("Bluetooth AG menu:\n");
        printf("    0 Device from XML database (already paired)\n");
        printf("    1 Device found in last discovery\n");
        device_index = app_get_choice("Select source");
        /* Devices from XML databased */
        if (device_index == 0)
        {
            /* Read the Remote device xml file to have a fresh view */
            app_hs_read_xml_remote_devices();

            app_xml_display_devices(app_xml_remote_devices_db, APP_NUM_ELEMENTS(app_xml_remote_devices_db));
            device_index = app_get_choice("Select device");
            if ((device_index >= 0) &&
                (device_index < APP_NUM_ELEMENTS(app_xml_remote_devices_db)) &&
                (app_xml_remote_devices_db[device_index].in_use != FALSE))
            {
                bdcpy(bd_addr, app_xml_remote_devices_db[device_index].bd_addr);
            }
            else
            {
                printf("Bad Device Index:%d\n", device_index);
                return -1;
            }
        }
        /* Devices from Discovery */
        else
        {
            app_disc_display_devices();
            printf("Enter device number\n");
            device_index = app_get_choice("Select device");
            if ((device_index >= 0) &&
                (device_index < APP_DISC_NB_DEVICES) &&
                (app_discovery_cb.devs[device_index].in_use != FALSE))
            {
                bdcpy(bd_addr, app_discovery_cb.devs[device_index].device.bd_addr);
            }
            else
            {
                printf("Bad Device Index:%d\n", device_index);
                return -1;
            }
        }
    }
    else
    {
        bdcpy(bd_addr, *bd_addr_in);
    }

    BSA_HsOpenInit(&param);

    bdcpy(param.bd_addr, bd_addr);

    status = BSA_HsOpen(&param);
    app_hs_cb.open_pending = TRUE;
    bdcpy(app_hs_cb.open_pending_bda, bd_addr);

    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("BSA_HsOpen failed (%d)", status);
        app_hs_cb.open_pending = FALSE;
        memset(app_hs_cb.open_pending_bda, 0, sizeof(BD_ADDR));
        return -1;
    }

    /* this is an active wait for demo purpose */
    APP_DEBUG0("waiting for hs connection to open");
#if 0
    while (app_hs_cb.open_pending == TRUE);
#else
    GKI_delay(3000);
    if(app_hs_cb.open_pending == TRUE) {
        APP_ERROR0("after 2 seconds, app_hs_cback not return BSA_HS_OPEN_EVT");
        app_hs_cb.open_pending = FALSE;
        memset(app_hs_cb.open_pending_bda, 0, sizeof(BD_ADDR));
        return -1;
    }
#endif

    return status;
}

/*******************************************************************************
**
** Function         app_hs_audio_open
**
** Description      Open the SCO connection alone
**
** Parameter        None
**
** Returns          0 if success -1 if failure
*******************************************************************************/

int app_hs_audio_open(UINT16 handle)
{
    printf("app_hs_audio_open\n");

    tBSA_STATUS status;
    tBSA_HS_AUDIO_OPEN audio_open;
    tAPP_HS_CONNECTION * p_conn;

    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    if(app_hs_cb.sco_route == BSA_SCO_ROUTE_HCI &&
        p_conn->uipc_channel == UIPC_CH_ID_BAD)
    {
        APP_ERROR0("Bad UIPC channel in app_hs_audio_open");
        return -1;
    }

    BSA_HsAudioOpenInit(&audio_open);
    audio_open.sco_route = app_hs_cb.sco_route;
    audio_open.hndl = p_conn->handle;

    status = BSA_HsAudioOpen(&audio_open);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("failed with status : %d", status);
        return -1;
    }
    return status;
}

/*******************************************************************************
**
** Function         app_hs_audio_close
**
** Description      Close the SCO connection alone
**
** Parameter        None
**
** Returns          0 if success -1 if failure
*******************************************************************************/

int app_hs_audio_close(UINT16 handle)
{
    printf("app_hs_audio_close\n");
    tBSA_STATUS status;
    tBSA_HS_AUDIO_CLOSE audio_close;
    tAPP_HS_CONNECTION * p_conn;

    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsAudioCloseInit(&audio_close);
    audio_close.hndl = p_conn->handle;

    status = BSA_HsAudioClose(&audio_close);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("failed with status : %d", status);
        return -1;
    }
    return status;
}

/*******************************************************************************
**
** Function         app_hs_close
**
** Description      release mono headset connections
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_close(UINT16 handle)
{
    tBSA_HS_CLOSE param;
    tBSA_STATUS status;
    tAPP_HS_CONNECTION * p_conn;

    /* Prepare parameters */
    BSA_HsCloseInit(&param);
    /* todo : promt user to check what to close here */

    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    param.hndl = p_conn->handle;

    status = BSA_HsClose(&param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("failed with status : %d", status);
        return -1;
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_hs_close_all
**
** Description      release all mono headset connections
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_close_all(void)
{
    tBSA_HS_CLOSE param;
    tBSA_STATUS status;
    UINT16 cb_index;

    APPL_TRACE_EVENT0("app_hs_close_all");

    for(cb_index = 0; cb_index < BSA_HS_MAX_NUM_CONN; cb_index++) {
        if(app_hs_cb.connections[cb_index].connection_active) {
            /* Prepare parameters */
            BSA_HsCloseInit(&param);
            param.hndl = app_hs_cb.connections[cb_index].handle;

            status = BSA_HsClose(&param);
            if (status != BSA_SUCCESS) {
                APP_ERROR1("failed with status : %d", status);
                return -1;
            }
        }
    }

    return 0;
}

/*******************************************************************************
**
** Function         app_hs_cancel
**
** Description      cancel connections
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_cancel(void)
{
    tBSA_HS_CANCEL param;
    tBSA_STATUS status;

    /* Prepare parameters */
    BSA_HsCancelInit(&param);
    bdcpy(param.bd_addr, app_hs_cb.open_pending_bda);

    status = BSA_HsCancel(&param);
    if (status != BSA_SUCCESS)
    {
        APP_ERROR1("failed with status : %d", status);
        return -1;
    }
    memset(app_hs_cb.open_pending_bda, 0, sizeof(BD_ADDR));
    return 0;
}

/*******************************************************************************
**
** Function         app_is_open_pending
**
**
** Returns          TRUE if open if pending
*******************************************************************************/
BOOLEAN app_hs_is_open_pending(BD_ADDR bda_open_pending)
{
    bdcpy(bda_open_pending, app_hs_cb.open_pending_bda);
    return app_hs_cb.open_pending;
}

/*******************************************************************************
**
** Function         app_hs_answer_call
**
** Description      example of function to answer the call
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_answer_call(UINT16 handle)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn;

    printf("app_hs_answer_call\n");

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_A_CMD;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_hangup
**
** Description      example of function to hang up
**
** Returns          0 if success -1 if failure
*******************************************************************************/
int app_hs_hangup(UINT16 handle)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn;

    printf("app_hs_hangup\n");

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_CHUP_CMD;
    BSA_HsCommand(&cmd_param);

    return 0;
}

/*******************************************************************************
**
** Function         app_hs_stop
**
** Description      This function is used to stop hs services and close all
**                  UIPC channel.
**
** Parameters       void
**
** Returns          void
**
*******************************************************************************/
void app_hs_stop(void)
{
    tBSA_HS_DISABLE      disable_param;
    tBSA_HS_DEREGISTER   deregister_param;
    tAPP_HS_CONNECTION * p_conn;
    UINT8 index;

    for(index=0; index<BSA_HS_MAX_NUM_CONN; index++)
    {
        BSA_HsDeregisterInit(&deregister_param);

        p_conn = &(app_hs_cb.connections[index]);
        deregister_param.hndl = p_conn->handle;
        APP_DEBUG1("handle %d", deregister_param.hndl);
        BSA_HsDeregister(&deregister_param);

        if(p_conn->uipc_channel != UIPC_CH_ID_BAD)
        {
            if(p_conn->uipc_connected)
            {
                APPL_TRACE_DEBUG0("Closing UIPC Channel");
                UIPC_Close(p_conn->uipc_channel);
                p_conn->uipc_connected = FALSE;
            }
            p_conn->uipc_channel = UIPC_CH_ID_BAD;
        }
    }

    BSA_HsDisableInit(&disable_param);
    BSA_HsDisable(&disable_param);

    /* for now we just handle one instance */
    /* for multiple instance user should be prompted */
}

/*******************************************************************************
**
** Function         app_hs_close_rec_file
**
** Description      Close recording file
**
** Parameters       void
**
** Returns          void
**
*******************************************************************************/
void app_hs_close_rec_file(void)
{
    int fd;
    tAPP_WAV_FILE_FORMAT format;

    if(app_hs_cb.rec_fd <= 0)
        return;

    format.bits_per_sample = APP_HS_BITS_PER_SAMPLE;
    format.nb_channels = APP_HS_CHANNEL_NB;
    format.sample_rate = app_hs_cb.sample_rate;

    fd = app_hs_cb.rec_fd;
    app_hs_cb.rec_fd = 0;
    app_wav_close_file(fd, &format);
}

/*******************************************************************************
**
** Function         app_hs_open_rec_file
**
** Description     Open recording file
**
** Parameters      filename to open
**
** Returns          void
**
*******************************************************************************/
void app_hs_open_rec_file(char * filename, UINT32 sample_rate)
{
    app_hs_cb.sample_rate = sample_rate;
    app_hs_cb.rec_fd = app_wav_create_file(filename, 0);
}

/*******************************************************************************
**
** Function         app_hs_play_file
**
** Description      Play SCO data from file to SCO OUT channel
**
** Parameters       char * filename
**
** Returns          0 if success -1 if failure
**
*******************************************************************************/
int app_hs_play_file(UINT16 handle, char * filename)
{
    tAPP_HS_CONNECTION * p_conn;
    tAPP_WAV_FILE_FORMAT  wav_format;
    int nb_bytes = 0;
    int fd = 0;

    printf("app_hs_play_file\n");

    fd = app_wav_open_file(filename, &wav_format);

    if(fd < 0)
    {
        printf("Error could not open wav input file\n");
        printf("Use the Record audio file function to create an audio file called %s and then try again\n",APP_HS_SCO_OUT_SOUND_FILE);
        return -1;
    }

    do
    {
        nb_bytes = read(fd, app_hs_cb.audio_buf, sizeof(app_hs_cb.audio_buf)); /* read audio sample */

        if(nb_bytes < 0)
        {
            close(fd);
            return -1;
        }

        /* for multiple instance user should be prompted */
        p_conn = app_hs_get_conn_by_handle(handle);
        if(p_conn == NULL)
        {
            APP_ERROR0("no connection available");
            close(fd);
            return -1;
        }

        if (TRUE != UIPC_Send(p_conn->uipc_channel, 0,
                (UINT8 *) app_hs_cb.audio_buf,
                nb_bytes))
        {
            printf("error in UIPC send could not send data \n");
        }

    } while (nb_bytes != 0);

    close(fd);

    return 0;
}

/*******************************************************************************
**
** Function         app_hs_cback
**
** Description      Example of HS callback function
**
** Parameters       event code and data
**
** Returns          void
**
*******************************************************************************/
void app_hs_cback(tBSA_HS_EVT event, tBSA_HS_MSG *p_data)
{
    char buf[100];
    UINT16 handle = APP_HS_INVALID_HANDLE;
    tAPP_HS_CONNECTION *p_conn;

    if (!p_data)
    {
        APP_DEBUG1("p_data=NULL for event:%d\n", event);
        return;
    }

    /* retrieve the handle of the connection for which this event */
    handle = p_data->hdr.handle;
    APPL_TRACE_DEBUG2("app_hs_cback event:%d for handle: %d", event, handle);

    /* retrieve the connection for this handle */
    p_conn = app_hs_get_conn_by_handle(handle);
    if (!p_conn && event != BSA_HS_REGISTER_EVT)
    {
        APP_DEBUG1("handle %d not supported\n", handle);
        return;
    }

    switch (event)
    {
    case BSA_HS_REGISTER_EVT:
        APP_DEBUG1("p_data->reg.hndl: %d, p_data->reg.status: %d", p_data->reg.hndl, p_data->reg.status);
        if(p_data->reg.hndl == 0 || p_data->reg.hndl > BSA_MAX_HS_CONNECTIONS || p_data->reg.status != BSA_SUCCESS)
        {
            APP_DEBUG1("ERROR : BSA_HS_REGISTER_EVT invalid handle=%d, status=%d\n", p_data->reg.hndl, p_data->reg.status);
            break;
        }

        p_conn = &(app_hs_cb.connections[p_data->reg.hndl - 1]);
        p_conn->handle = p_data->reg.hndl;
        p_conn->uipc_channel = p_data->reg.uipc_channel;
        break;

    case BSA_HS_CONN_EVT:       /* Service level connection */
        printf("BSA_HS_CONN_EVT:\n");
        app_hs_cb.open_pending = FALSE;
        memset(app_hs_cb.open_pending_bda, 0, sizeof(BD_ADDR));

        printf("    - Remote bdaddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
                p_data->conn.bd_addr[0], p_data->conn.bd_addr[1],
                p_data->conn.bd_addr[2], p_data->conn.bd_addr[3],
                p_data->conn.bd_addr[4], p_data->conn.bd_addr[5]);
        printf("    - Service: ");
        switch (p_data->conn.service)
        {
        case BSA_HSP_HS_SERVICE_ID:
            printf("Headset\n");
            break;
        case BSA_HFP_HS_SERVICE_ID:
            printf("Handsfree\n");
            break;
        default:
            printf("Not supported 0x%08x\n", p_data->conn.service);
            return;
        }

        /* check if this conneciton is already opened */
        if (p_conn->connection_active)
        {
            printf("BSA_HS_CONN_EVT: connection already opened for handle %d\n", handle);
            break;
        }
        bdcpy(p_conn->connected_bd_addr, p_data->conn.bd_addr);
        p_conn->handle = p_data->conn.handle;
        p_conn->connection_active = TRUE;
        p_conn->connected_hs_service_id = p_data->conn.service;
        p_conn->peer_feature = p_data->conn.peer_features;
        p_conn->status = BSA_HS_ST_CONNECT;
        app_hs_send_event(RK_BT_HFP_CONNECT_EVT, NULL);
        break;


    case BSA_HS_CLOSE_EVT:      /* Connection Closed (for info)*/
        /* Close event, reason BSA_HS_CLOSE_CLOSED or BSA_HS_CLOSE_CONN_LOSS */
        APP_DEBUG1("BSA_HS_CLOSE_EVT, handle %d, reason %d", handle, p_data->hdr.status);
        app_hs_cb.open_pending = FALSE;
        p_conn->sample_rate = APP_HS_SAMPLE_RATE;
        memset(app_hs_cb.open_pending_bda, 0, sizeof(BD_ADDR));

        if (!p_conn->connection_active)
        {
            printf("BSA_HS_CLOSE_EVT: connection not opened for handle %d\n", handle);
            break;
        }
        p_conn->connection_active = FALSE;
        p_conn->indicator_string_received = FALSE;

        BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_CONNECTABLE);
        app_hs_send_event(RK_BT_HFP_DISCONNECT_EVT, NULL);
        break;

    case BSA_HS_AUDIO_OPEN_EVT:     /* Audio Open Event */
        fprintf(stdout,"BSA_HS_AUDIO_OPEN_EVT\n");

        if(app_hs_get_active_audio_conn() != NULL)
        {
            p_conn->call_state = BSA_HS_CALL_CONN;
            BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_SCOOPEN);
            app_hs_audio_close(p_conn->handle);
            break;
        }

        if(app_hs_cb.sco_route == BSA_SCO_ROUTE_HCI &&
           p_conn->uipc_channel != UIPC_CH_ID_BAD &&
           !p_conn->uipc_connected)
        {
            /* Open UIPC channel for TX channel ID */
            if(UIPC_Open(p_conn->uipc_channel, app_hs_sco_uipc_cback)!= TRUE)
            {
                APP_ERROR1("app_hs_register failed to open UIPC channel(%d)",
                        p_conn->uipc_channel);
                break;
            }
            p_conn->uipc_connected = TRUE;
            UIPC_Ioctl(p_conn->uipc_channel,UIPC_REG_CBACK,app_hs_sco_uipc_cback);
        }

#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
        if(BSA_HS_GETSTATUS(p_conn, BSA_HS_ST_SCOOPEN))
           app_hs_close_alsa_duplex();
        app_hs_open_alsa_duplex(handle);

#if (BSA_SCO_ROUTE_DEFAULT == BSA_SCO_ROUTE_PCM)
        if(BSA_HS_GETSTATUS(p_conn, BSA_HS_ST_SCOOPEN))
            app_hs_close_bt_duplex();
        app_hs_open_bt_duplex();
#endif

#endif
#endif /* PCM_ALSA */

        p_conn->call_state = BSA_HS_CALL_CONN;
        BSA_HS_SETSTATUS(p_conn, BSA_HS_ST_SCOOPEN);
        app_hs_send_event(RK_BT_HFP_AUDIO_OPEN_EVT, NULL);
        break;

    case BSA_HS_AUDIO_CLOSE_EVT:         /* Audio Close event */
        fprintf(stdout,"BSA_HS_AUDIO_CLOSE_EVT\n");

#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
        app_hs_close_alsa_duplex();
#if (BSA_SCO_ROUTE_DEFAULT == BSA_SCO_ROUTE_PCM)
        app_hs_close_bt_duplex();
#endif
#endif
#endif /* PCM_ALSA */

        p_conn->call_state = BSA_HS_CALL_NONE;
        BSA_HS_RESETSTATUS(p_conn, BSA_HS_ST_SCOOPEN);

        if(p_conn->uipc_channel != UIPC_CH_ID_BAD &&
           p_conn->uipc_connected)
        {
            APPL_TRACE_DEBUG0("Closing UIPC Channel");
            //Release the select to exit the read thread*/
            UIPC_Ioctl(p_conn->uipc_channel, UIPC_REG_CBACK, NULL);
            usleep(10000);
            UIPC_Close(p_conn->uipc_channel);
            p_conn->uipc_connected = FALSE;

        }

        app_hs_cb.is_pick_up = FALSE;
        app_hs_send_event(RK_BT_HFP_AUDIO_CLOSE_EVT, NULL);
        break;

    case BSA_HS_CIEV_EVT:                /* CIEV event */
        printf("BSA_HS_CIEV_EVT\n");
        strncpy(buf, p_data->val.str, 4);
        buf[5] ='\0';
        printf("handle %d, Call Ind Status %s\n", handle, buf);
        app_hs_set_indicator_status(p_conn, buf);
        break;

    case BSA_HS_CIND_EVT:                /* CIND event */
        printf("BSA_HS_CIND_EVT\n");
        printf("handle %d, Call Indicator %s\n",handle, p_data->val.str);

        /* check if indicator configuration was received */
        if(p_conn->indicator_string_received)
        {
            app_hs_set_initial_indicator_status(p_conn, p_data->val.str);
        }
        else
        {
            p_conn->indicator_string_received = TRUE;
            app_hs_decode_indicator_string(p_conn, p_data->val.str);
        }
        break;

    case BSA_HS_RING_EVT:
        fprintf(stdout, "BSA_HS_RING_EVT : handle %d\n", handle);
        app_hs_send_event(RK_BT_HFP_RING_EVT, NULL);
        break;

    case BSA_HS_CLIP_EVT:
        fprintf(stdout, "BSA_HS_CLIP_EVT : handle %d\n", handle);
        break;

    case BSA_HS_BSIR_EVT:
        fprintf(stdout, "BSA_HS_BSIR_EVT : handle %d\n", handle);
        break;

    case BSA_HS_BVRA_EVT:
        fprintf(stdout, "BSA_HS_BVRA_EVT : handle %d\n", handle);
        break;

    case BSA_HS_CCWA_EVT:
        fprintf(stdout, "Call waiting : BSA_HS_CCWA_EVT:%s, handle %d\n", p_data->val.str, handle);
        break;

    case BSA_HS_CHLD_EVT:
        fprintf(stdout, "BSA_HS_CHLD_EVT : handle %d\n", handle);
        break;

    case BSA_HS_VGM_EVT:
        fprintf(stdout, "BSA_HS_VGM_EVT : handle %d\n", handle);
        break;

    case BSA_HS_VGS_EVT:
        APP_DEBUG1("BSA_HS_VGS_EVT Speaker volume: %d", p_data->val.num);
        app_hs_send_event(RK_BT_HFP_VOLUME_EVT, &p_data->val.num);
        break;

    case BSA_HS_BINP_EVT:
        fprintf(stdout, "BSA_HS_BINP_EVT : handle %d\n", handle);
        break;

    case BSA_HS_BTRH_EVT:
        fprintf(stdout, "BSA_HS_BTRH_EVT : handle %d\n", handle);
        break;

    case BSA_HS_CNUM_EVT:
        fprintf(stdout, "BSA_HS_CNUM_EVT:%s, handle %d\n",p_data->val.str, handle);
        break;

    case BSA_HS_COPS_EVT:
        fprintf(stdout, "BSA_HS_COPS_EVT:%s, handle %d\n",p_data->val.str, handle);
        break;

    case BSA_HS_CMEE_EVT:
        fprintf(stdout, "BSA_HS_CMEE_EVT:%s, handle %d\n", p_data->val.str, handle);
        break;

    case BSA_HS_CLCC_EVT:
        fprintf(stdout, "BSA_HS_CLCC_EVT:%s, handle %d\n", p_data->val.str, handle);
        break;

    case BSA_HS_UNAT_EVT:
        fprintf(stdout, "BSA_HS_UNAT_EVT : handle %d\n", handle);
        break;

    case BSA_HS_OK_EVT:
        fprintf(stdout, "BSA_HS_OK_EVT: command value %d, %s, handle %d\n", p_data->val.num, p_data->val.str, handle);
        switch(p_data->val.num) {
            case BSA_HS_A_CMD:
                APP_DEBUG0("Call has been picked up");
                app_hs_send_event(RK_BT_HFP_PICKUP_EVT, NULL);
                break;

            case BSA_HS_CHUP_CMD:
                APP_DEBUG0("Call has been hanged up");
                app_hs_send_event(RK_BT_HFP_HANGUP_EVT, NULL);
                break;
        }
        break;

    case BSA_HS_ERROR_EVT:
        fprintf(stdout, "BSA_HS_ERROR_EVT : handle %d\n", handle);
        break;

    case BSA_HS_BCS_EVT:
        fprintf(stdout, "BSA_HS_BCS_EVT: codec %d (%s), handle %d\n",p_data->val.num,
            (p_data->val.num == BSA_SCO_CODEC_MSBC) ? "mSBC":"CVSD", handle);
        if(p_data->val.num == BSA_SCO_CODEC_MSBC)
            p_conn->sample_rate = APP_HS_WBS_SAMPLE_RATE;
        else
            p_conn->sample_rate = APP_HS_SAMPLE_RATE;
        break;

    case BSA_HS_OPEN_EVT:
        fprintf(stdout, "BSA_HS_OPEN_EVT : handle %d\n", handle);
        app_hs_cb.open_pending = FALSE;
        memset(app_hs_cb.open_pending_bda, 0, sizeof(BD_ADDR));
        break;

    default:
        printf("app_hs_cback unknown event:%d, handle %d\n", event, handle);
        break;
    }
    fflush(stdout);

    /* forward callback to the registered application */
    if(s_pHsCallback)
        s_pHsCallback(event, p_data);
}

/*******************************************************************************
**
** Function         app_hs_start
**
** Description      Example of function to start the Headset application
**
** Parameters		Callback for event notification (can be NULL, if NULL default will be used)
**
** Returns          0 if ok -1 in case of error
**
*******************************************************************************/
int app_hs_start(tBSA_HS_CBACK cb)
{
    tBSA_STATUS status;

    status = app_hs_enable();
    if (status != 0)
    {
        return status;
    }

    status = app_hs_register();

    s_pHsCallback = cb;

    return status;
}

/*******************************************************************************
**
** Function         app_hs_enable
**
** Description      Example of function to start enable Hs service
**
** Parameters       void
**
** Returns          0 if ok -1 in case of error
**
*******************************************************************************/
int app_hs_enable(void)
{
    int                status = 0;
    tBSA_HS_ENABLE     enable_param;

    /* prepare parameters */
    BSA_HsEnableInit(&enable_param);
    enable_param.p_cback = app_hs_cback;

    status = BSA_HsEnable(&enable_param);
    if (status != BSA_SUCCESS)
    {
        APP_DEBUG1("BSA_HsEnable failes with status %d", status);
        return -1;
    }
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_register
**
** Description      Example of function to start register one Hs instance
**
** Parameters       void
**
** Returns          0 if ok -1 in case of error
**
*******************************************************************************/
int app_hs_register(void)
{
    int index, status = 0;
    tBSA_HS_REGISTER   param;

    APP_DEBUG0("start Register");

    app_hs_cb.sco_route = BSA_SCO_ROUTE_DEFAULT;

    /* prepare parameters */
    BSA_HsRegisterInit(&param);
    param.services = BSA_HSP_HS_SERVICE_MASK | BSA_HFP_HS_SERVICE_MASK;
    param.sec_mask = BSA_SEC_NONE;
    /* Use BSA_HS_FEAT_CODEC (for WBS) for SCO over PCM only, not for SCO over HCI*/
    param.features = APP_HS_FEATURES;
#if 0
    if(app_hs_cb.sco_route == BSA_SCO_ROUTE_HCI)
        param.features &= ~BSA_HS_FEAT_CODEC;
#endif
    param.settings.ecnr_enabled = (param.features & BSA_HS_FEAT_ECNR) ? TRUE : FALSE;
    param.settings.mic_vol = APP_HS_MIC_VOL;
    param.settings.spk_vol = APP_HS_SPK_VOL;
    strncpy(param.service_name[0], APP_HS_HSP_SERVICE_NAME, BSA_HS_SERVICE_NAME_LEN_MAX);
    strncpy(param.service_name[1], APP_HS_HFP_SERVICE_NAME, BSA_HS_SERVICE_NAME_LEN_MAX);

    /* SCO routing options:  BSA_SCO_ROUTE_HCI or BSA_SCO_ROUTE_PCM */
    param.sco_route = app_hs_cb.sco_route;

    for(index=0; index<BSA_HS_MAX_NUM_CONN; index++)
    {
        status = BSA_HsRegister(&param);
        if (status != BSA_SUCCESS)
        {
            APP_ERROR1("Unable to register HS with status %d", status);
            return -1;
        }

        if (status != BSA_SUCCESS)
        {
            APP_ERROR1("BSA_HsRegister failed(%d)", status);
            return -1;
        }
    }

    APP_DEBUG0("Register complete");

    return 0;
}

/*******************************************************************************
 **
 ** Function         app_hs_init
 **
 ** Description      Init Headset application
 **
 ** Parameters
 **
 ** Returns          0 if successful execution, error code else
 **
 *******************************************************************************/
void app_hs_init(void)
{
    UINT8 index;

    memset(app_hs_battery_report, 0, BSA_HS_MAX_NUM_CONN * sizeof(int));
    memset(&app_hs_cb, 0, sizeof(app_hs_cb));

    for(index=0; index < BSA_HS_MAX_NUM_CONN ; index++)
    {
        app_hs_cb.connections[index].uipc_channel = UIPC_CH_ID_BAD;
        app_hs_cb.connections[index].sample_rate = APP_HS_SAMPLE_RATE;
    }
}

/*******************************************************************************
**
** Function         app_hs_hold_call
**
** Description      Hold active call
**
** Parameters       void
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_hold_call(UINT16 handle, tBSA_BTHF_CHLD_TYPE_T type)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn;

    printf("app_hs_hold_call\n");

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_CHLD_CMD;
    cmd_param.data.num = type;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_last_num_dial
**
** Description      Re-dial last dialed number
**
** Parameters       void
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_last_num_dial(UINT16 handle)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn;

    printf("app_hs_last_num_dial\n");

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }


    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_BLDN_CMD;

    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_dial_num
**
** Description      Dial a phone number
**
** Parameters       Phone number string
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_dial_num(UINT16 handle, const char *num)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn;

    printf("app_hs_dial_num\n");

    if((num == NULL) || (strlen(num) == 0))
    {
        APP_DEBUG0("empty number string");
        return -1;
    }

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_D_CMD;

    strcpy(cmd_param.data.str, num);
    strcat(cmd_param.data.str, ";");
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_send_unat
**
** Description      Send an unknown AT Command
**
** Parameters       char *cCmd
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_unat(UINT16 handle, char *cCmd)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_send_unat:Command : %s\n", cCmd);

    if(NULL==cCmd)
    {
        APP_DEBUG0("invalid AT Command");
        return -1;
    }

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }


    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_UNAT_CMD;

    strncpy(cmd_param.data.str, cCmd, sizeof(cmd_param.data.str)-1);

    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_send_clcc
**
** Description      Send CLCC AT Command for obtaining list of current calls
**
** Parameters       None
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_clcc_cmd(UINT16 handle)
{
    printf("app_hs_send_clcc_cmd\n");
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_CLCC_CMD;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_send_cops_cmd
**
** Description      Send COPS AT Command to obtain network details and set network details
**
** Parameters       char *cCmd
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_cops_cmd(UINT16 handle, char *cCmd)
{
    printf("app_hs_send_cops_cmd\n");
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    strncpy(cmd_param.data.str, cCmd, sizeof(cmd_param.data.str)-1);
    cmd_param.command = BSA_HS_COPS_CMD;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_send_ind_cmd
**
** Description      Send indicator (CIND) AT Command
**
** Parameters       None
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_ind_cmd(UINT16 handle)
{
    printf("app_hs_send_cind_cmd\n");
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }


    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_CIND_CMD;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_mute_unmute_microphone
**
** Description      Send Mute / unmute commands
**
** Returns          Returns SUCCESS or FAILURE on sending mute/unmute command
*******************************************************************************/
int app_hs_mute_unmute_microphone(UINT16 handle, BOOLEAN bMute)
{
    char str[20];
    memset(str,'\0',sizeof(str));
    strcpy(str, "+CMUT=");

    if(bMute)
        strcat(str, "1");
    else
        strcat(str, "0");
    return app_hs_send_unat(handle, str);
}

/*******************************************************************************
**
** Function         app_hs_send_dtmf
**
** Description      Send DTMF AT Command
**
** Parameters       char dtmf (0-9, #, A-D)
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_dtmf(UINT16 handle, char dtmf)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_send_dtmf:Command : %x\n", dtmf);

    /* If no connection exist, error */
    if('\0'==dtmf)
    {
        APP_DEBUG0("invalid DTMF Command");
        return -1;
    }

    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_VTS_CMD;

    cmd_param.data.str[0] = dtmf;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_send_cnum
**
** Description      Send CNUM AT Command
**
** Parameters       No parameter needed
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_cnum(UINT16 handle)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_send_cnum:Command \n");

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_CNUM_CMD;
    cmd_param.data.num=0;

    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_send_keypress_evt
**
** Description      Send Keypress event
**
** Parameters       char *cCmd - Keyboard sequence (0-9, * , #)
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_send_keypress_evt(UINT16 handle, char *cCmd)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_send_keypress_evt:Command \n");

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;
    cmd_param.command = BSA_HS_CKPD_CMD;
    strncpy(cmd_param.data.str, cCmd, sizeof(cmd_param.data.str)-1);

    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_start_voice_recognition
**
** Description      Start voice recognition
**
** Parameters       None
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_start_voice_recognition(UINT16 handle)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_start_voice_recognition:Command \n");

    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    if(p_conn->peer_feature & BSA_HS_PEER_FEAT_VREC)
    {
        BSA_HsCommandInit(&cmd_param);
        cmd_param.hndl = p_conn->handle;
        cmd_param.command = BSA_HS_BVRA_CMD;
        cmd_param.data.num = 1;

        BSA_HsCommand(&cmd_param);
        return 0;
    }

    APP_DEBUG0("Peer feature - VR feature not available");
    return -1;
}


/*******************************************************************************
**
** Function         app_hs_stop_voice_recognition
**
** Description      Stop voice recognition
**
** Parameters       None
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_stop_voice_recognition(UINT16 handle)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_stop_voice_recognition:Command \n");

    /* If no connection exit, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    if(p_conn->peer_feature & BSA_HS_PEER_FEAT_VREC)
    {
        BSA_HsCommandInit(&cmd_param);
        cmd_param.hndl = p_conn->handle;
        cmd_param.command = BSA_HS_BVRA_CMD;
        cmd_param.data.num = 0;
        BSA_HsCommand(&cmd_param);
        return 0;
   }

   APP_DEBUG0("Peer feature - VR feature not available");
   return -1;
}

/*******************************************************************************
**
** Function         app_hs_set_volume
**
** Description      Send volume AT Command
**
** Parameters       tBSA_BTHF_VOLUME_TYPE_T type, int volume
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_set_volume(UINT16 handle, tBSA_BTHF_VOLUME_TYPE_T type, int volume)
{
    tBSA_HS_COMMAND cmd_param;
    tAPP_HS_CONNECTION *p_conn=NULL;

    printf("app_hs_set_volume, Command: %d, %d\n", type, volume);

    /* If no connection exist, error */
    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    BSA_HsCommandInit(&cmd_param);
    cmd_param.hndl = p_conn->handle;

    if(BTHF_VOLUME_TYPE_SPK == type)
        cmd_param.command = BSA_HS_SPK_CMD;
    else
        cmd_param.command = BSA_HS_MIC_CMD;

    cmd_param.data.num = volume;
    BSA_HsCommand(&cmd_param);
    return 0;
}

/*******************************************************************************
**
** Function         app_hs_getallIndicatorValues
**
** Description      Get all indicator values
**
** Parameters       tBSA_HS_IND_VALS *pIndVals
**
** Returns          0 if successful execution, error code else
**
*******************************************************************************/
int app_hs_getallIndicatorValues(UINT16 handle, tBSA_HS_IND_VALS *pIndVals)
{
    tAPP_HS_CONNECTION * p_conn = NULL;

    p_conn = app_hs_get_conn_by_handle(handle);
    if(NULL!=p_conn && NULL!=pIndVals)
    {
       pIndVals->curr_callwait_ind = 0;
       pIndVals->curr_signal_strength_ind = p_conn->curr_signal_strength_ind;
       pIndVals->curr_battery_ind = p_conn->curr_battery_ind;
       pIndVals->curr_call_ind = p_conn->curr_call_ind;
       pIndVals->curr_call_setup_ind = p_conn->curr_call_setup_ind;
       pIndVals->curr_roam_ind = p_conn->curr_roam_ind;
       pIndVals->curr_service_ind = p_conn->curr_service_ind;
       pIndVals->curr_callheld_ind = p_conn->curr_callheld_ind;
       if(pIndVals->curr_call_ind == BSA_HS_CALL_ACTIVE &&
          pIndVals->curr_call_setup_ind == BSA_HS_CALLSETUP_INCOMING)
       {
           pIndVals->curr_callwait_ind = 1;
       }
       return 0;
    }
    return -1;
}

#ifdef PCM_ALSA
#ifndef PCM_ALSA_DISABLE_HS
#if (BSA_SCO_ROUTE_DEFAULT == BSA_SCO_ROUTE_PCM)
static int set_sw_params(snd_pcm_t *pcm, snd_pcm_uframes_t buffer_size,
                snd_pcm_uframes_t period_size, char **msg)
{
    snd_pcm_sw_params_t *params;
    int err;

    snd_pcm_sw_params_malloc(&params);
    if ((err = snd_pcm_sw_params_current(pcm, params)) != 0) {
        APP_ERROR1("Get current params: %s", snd_strerror(err));
        return -1;
    }

    /* start the transfer when the buffer is full (or almost full) */
    snd_pcm_uframes_t threshold = (buffer_size / period_size) * period_size;
    if ((err = snd_pcm_sw_params_set_start_threshold(pcm, params, threshold)) != 0) {
        APP_ERROR1("Set start threshold: %s: %lu", snd_strerror(err), threshold);
        return -1;
    }

    /* allow the transfer when at least period_size samples can be processed */
    if ((err = snd_pcm_sw_params_set_avail_min(pcm, params, period_size)) != 0) {
        APP_ERROR1("Set avail min: %s: %lu", snd_strerror(err), period_size);
        return -1;
    }

    if ((err = snd_pcm_sw_params(pcm, params)) != 0) {
        APP_ERROR1("snd_pcm_sw_params: %s", snd_strerror(err));
        return -1;
    }

    if(params)
        snd_pcm_sw_params_free(params);
    return 0;
}

static int app_hs_playback_device_open(snd_pcm_t** playback_handle,
        const char* device_name, int channels, uint32_t write_sampleRate,
        snd_pcm_uframes_t write_periodSize, snd_pcm_uframes_t write_bufferSize)
{
    snd_pcm_hw_params_t *write_params;
    int write_err;
    int dir_write = 0;

    write_err = snd_pcm_open(playback_handle, device_name, SND_PCM_STREAM_PLAYBACK, 0);
    if (write_err) {
        APP_ERROR1( "Unable to open playback PCM device: %s", device_name);
        return -1;
    }
    APP_DEBUG1("Open playback PCM device: %s", device_name);

    write_err = snd_pcm_hw_params_malloc(&write_params);
    if (write_err) {
        APP_ERROR1("cannot malloc hardware parameter structure (%s)", snd_strerror(write_err));
        return -1;
    }

    write_err = snd_pcm_hw_params_any(*playback_handle, write_params);
    if (write_err) {
        APP_ERROR1("cannot initialize hardware parameter structure (%s)", snd_strerror(write_err));
        return -1;
    }

    write_err = snd_pcm_hw_params_set_access(*playback_handle, write_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (write_err) {
        APP_ERROR0("Error setting interleaved mode");
        return -1;
    }

    write_err = snd_pcm_hw_params_set_format(*playback_handle, write_params, SND_PCM_FORMAT_S16_LE);
    if (write_err) {
        APP_ERROR1("Error setting format: %s", snd_strerror(write_err));
        return -1;
    }

    write_err = snd_pcm_hw_params_set_channels(*playback_handle, write_params, channels);
    if (write_err) {
        APP_ERROR1( "Error setting channels: %s", snd_strerror(write_err));
        return -1;
    }

    APP_DEBUG1("WANT-RATE =%d", write_sampleRate);
    if (write_sampleRate) {
        write_err = snd_pcm_hw_params_set_rate_near(*playback_handle, write_params,&write_sampleRate, 0 /*&write_dir*/);
        if (write_err) {
            APP_ERROR1("Error setting sampling rate (%d): %s", write_sampleRate, snd_strerror(write_err));
            return -1;
        }
        APP_DEBUG1("setting sampling rate (%d)", write_sampleRate);
    }

    write_err = snd_pcm_hw_params_set_period_size_near(*playback_handle, write_params, &write_periodSize, &dir_write/*0*/);
    if (write_err) {
        APP_ERROR1("Error setting period time (%ld): %s", write_periodSize, snd_strerror(write_err));
        return -1;
    }
    APP_DEBUG1("write_periodSize = %d", (int)write_periodSize);

    write_err = snd_pcm_hw_params_set_buffer_size_near(*playback_handle, write_params, &write_bufferSize);
    if (write_err) {
        APP_ERROR1("Error setting buffer size (%ld): %s", write_bufferSize, snd_strerror(write_err));
        return -1;
    }
    APP_DEBUG1("write_bufferSize = %d", (int)write_bufferSize);

    /* Write the parameters to the driver */
    write_err = snd_pcm_hw_params(*playback_handle, write_params);
    if (write_err < 0) {
        APP_ERROR1( "Unable to set HW parameters: %s", snd_strerror(write_err));
        return -1;
    }

    APP_DEBUG1("Open playback device is successful: %s", device_name);

    set_sw_params(*playback_handle, write_bufferSize, write_periodSize, NULL);
    if (write_params)
        snd_pcm_hw_params_free(write_params);

    return 0;
}

static int app_hs_capture_device_open(snd_pcm_t** capture_handle,
        const char* device_name, int channels, uint32_t rate,
        snd_pcm_uframes_t periodSize, snd_pcm_uframes_t bufferSize)
{
    snd_pcm_hw_params_t *hw_params;
    int err;

    err = snd_pcm_open(capture_handle, device_name, SND_PCM_STREAM_CAPTURE, 0);
    if (err) {
        APP_ERROR1( "Unable to open capture PCM device: %s", device_name);
        return -1;
    }
    APP_DEBUG1("Open capture PCM device: %s", device_name);

    err = snd_pcm_hw_params_malloc(&hw_params);
    if (err) {
        APP_ERROR1("cannot allocate hardware parameter structure (%s)", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_any(*capture_handle, hw_params);
    if (err) {
        APP_ERROR1("cannot initialize hardware parameter structure (%s)", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_access(*capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err) {
        APP_ERROR0("Error setting interleaved mode");
        return -1;
    }

    err = snd_pcm_hw_params_set_format(*capture_handle, hw_params, SND_PCM_FORMAT_S16_LE);
    if (err) {
        APP_ERROR1("Error setting format: %s", snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_channels(*capture_handle, hw_params, channels);
    if (err) {
        APP_ERROR1( "Error setting channels: %s, channels = %d", snd_strerror(err), channels);
        return -1;
    }

    if (rate) {
        err = snd_pcm_hw_params_set_rate_near(*capture_handle, hw_params, &rate, 0);
        if (err) {
            APP_ERROR1("Error setting sampling rate (%d): %s", rate, snd_strerror(err));
            return -1;
        }
        APP_DEBUG1("setting sampling rate (%d)", rate);
    }

    err = snd_pcm_hw_params_set_period_size_near(*capture_handle, hw_params, &periodSize, 0);
    if (err) {
        APP_ERROR1("Error setting period time (%d): %s", (int)periodSize, snd_strerror(err));
        return -1;
    }
    APP_DEBUG1("periodSize = %d", (int)periodSize);

    err = snd_pcm_hw_params_set_buffer_size_near(*capture_handle, hw_params, &bufferSize);
    if (err) {
        APP_ERROR1("Error setting buffer size (%d): %s", (int)bufferSize, snd_strerror(err));
        return -1;
    }
    APP_DEBUG1("bufferSize = %d", (int)bufferSize);

    /* Write the parameters to the driver */
     err = snd_pcm_hw_params(*capture_handle, hw_params);
     if (err < 0) {
         APP_ERROR1( "Unable to set HW parameters: %s", snd_strerror(err));
         return -1;
     }

     APP_DEBUG1("Open capture device is successful: %s", device_name);
     if (hw_params)
        snd_pcm_hw_params_free(hw_params);

     return 0;
}

static void app_hs_pcm_close(snd_pcm_t *handle)
{
    if(handle)
        snd_pcm_close(handle);
}

static void *app_hs_alsa_playback(void *arg)
{
    int err, ret = -1;
    snd_pcm_t *capture_handle = NULL;
    snd_pcm_t *playbcak_handle = NULL;
    short buffer[READ_FRAME_1024 * 2] = {0};

    snd_pcm_uframes_t periodSize = PERIOD_SIZE_1024;
    snd_pcm_uframes_t bufferSize = BUFFER_SIZE_4096;

device_open:
    APP_DEBUG0("==========bt asound capture, alsa playback============");
    ret = app_hs_capture_device_open(&capture_handle, bt_capture_device, APP_HS_PCM_CHANNEL_NB,
                APP_HS_PCM_SAMPLE_RATE, periodSize, bufferSize);
    if (ret == -1) {
        APP_ERROR1("capture device open failed: %s", bt_capture_device);
        goto exit;
    }

    ret = app_hs_playback_device_open(&playbcak_handle, alsa_playback_device, APP_HS_PCM_CHANNEL_NB,
                APP_HS_PCM_SAMPLE_RATE, periodSize, bufferSize);
    if (ret == -1) {
        APP_ERROR1("playback device open failed: %s", alsa_playback_device);
        goto exit;
    }

    alsa_duplex_opened = TRUE;

    while (alsa_duplex_opened) {
        err = snd_pcm_readi(capture_handle, buffer , READ_FRAME_1024);
        if (!alsa_duplex_opened)
            goto exit;

        if (err != READ_FRAME_1024) {
            APP_ERROR1("=====read frame error = %d=====", err);
            //gettimeofday(&tv_begin, NULL);
            //APP_ERROR1("timeread = %ld\n",(tv_begin.tv_sec * 1000 + tv_begin.tv_usec / 1000));
        }

        if (err == -EPIPE)
            APP_ERROR1("Overrun occurred: %d", err);

        if (err < 0) {
            err = snd_pcm_recover(capture_handle, err, 0);
            // Still an error, need to exit.
            if (err < 0) {
                APP_ERROR1( "Error occured while recording: %s", snd_strerror(err));
                usleep(100 * 1000);
                app_hs_pcm_close(capture_handle);
                app_hs_pcm_close(playbcak_handle);
                goto device_open;
            }
        }

        err = snd_pcm_writei(playbcak_handle, buffer, READ_FRAME_1024);
        if (!alsa_duplex_opened)
            goto exit;

        if (err != READ_FRAME_1024) {
            //gettimeofday(&tv_begin, NULL);
            APP_ERROR1("=====write frame error = %d=====", err);
            //printf("time_write = %ld\n",(tv_begin.tv_sec * 1000 +tv_begin.tv_usec / 1000));
        }

        if (err == -EPIPE)
            APP_ERROR1("Underrun occurred from write: %d", err);

        if (err < 0) {
            err = snd_pcm_recover(playbcak_handle, err, 0);
            //std::this_thread::sleep_for(std::chrono::milliseconds(20));
            // Still an error, need to exit.
            if (err < 0) {
                APP_ERROR1( "Error occured while writing: %s", snd_strerror(err));
                usleep(100 * 1000);
                app_hs_pcm_close(capture_handle);
                app_hs_pcm_close(playbcak_handle);
                goto device_open;
            }
        }
    }

exit:
    app_hs_pcm_close(capture_handle);
    app_hs_pcm_close(playbcak_handle);

    APP_DEBUG0("Exit app hs alsa playback thread");
    pthread_exit(0);
}

static void app_hs_tinymix_set(int group, int volume)
{
    char cmd[50] = {0};

    sprintf(cmd, "tinymix set 'ADC MIC Group %d Left Volume' %d", group, volume);
    if (-1 == system(cmd))
        APP_ERROR1("tinymix set ADC MIC Group %d Left Volume failed", group);

    memset(cmd, 0, 50);
    sprintf(cmd, "tinymix set 'ADC MIC Group %d Right Volume' %d", group, volume);
    if (-1 == system(cmd))
        APP_ERROR1("tinymix set ADC MIC Group %d Right Volume failed", group);
}

//#define WRITE_PCM_FILE
static void *app_hs_bt_playback(void *arg)
{
    int i = 0, j = 0;
    int err, ret = -1;
    snd_pcm_t *capture_handle = NULL;
    snd_pcm_t *playbcak_handle = NULL;
    int buffer_size = READ_FRAME * APP_HS_6MIC_LOOPBACK_CHANNEL_NB;
    int playback_buf_size = READ_FRAME * APP_HS_PCM_CHANNEL_NB;
    short buffer[buffer_size] = {0};
    short playback_buf[playback_buf_size] = {0};

#ifdef WRITE_PCM_FILE
    int write_size = 0;
    int fd = open("/data/bsa/hfp.pcm", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        APP_ERROR1("open hfp.pcm failed: %d", errno);
#endif

device_open:
    APP_DEBUG0("==========microphone capture, bt asound playback============");
    ret = app_hs_capture_device_open(&capture_handle, alsa_capture_device, APP_HS_6MIC_LOOPBACK_CHANNEL_NB,
                APP_HS_PCM_SAMPLE_RATE, PERIOD_SIZE, BUFFER_SIZE);
    if (ret == -1) {
        APP_ERROR1("capture device open failed: %s", alsa_capture_device);
        goto exit;
    }

    app_hs_tinymix_set(1, 3);

    ret = app_hs_playback_device_open(&playbcak_handle, bt_playback_device, APP_HS_PCM_CHANNEL_NB,
                APP_HS_PCM_SAMPLE_RATE, PERIOD_SIZE, BUFFER_SIZE);
    if (ret == -1) {
        APP_ERROR1("playback device open failed: %s", bt_playback_device);
        goto exit;
    }

    bt_duplex_opened = TRUE;

    while (bt_duplex_opened) {
        err = snd_pcm_readi(capture_handle, buffer , READ_FRAME);
        if (!bt_duplex_opened)
            goto exit;

        if (err != READ_FRAME)
            APP_ERROR1("=====read frame error = %d=====", err);

        if (err == -EPIPE)
            APP_ERROR1("Overrun occurred: %d", err);

        if (err < 0) {
            err = snd_pcm_recover(capture_handle, err, 0);
            // Still an error, need to exit.
            if (err < 0) {
                APP_ERROR1( "Error occured while recording: %s", snd_strerror(err));
                usleep(100 * 1000);
                app_hs_pcm_close(capture_handle);
                app_hs_pcm_close(playbcak_handle);
                goto device_open;
            }
        }

        /* Extract the data of the third and fourth channel */
        i = 0, j = 0;
        do {
            playback_buf[i] = buffer[j + 2];
            playback_buf[i + 1] = buffer[j + 3];

            i += APP_HS_PCM_CHANNEL_NB;
            j += APP_HS_6MIC_LOOPBACK_CHANNEL_NB;
        } while (i < playback_buf_size && j < buffer_size);

#ifdef WRITE_PCM_FILE
        if(fd >= 0) {
            write_size = write(fd, (UINT8 *)buffer, buffer_size * 2);
            if (write_size != buffer_size * 2)
                APP_ERROR1("write pcm data failed, write_size: %d", write_size);
        }
#endif

        err = snd_pcm_writei(playbcak_handle, playback_buf, READ_FRAME);
        if (!bt_duplex_opened)
            goto exit;

        if (err != READ_FRAME)
            APP_ERROR1("====write frame error = %d===",err);

        if (err == -EPIPE)
            APP_ERROR1("Underrun occurred from write: %d", err);

        if (err < 0) {
            err = snd_pcm_recover(playbcak_handle, err, 0);
            //std::this_thread::sleep_for(std::chrono::milliseconds(20));
            // Still an error, need to exit.
            if (err < 0) {
                APP_ERROR1( "Error occured while writing: %s", snd_strerror(err));
                usleep(100 * 1000);
                app_hs_pcm_close(capture_handle);
                app_hs_pcm_close(playbcak_handle);
                goto device_open;
            }
        }
    }

exit:
    app_hs_pcm_close(capture_handle);
    app_hs_pcm_close(playbcak_handle);

#ifdef WRITE_PCM_FILE
    if(fd >= 0)
        close(fd);
#endif

    APP_DEBUG0("Exit app hs bt pcm playback thread");
    pthread_exit(0);
}

static int app_hs_open_alsa_duplex(UINT16 handle)
{
    if (!alsa_duplex_opened) {
        if (pthread_create(&alsa_tid, NULL, app_hs_alsa_playback, NULL)) {
            APP_ERROR0("Create alsa duplex thread failed");
            return -1;
        }
    }

    return 0;
}

static int app_hs_close_alsa_duplex(void)
{
    APP_DEBUG0("app_hs_close_alsa_duplex start");
    alsa_duplex_opened = FALSE;
    if (alsa_tid) {
        pthread_join(alsa_tid, NULL);
        alsa_tid = 0;
    }

    APP_DEBUG0("app_hs_close_alsa_duplex end");
    return 0;
}

static int app_hs_open_bt_duplex(void)
{
    if (!bt_duplex_opened) {
        if (pthread_create(&bt_tid, NULL, app_hs_bt_playback, NULL)) {
            APP_ERROR0("Create bt pcm duplex thread failed");
            return -1;
        }
    }

    return 0;
}

static int app_hs_close_bt_duplex(void)
{
    APP_DEBUG0("app_hs_close_bt_duplex start");
    bt_duplex_opened = FALSE;
    if (bt_tid) {
        pthread_join(bt_tid, NULL);
        bt_tid = 0;
    }

    APP_DEBUG0("app_hs_close_bt_duplex end");
    return 0;
}

#else
/*******************************************************************************
**
** Function         app_hs_open_alsa_duplex
**
** Description      function to open ALSA driver
**
** Parameters       None
**
** Returns          void
**
*******************************************************************************/
static int app_hs_open_alsa_duplex(UINT16 handle)
{
    tAPP_HS_CONNECTION * p_conn;
    int status;

    /* If ALSA PCM driver was already open => close it */
    if (alsa_handle_playback != NULL)
    {
        snd_pcm_close(alsa_handle_playback);
        alsa_handle_playback = NULL;
    }

    APP_DEBUG0("Opening Alsa/Asound audio driver Playback");

    p_conn = app_hs_get_conn_by_handle(handle);
    if(p_conn == NULL)
    {
        APP_ERROR0("no connection available");
        return -1;
    }

    /* Open ALSA driver */
    status = snd_pcm_open(&alsa_handle_playback, alsa_device,
        SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (status < 0)
    {
        APP_ERROR1("snd_pcm_open failed: %s", snd_strerror(status));
        return status;
    }
    else
    {
        /* Configure ALSA driver with PCM parameters */
        status = snd_pcm_set_params(alsa_handle_playback,
            SND_PCM_FORMAT_S16_LE,
            SND_PCM_ACCESS_RW_INTERLEAVED,
            APP_HS_CHANNEL_NB,
            p_conn->sample_rate,
            1, /* SW resample */
            500000);/* 500msec */
        if (status < 0)
        {
            APP_ERROR1("snd_pcm_set_params failed: %s", snd_strerror(status));
            return status;
        }
    }
    alsa_playback_opened = TRUE;

    /* If ALSA PCM driver was already open => close it */
    if (alsa_handle_capture != NULL)
    {
        snd_pcm_close(alsa_handle_capture);
        alsa_handle_capture = NULL;
    }
    APP_DEBUG0("Opening Alsa/Asound audio driver Capture");

    status = snd_pcm_open(&alsa_handle_capture, alsa_device,
                            SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (status < 0)
    {
        APP_ERROR1("snd_pcm_open failed: %s", snd_strerror(status));
        return status;
    }
    else
    {
        /* Configure ALSA driver with PCM parameters */
        status = snd_pcm_set_params(alsa_handle_capture,
                SND_PCM_FORMAT_S16_LE,
                SND_PCM_ACCESS_RW_INTERLEAVED,
                APP_HS_CHANNEL_NB,
                p_conn->sample_rate,
                1, /* SW resample */
                500000);/* 500msec */
        if (status < 0)
        {
            APP_ERROR1("snd_pcm_set_params failed: %s", snd_strerror(status));
            return status;
        }
    }
    alsa_capture_opened = TRUE;

    return 0;
}

/*******************************************************************************
**
** Function         app_hs_close_alsa_duplex
**
** Description      function to close ALSA driver
**
** Parameters       None
**
** Returns          void
**
*******************************************************************************/
static int app_hs_close_alsa_duplex(void)
{
    if (alsa_handle_playback != NULL)
    {
        snd_pcm_close(alsa_handle_playback);
        alsa_handle_playback = NULL;
        alsa_playback_opened = FALSE;
    }
    if (alsa_handle_capture != NULL)
    {
        snd_pcm_close(alsa_handle_capture);
        alsa_handle_capture = NULL;
        alsa_capture_opened = FALSE;
    }
    return 0;
}
#endif
#endif
#endif /* PCM_ALSA */

static int app_hs_latest_connect()
{
    int index;

    index = app_mgr_get_latest_device();
    if(index < 0 || index >= APP_MAX_NB_REMOTE_STORED_DEVICES) {
        APP_DEBUG0("can't find latest connected device");
        return -1;
    }

    return app_hs_open(&app_xml_remote_devices_db[index].bd_addr);
}

int app_hs_initialize()
{
    int connect_cnt = 2;

    /* Init Headset Application */
    app_hs_init();

    /* Start Headset service*/
    if(app_hs_start(NULL) < 0) {
        APP_ERROR0("Start Headset service failed");
        return -1;
    }

    if(app_mgr_is_reconnect()) {
        while(connect_cnt--) {
            if(app_hs_latest_connect() == 0)
                break;
        }
    }

    return 0;
}

void app_hs_deinitialize()
{
    if(app_hs_cb.is_pick_up) {
        app_hs_hang_up();
        GKI_delay(1000);
    }

    app_hs_close_all();
    GKI_delay(1000);

    app_hs_stop();
    app_hs_deregister_cb();
}

int app_hs_pick_up()
{
    UINT16 index, handle;

    if(app_hs_cb.is_pick_up) {
        APP_DEBUG0("The phone has been picked up");
        return 0;
    }

    for(index = 0; index < BSA_HS_MAX_NUM_CONN; index++) {
        //APP_DEBUG1("app_hs_cb.connections[%d].connection_active: %d",
        //    index, app_hs_cb.connections[index].connection_active);
        if(app_hs_cb.connections[index].connection_active) {
            handle = app_hs_cb.connections[index].handle;
            if(app_hs_answer_call(handle) < 0) {
                APP_ERROR0("app_hs_answer_call failed");
                return -1;
            }

        }
    }

    app_hs_cb.is_pick_up = TRUE;

    return 0;
}

int app_hs_hang_up()
{
    UINT16 index, handle;

    if(!app_hs_cb.is_pick_up) {
        APP_DEBUG0("The phone has been hanged up");
        return 0;
    }

    for(index = 0; index < BSA_HS_MAX_NUM_CONN; index++) {
        //APP_DEBUG1("app_hs_cb.connections[%d].connection_active: %d",
        //    index, app_hs_cb.connections[index].connection_active);
        if(app_hs_cb.connections[index].connection_active) {
            handle = app_hs_cb.connections[index].handle;
            if(app_hs_hangup(handle) < 0) {
                APP_ERROR0("app_hs_hangup failed");
                return -1;
            }
        }
    }

    app_hs_cb.is_pick_up = FALSE;

    return 0;
}

int app_hs_redial()
{
    UINT16 index, handle;

    if(app_hs_cb.is_pick_up) {
        APP_DEBUG0("The phone has been picked up");
        return 0;
    }

    for(index = 0; index < BSA_HS_MAX_NUM_CONN; index++) {
        //APP_DEBUG1("app_hs_cb.connections[%d].connection_active: %d",
        //    index, app_hs_cb.connections[index].connection_active);
        if(app_hs_cb.connections[index].connection_active) {
            handle = app_hs_cb.connections[index].handle;
            if(app_hs_last_num_dial(handle) < 0) {
                APP_ERROR0("app_hs_last_num_dial failed");
                return -1;
            }
        }
    }

    app_hs_cb.is_pick_up = TRUE;

    return 0;
}

int app_hs_report_battery(int value)
{
    char at_cmd[100] = {0};
    UINT16 index, handle;

    if ((value < 0) || (value > 9)) {
        APP_ERROR0("ERROR: Invalid value, should within [0, 9]");
        return -1;
    }

    sprintf(at_cmd, "+IPHONEACCEV=1,1,%d", value);
    for(index = 0; index < BSA_HS_MAX_NUM_CONN; index++) {
        if(app_hs_cb.connections[index].connection_active) {
            handle = app_hs_cb.connections[index].handle;

            if(!app_hs_battery_report[index]) {
                if(app_hs_send_unat(handle,"+XAPL=ABCD-1234-0100,2") < 0) {
                    APP_ERROR0("send AT cmd failed: AT+XAPL=ABCD-1234-0100,2");
                    return -1;
                } else {
                    app_hs_battery_report[index] = 1;
                }
            }

            if(app_hs_send_unat(handle, at_cmd) < 0) {
                APP_ERROR1("app_hs_send_unat failed, at_cmd: %s", at_cmd);
                return -1;
            }
        }
    }

    return 0;
}

int app_hs_set_vol(int volume)
{
    UINT16 index, handle;

    if(volume < 0 || volume > 15) {
        APP_ERROR1("Invalid value(%d), should within [0, 15]", volume);
        return -1;
    }

    for(index = 0; index < BSA_HS_MAX_NUM_CONN; index++) {
        if(app_hs_cb.connections[index].connection_active) {
            handle = app_hs_cb.connections[index].handle;
            if(app_hs_set_volume(handle, BSA_HS_SPK_CMD, volume) < 0) {
                APP_ERROR0("app_hs_set_volume failed");
                return -1;
            }
        }
    }

    return 0;
}
