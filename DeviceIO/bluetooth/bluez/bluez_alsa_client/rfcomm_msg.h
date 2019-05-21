#ifndef __DEVICEIO_BA_RFCOMM_MSG__
#define __DEVICEIO_BA_RFCOMM_MSG__

#ifdef __cplusplus
extern "C" {
#endif

#include <DeviceIo/RkBtHfp.h>

int rfcomm_listen_ba_msg_start();
int rfcomm_listen_ba_msg_stop();
void rfcomm_hfp_hf_regist_cb(RK_BT_HFP_CALLBACK cb);

int rfcomm_hfp_open_audio_path();
int rfcomm_hfp_close_audio_path();

#ifdef __cplusplus
}
#endif

#endif /* __DEVICEIO_BA_RFCOMM_MSG__ */