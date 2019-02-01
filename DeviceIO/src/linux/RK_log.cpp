#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "DeviceIo/RK_log.h"

#define MAX_BUFFER    (2048)

static pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;

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

static char *log_prefix(const char level)
{
	static char str[40];
	char *time_info, *thread_info;

	memset(str, 0, sizeof(str));
	time_info = get_time_info();
	thread_info = get_thread_info();

	snprintf(str, sizeof(str), "%s%s %c ", time_info, thread_info, level);

	return str;
}

static int RK_LOG(const char level, const char *format, va_list arg)
{
	static char buffer[MAX_BUFFER + 1];
	int done, len_prefix, len_buffer;
	char *prefix;

	prefix = log_prefix(level);
	len_prefix = strlen(prefix);

	strncpy(buffer, prefix, sizeof(buffer));
	done = vsnprintf(buffer + len_prefix, sizeof(buffer) - len_prefix, format, arg);
	len_buffer = strlen(buffer);
	if (buffer[len_buffer - 1] != '\n') {
		if (len_buffer == sizeof(buffer) - 1) {
			buffer[len_buffer - 1] = '\n';
		} else {
			buffer[len_buffer] = '\n';
		}
	}
	printf("%s", buffer);
	return done;
}

int RK_LOGV(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('V', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}

int RK_LOGI(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('I', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}

int RK_LOGE(const char *format, ...)
{
	va_list arg;
	int done;
	pthread_mutex_lock(&m_mutex);
	va_start (arg, format);
	done = RK_LOG('E', format, arg);
	va_end (arg);
	pthread_mutex_unlock(&m_mutex);
	return done;
}
