#ifndef __RK_LED_H__
#define __RK_LED_H__

#ifdef __cplusplus
extern "C" {
#endif


int RK_set_all_led_status(const int Rval, const int Gval, const int Bval);
int RK_set_all_led_off();


#ifdef __cplusplus
}
#endif

#endif
