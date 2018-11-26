#ifndef __A2DP_SOURCE_CTRL__
#define __A2DP_SOURCE_CTRL__

int init_a2dp_master_ctrl();
int release_a2dp_master_ctrl();
int a2dp_master_scan(void *data, int len);
int a2dp_master_connect(char *address);
int a2dp_master_disconnect(char *address);
int a2dp_master_status(char *addr_buf);
int a2dp_master_remove(char *address);

#endif /* __A2DP_SOURCE_CTRL__ */
