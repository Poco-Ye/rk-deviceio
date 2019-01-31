#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "DeviceIo/RK_log.h"

static char *get_time_info(void)
{
	static char str[20];
	char ftime[16];
	struct timespec tout;
	struct tm* ltime;

	memset(str, 0, sizeof(str));
	memset(ftime, 0, sizeof(ftime));
	clock_gettime(CLOCK_REALTIME, &tout);
	ltime = localtime(&tout.tv_sec);

	strftime(ftime, sizeof(ftime), "%m-%d %H:%M:%S", ltime);
	snprintf(str, sizeof(str), "%s.%03ld", ftime, tout.tv_nsec / 1000000);

	return str;
}

static char *get_thread_info(void)
{
	static char str[20];
	char pid[7], tid[7];

	memset(str, 0, sizeof(str));
	memset(pid, 0, sizeof(pid));
	memset(tid, 0, sizeof(tid));

	snprintf(pid, sizeof(pid), "%6d", getpid());
	snprintf(tid, sizeof(tid), "%6d", (pid_t)syscall(__NR_gettid));
	snprintf(str, sizeof(str), "%s%s", pid, tid);

	return str;
}

char *log_prefix(char level)
{
	static char str[40];
	char *time_info, *thread_info;

	memset(str, 0, sizeof(str));
	time_info = get_time_info();
	thread_info = get_thread_info();

	snprintf(str, sizeof(str), "%s%s %c ", time_info, thread_info, level);

	return str;
}
