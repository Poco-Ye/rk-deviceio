#ifndef __BLUETOOTH_MASTER_H__
#define __BLUETOOTH_MASTER_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	RK_BtMasterEvent_Connect_Failed,
	RK_BtMasterEvent_Connected,
	RK_BtMasterEvent_Disconnected,
} RK_BtMasterEvent_e;

typedef enum {
	RK_BtMasterStatus_Connected,
	RK_BtMasterStatus_Disconnected,
} RK_BtMasterStatus;

typedef void (*RK_btmaster_callback)(void *userdata, const RK_BtMasterEvent_e enEvent);

int RK_btmaster_connect_start(void *userdata, RK_btmaster_callback cb);
int RK_btmaster_stop(void);
int RK_btmaster_getDeviceName(char *name, int len);
int RK_btmaster_getDeviceAddr(char *addr, int len);
int RK_btmaster_getStatus(RK_BtMasterStatus *pstatus);

#ifdef __cplusplus
}
#endif

#endif /* __BLUETOOTH_MASTER_H__ */