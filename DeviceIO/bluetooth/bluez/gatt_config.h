#ifndef __BT_GATT_CONFIG_H__
#define __BT_GATT_CONFIG_H__

#include <DeviceIo/RkBtBase.h>

#ifdef __cplusplus
extern "C" {
#endif

int gatt_init(RkBtContent *bt_content);
int gatt_open(void);
void gatt_close(void);
void ble_enable_adv(void);
void ble_disable_adv(void);
unsigned int gatt_mtu(void);
int gatt_write_data(char *uuid, void *data, int len);

#ifdef __cplusplus
}
#endif

#endif /* __BT_GATT_CONFIG_H__ */

