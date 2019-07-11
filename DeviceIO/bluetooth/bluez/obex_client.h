#ifdef __cplusplus
extern "C" {
#endif

int obex_main_thread(void);
void obex_connect_pbap(char *dev_addr);
void obex_get_pbap_pb(char *dir_name, char *dir_file);
void obex_disconnect(int argc, char *btaddr);
void obex_quit(void);

#ifdef __cplusplus
}
#endif
