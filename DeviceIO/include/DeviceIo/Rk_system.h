#ifndef __RK_SYSTEM_H__
#define __RK_SYSTEM_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 *Version 1.0.1 Release 2019/01/07
  1.ble_wifi:	fix Chinese coding problem
		fix ble report event and add wifi priority
		add wrong key event callback
		add initBTForHis interface
  2.volume:	setVolume support zero
  3.propery:	implement RK_property
  4.player:	separate mediaplayer and deviceio
		add playlist function
 *Version 1.0.0 Release 2018/12/22
 */

#define DEVICEIO_VERSION "V1.0.1"

int RK_read_chip_id(char *buffer, const int size);
int RK_read_version(char *buffer, const int size);


#ifdef __cplusplus
}
#endif

#endif
