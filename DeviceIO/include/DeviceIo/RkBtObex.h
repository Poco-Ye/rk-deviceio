#ifndef __BLUETOOTH_OBEX_H__
#define __BLUETOOTH_OBEX_H__

#ifdef __cplusplus
extern "C" {
#endif

int rk_bt_obex_init(char *path);
int rk_bt_obex_pbap_init(void);
int rk_bt_obex_pbap_connect(char *btaddr);
int rk_bt_obex_pbap_get_vcf(char *dir_name, char *dir_file);
int rk_bt_obex_pbap_disconnect(char *btaddr);
int rk_bt_obex_pbap_deinit(void);
int rk_bt_obex_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_OBEX_H__ */
