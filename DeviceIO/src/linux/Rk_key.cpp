#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/select.h>
#include "DeviceIo/Rk_key.h"

static RK_input_callback m_cb;
static pthread_t m_th;


static int m_event0 = -1;
static int m_event1 = -1;
static int m_event2 = -1;

static void* thread_key_monitor(void *arg)
{
	int max_fd;
	fd_set rfds;

	max_fd = (m_event0 > m_event1 ? m_event0 : m_event1);
	max_fd = (max_fd > m_event2 ? max_fd : m_event2);
	max_fd = max_fd + 1;

	FD_ZERO(&rfds);
	if (m_event0 > 0) {
		FD_SET(m_event0, &rfds);
	}

	if (m_event1 > 0) {
		FD_SET(m_event1, &rfds);
	}

	if (m_event2 > 0) {
		FD_SET(m_event2, &rfds);
	}

	int ret;
	struct input_event ev_key;
	while (1) {
		select(max_fd, &rfds, NULL, NULL, NULL);

		if (FD_ISSET(m_event0, &rfds)) {
			ret = read(m_event0, &ev_key, sizeof(ev_key));
		} else if (FD_ISSET(m_event1, &rfds)) {
			ret = read(m_event1, &ev_key, sizeof(ev_key));
		} else if (FD_ISSET(m_event2, &rfds)) {
			ret = read(m_event2, &ev_key, sizeof(ev_key));
		} else {
			continue;
		}

		if (ret == sizeof(ev_key)) {
			if(m_cb != NULL && ev_key.code != 0) {
				m_cb(ev_key.code, ev_key.value);
			}
		}
	}


	return NULL;
}

int RK_input_init(RK_input_callback input_callback_cb)
{
	int ret;

	m_cb = input_callback_cb;
	m_event0 = open("/dev/input/event0", O_RDONLY);
	if (m_event0 < 0) {
		printf("open /dev/input/event0 failed...\n");
	}

	m_event1 = open("/dev/input/event1", O_RDONLY);
	if (m_event1 < 0) {
		printf("open /dev/input/event1 failed...\n");
	}

	m_event2 = open("/dev/input/event2", O_RDONLY);
	if (m_event2 < 0) {
		printf("open /dev/input/event2 failed...\n");
	}

	ret = pthread_create(&m_th, NULL, thread_key_monitor, NULL);

	return ret;
}

int RK_input_exit(void)
{
	int ret;

	if (m_th > 0) {
		ret  = pthread_cancel(m_th);
		if (ret == 0) {
			pthread_join(m_th, NULL);
		}
		m_th = -1;
	}

	if (m_event0 > 0) {
		close(m_event0);
		m_event0 = -1;
	}

	if (m_event1 > 0) {
		close(m_event1);
		m_event1 = -1;
	}

	if (m_event2 > 0) {
		close(m_event2);
		m_event2 = -1;
	}

	return 0;
}
