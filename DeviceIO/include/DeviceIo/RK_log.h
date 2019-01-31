#ifndef __RK_LOG_H__
#define __RK_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#define RK_LOGV(fmt, ...) printf("%s" fmt, log_prefix('V'), ##__VA_ARGS__)
#define RK_LOGI(fmt, ...) printf("%s" fmt, log_prefix('I'), ##__VA_ARGS__)
#define RK_LOGE(fmt, ...) printf("%s" fmt, log_prefix('E'), ##__VA_ARGS__)

char *log_prefix(char level);

#ifdef __cplusplus
}
#endif

#endif
