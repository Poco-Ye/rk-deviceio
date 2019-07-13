#ifndef RKBT_UTILITY_H
#define RKBT_UTILITY_H

#ifdef __cplusplus
extern "C" {
#endif

int test_pthread(pthread_t tid);
void bt_exec_command(const char cmdline[], char recv_buff[], int len);
int bt_exec_command_system(const char *cmd);
int bt_run_task(char *name, char *cmd);
int bt_kill_task(char *name);
int bt_get_ps_pid(const char Name[]);
int test_pthread(pthread_t tid); /*pthread_kill的返回值：成功（0） 线程不存在（ESRCH） 信号不合法（EINVAL）*/

#define msleep(x) usleep(x * 1000)

#ifdef __cplusplus
}
#endif

#endif //RKBT_UTILITY_H
