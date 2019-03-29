#ifndef __BLUETOOTH_HANDSFREE_H__
#define __BLUETOOTH_HANDSFREE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RK_BT_HFP_CONNECT_EVT,              /* HFP connection open */
    RK_BT_HFP_DISCONNECT_EVT,           /* HFP connection closed */
    RK_BT_HFP_CONNECT_FAILED_EVT,       /* HFP connection failed */
    RK_BT_HFP_RING_EVT,                 /* RING alert from AG */
    RK_BT_HFP_AUDIO_OPEN_EVT,           /* Audio connection open */
    RK_BT_HFP_AUDIO_CLOSE_EVT,          /* Audio connection closed */
    RK_BT_HFP_PICKIP,                   /* Call has been picked up */
    RK_BT_HFP_HANGUP,                   /* Call has been hung up */
} RK_BT_HFP_EVENT;

typedef int (*RK_BT_HFP_CALLBACK)(RK_BT_HFP_EVENT event);

void rk_bt_hfp_register_callback(RK_BT_HFP_CALLBACK cb);
int rk_bt_hfp_open(void);
int rk_bt_hfp_close(void);
void rk_bt_hfp_pickup(void);
void rk_bt_hfp_hangup(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_HANDSFREE_H__ */