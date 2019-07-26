#ifndef __BT_BASE_H__
#define __BT_BASE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char	  uint8_t;
typedef unsigned short uint16_t;

typedef struct {
#define UUID_16     2
#define UUID_32     4
#define UUID_128    16

	uint16_t len; //byte
	const char *uuid;
} Ble_Uuid_Type_t;

enum {
	BLE_ADVDATA_TYPE_USER = 0,
	BLE_ADVDATA_TYPE_SYSTEM
};

typedef enum {
	RK_BT_BOND_STATE_NONE,
	RK_BT_BOND_STATE_BONDING,
	RK_BT_BOND_STATE_BONDED,
} RK_BT_BOND_STATE;

typedef struct {
	Ble_Uuid_Type_t server_uuid;
	Ble_Uuid_Type_t chr_uuid[12];
	uint8_t chr_cnt;
	const char *ble_name;
	uint8_t advData[256];
	uint8_t advDataLen;
	uint8_t respData[256];
	uint8_t respDataLen;
	uint8_t advDataType;
	//AdvDataKgContent adv_kg;
	char le_random_addr[6];
	/* recevice data */
	void (*cb_ble_recv_fun)(const char *uuid, char *data, int len);
	/* full data */
	void (*cb_ble_request_data)(const char *uuid);
} RkBleContent;

typedef struct {
	RkBleContent ble_content;
	const char *bt_name;
} RkBtContent;

struct paired_dev {
	char *remote_address;
	char *remote_name;
	bool is_connected;
	struct paired_dev *next;
};
typedef struct paired_dev bt_paried_device;

typedef void (*RK_BT_BOND_CALLBACK)(const char *bd_addr, const char *name, RK_BT_BOND_STATE state);

void rk_bt_register_bond_callback(RK_BT_BOND_CALLBACK cb);
int rk_bt_init(RkBtContent *p_bt_content);
int rk_bt_deinit(void);
int rk_bt_is_connected(void);
int rk_bt_set_class(int value);
int rk_bt_enable_reconnect(int value);
void rk_bt_start_discovery(unsigned int mseconds);
void rk_bt_cancel_discovery();
bool rk_bt_is_discovering();
void rk_bt_display_devices();
void rk_bt_display_paired_devices();
int rk_bt_pair_by_addr(char *addr);
int rk_bt_unpair_by_addr(char *addr);
int rk_bt_set_device_name(char *name);
int rk_bt_get_device_name(char *name, int len);
int rk_bt_get_device_addr(char *addr, int len);
int rk_bt_get_paired_devices(bt_paried_device **dev_list,int *count);
int rk_bt_free_paired_devices(bt_paried_device **dev_list);

#ifdef __cplusplus
}
#endif

#endif /* __BT_BASE_H__ */
