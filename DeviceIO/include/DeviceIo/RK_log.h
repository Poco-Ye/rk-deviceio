#ifndef __RK_LOG_H__
#define __RK_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

int RK_LOGV(const char *format, ...);
int RK_LOGI(const char *format, ...);
int RK_LOGE(const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
