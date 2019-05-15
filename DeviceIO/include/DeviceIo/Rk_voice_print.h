#ifndef _RK_VOICE_PRINT_H_
#define _RK_VOICE_PRINT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RK_VP_SSID_PSK_CALLBACK)(char* ssid, char* psk);

int rk_voice_print_start(void);
int rk_voice_print_stop(void);
void rk_voice_print_register_callback(RK_VP_SSID_PSK_CALLBACK cb);

#ifdef __cplusplus
}
#endif

#endif
