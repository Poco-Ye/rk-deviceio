#ifndef __RK_SYSTEM_H__
#define __RK_SYSTEM_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 *Version 1.0 Release 2018/12/22
 */

#define DEVICEIO_VERSION "V1.0.0"

int RK_read_chip_id(char *buffer, const int size);
int RK_read_version(char *buffer, const int size);


#ifdef __cplusplus
}
#endif

#endif
