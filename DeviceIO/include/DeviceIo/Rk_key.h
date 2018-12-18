#ifndef __RK_KEY_H__
#define __RK_KEY_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef int (*RK_input_callback)(const int key_code, const int key_value);
int RK_input_init(RK_input_callback input_callback_cb);
int RK_input_exit(void);


#ifdef __cplusplus
}
#endif

#endif
