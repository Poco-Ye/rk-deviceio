#ifndef __BT_HAL_P__
#define __BT_HAL_P__

#include <DeviceIo/RkBtMaster.h>
#include <DeviceIo/RkBtSink.h>
#include <DeviceIo/RkBle.h>
#include <DeviceIo/RkBtSpp.h>

int RK_bt_init(Bt_Content_t *p_bt_content);
/******************************************/
/*                    BLE                 */
/******************************************/
void RK_ble_test(void *data);
void RK_ble_audio_test(void *data);
/******************************************/
/*               A2DP SINK                */
/******************************************/
void bt_api2_sink_open(void *data);
void bt_api2_sink_visibility00(void *data);
void bt_api2_sink_visibility01(void *data);
void bt_api2_sink_visibility10(void *data);
void bt_api2_sink_visibility11(void *data);
void bt_api2_sink_play(void *data);
void bt_api2_sink_pause(void *data);
void bt_api2_sink_next(void *data);
void bt_api2_sink_previous(void *data);
void bt_api2_sink_stop(void *data);
void bt_api2_sink_reconnect_en0(void *data);
void bt_api2_sink_reconnect_en1(void *data);
void bt_api2_sink_disconnect(void *data);
void bt_api2_sink_close(void *data);
void bt_api2_sink_status(void *data);
/******************************************/
/*              A2DP SOURCE               */
/******************************************/
void bt_api2_master_start(void *data);
void bt_api2_master_stop(void *data);
void bt_api2_master_status(void *data);
void bt_init_open(void *data);

void bt_api2_spp_open(void *data);
void bt_api2_spp_write(void *data);
void bt_api2_spp_close(void *data);
void bt_api2_spp_status(void *data);

#endif /* __BT_HAL_P__ */
