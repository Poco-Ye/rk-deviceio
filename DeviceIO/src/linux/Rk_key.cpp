#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/select.h>
#include "DeviceIo/Rk_key.h"
#include "DeviceIo/RK_timer.h"

typedef int BOOL;

typedef struct RK_input_long_press_key {
	int key_code;
	uint32_t time;
	RK_input_long_press_callback cb;
	struct RK_input_long_press_key *next;
} RK_input_long_press_key_t;

typedef struct RK_input_long_press {
	int key_code;
	RK_input_long_press_key *event;
	struct RK_input_long_press *next;
} RK_input_long_press_t;

typedef struct RK_input_compose_press {
	char *keys;
	uint32_t time;
	RK_input_compose_press_callback cb;
	struct RK_input_compose_press *next;
} RK_input_compose_press_t;

typedef struct RK_input_pressed {
	int key_code;
	struct RK_input_pressed *next;
} RK_input_pressed_t;

typedef struct RK_input_timer {
	RK_Timer_t *timer;
	RK_input_long_press_callback cb_long_press;
	RK_input_compose_press_callback cb_compose_press;
	int key_code;
	char *keys;
} RK_input_timer_t;

static RK_input_long_press_t *m_long_press_head = NULL;
static RK_input_compose_press_t *m_compose_press_head = NULL;
static RK_input_pressed_t *m_key_pressed_head = NULL;

static RK_input_callback m_cb;
static RK_input_press_callback m_input_press_cb;
static RK_input_timer_t m_input_timer;

static pthread_t m_th;
static int m_event0 = -1;
static int m_event1 = -1;
static int m_event2 = -1;

static BOOL is_compose_press_event_ready(RK_input_compose_press_t *comp)
{
	char *p;
	RK_input_pressed_t *key;
	if (comp == NULL || comp->keys == NULL)
		return 0;

	char str[strlen(comp->keys) + 1];

	memset(str, 0, sizeof(str));
	strcpy(str, comp->keys);
	p = strtok(str, " ");
	while (p) {
		key = m_key_pressed_head;
		while (key) {
			if (key->key_code == atoi(p)) {
				break;
			}
			key = key->next;
		}
		if (key == NULL)
			return 0;

		p = strtok(NULL, " ");
	}
	return 1;
}

static BOOL check_compose_press_event(RK_input_compose_press_t *comp)
{
	RK_input_pressed_t *key;
	RK_input_compose_press_t *target;
	char str[8];
	int ret;

	ret = 0;
	key = m_key_pressed_head;
	target = m_compose_press_head;

	while (key) {
		memset(str, 0, sizeof(str));
		snprintf(str, sizeof(str), "%d ", key->key_code);
		while (target) {
			if (strstr(target->keys, str)) {
				ret = 1;
				if (is_compose_press_event_ready(target)) {
					if (target) {
						comp->keys = (char*) malloc(strlen(target->keys));
						strcpy(comp->keys, target->keys);
						comp->time = target->time;
						comp->cb = target->cb;
					}
					return ret;
				}
			}
			target = target->next;
		}
		key = key->next;
	}

	return ret;
}

static RK_input_long_press_key_t* get_max_input_long_press_key(const int code)
{
	RK_input_long_press_t *events;
	RK_input_long_press_key_t *event, *target;
	uint32_t max_time = 0;

	events = m_long_press_head;
	target = NULL;

	while (events) {
		if (events->key_code == code) {
			event = events->event;
			while (event) {
				if (max_time < event->time) {
					max_time = event->time;
					target = event;
				}
				event = event->next;
			}
			return target;
		}
		events = events->next;
	}

	return NULL;
}

static RK_input_long_press_key_t* get_input_long_press_key(const int key_code, const uint32_t time)
{
	RK_input_long_press_t *events;
	RK_input_long_press_key_t *event, *ret;

	events = m_long_press_head;
	ret = NULL;
	while (events) {
		if (events->key_code == key_code) {
			event = events->event;
			while (event) {
				if (time >= event->time) {
					ret = event;
				}
				event = event->next;
			}
		}
		events = events->next;
	}

	return ret;
}

static void input_timer_reset(RK_input_timer_t* input_timer)
{
	input_timer->timer = NULL;
	input_timer->key_code = 0;
	if (input_timer->keys) {
		free(input_timer->keys);
		input_timer->keys = NULL;
	}
	input_timer->cb_long_press = NULL;
	input_timer->cb_compose_press = NULL;
}

static void timer_cb(const int end)
{
	if (!end)
		return;

	RK_timer_stop(m_input_timer.timer);

	if (m_input_timer.keys && m_input_timer.cb_compose_press) {
		m_input_timer.cb_compose_press(m_input_timer.keys, m_input_timer.timer->timer_time);
	} else if (m_input_timer.key_code > 0 && m_input_timer.cb_long_press) {
		m_input_timer.cb_long_press(m_input_timer.key_code, m_input_timer.timer->timer_time);
	}
	input_timer_reset(&m_input_timer);
}

static uint64_t get_timestamp_ms(void)
{
	struct timeval ctime;
	gettimeofday(&ctime, NULL);

	return (1e+6 * (uint64_t)ctime.tv_sec + ctime.tv_usec) / 1000;
}

static void handle_input_event(const int code, const int value, RK_Timer_t *timer)
{
	static RK_input_pressed_t *event, *prev;
	static RK_input_compose_press_t comp;
	static RK_input_long_press_key_t *max_long;
	static int compose_state;

	if (value) {
		compose_state = 0;
		event = (RK_input_pressed_t*) malloc(sizeof(RK_input_pressed_t));
		event->key_code = code;
		event->next = m_key_pressed_head;
		m_key_pressed_head = event;

		if (check_compose_press_event(&comp)) {
			if (comp.keys) {
				compose_state = 2;
				if (comp.cb)
					comp.cb(comp.keys, comp.time);

				free(comp.keys);
				memset(&comp, 0, sizeof(RK_input_compose_press_t));

				return;
			} else {
				compose_state = 1;
			}
		}

		if (max_long = get_max_input_long_press_key(code)) {
			RK_timer_create(timer, timer_cb, max_long->time, 0);
			RK_timer_start(timer);
			m_input_timer.timer = timer;
			m_input_timer.key_code = code;
			m_input_timer.cb_long_press = max_long->cb;

			return;
		}

		if (m_input_press_cb)
			m_input_press_cb(code);
	} else {
		event = prev = m_key_pressed_head;
		while (event) {
			if (event->key_code == code) {
				if (event == prev) {// head
					m_key_pressed_head = event->next;
				} else {// not head
					prev->next = event->next;
				}
				free(event);
				break;
			}
			prev = event;
			event = event->next;
		}

		if (compose_state == 2)
			return;

		// check whether has long key
		if (max_long = get_max_input_long_press_key(code)) {
			if (m_input_timer.timer && m_input_timer.key_code > 0 && !m_input_timer.keys) {
				uint32_t time = get_timestamp_ms() - m_input_timer.timer->timer_start;
				RK_timer_stop(m_input_timer.timer);
				input_timer_reset(&m_input_timer);

				RK_input_long_press_key_t *long_press = get_input_long_press_key(code, time);
				if (long_press) {
					if (long_press->cb)
						long_press->cb(long_press->key_code, long_press->time);
				} else {
					if (m_input_press_cb)
						m_input_press_cb(code);
				}
			}
		}
	}
}

static void* thread_key_monitor(void *arg)
{
	int max_fd;
	fd_set rfds;

	max_fd = (m_event0 > m_event1 ? m_event0 : m_event1);
	max_fd = (max_fd > m_event2 ? max_fd : m_event2);
	max_fd = max_fd + 1;

	RK_timer_init();
	RK_Timer_t timer;

	int ret;
	struct input_event ev_key;
	while (1) {
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

		if (ret == sizeof(ev_key) && ev_key.code != 0) {
			if(m_cb != NULL) {
				m_cb(ev_key.code, ev_key.value);
			}
		}

		handle_input_event(ev_key.code, ev_key.value, &timer);
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

int RK_input_register_press_callback(RK_input_press_callback cb)
{
	m_input_press_cb = cb;
	return 0;
}

int RK_input_register_long_press_callback(RK_input_long_press_callback cb, const uint32_t time, const int key_code)
{
	RK_input_long_press_t *events = m_long_press_head;
	RK_input_long_press_key_t *event = NULL;

	while (events) {
		if (events->key_code == key_code) {
			break;
		}
		events = events->next;
	}

	if (events) {
		event = events->event;
		while (event) {
			if (event->time == time) {
				break;
			}
			event = event->next;
		}

		if (event) {// already registered, ignore

		} else {
			event = (RK_input_long_press_key_t*) malloc(sizeof(RK_input_long_press_key_t));
			event->key_code = key_code;
			event->time = time;
			event->cb = cb;

			event->next = events->event;
			events->event = event;
        }
	} else {
		events = (RK_input_long_press_t*) malloc (sizeof(RK_input_long_press_t));
		event = (RK_input_long_press_key_t*) malloc(sizeof(RK_input_long_press_key_t));
		event->key_code = key_code;
		event->time = time;
		event->cb = cb;

		event->next = events->event;
		events->event = event;
		events->key_code = key_code;

		events->next = m_long_press_head;
		m_long_press_head = events;
	}

	return 0;
}

int RK_input_register_compose_press_callback(RK_input_compose_press_callback cb, const uint32_t time, const int key_code, ...)
{
	char* keys;
	char strKey[64];
	char tmp[8];
	va_list keys_ptr;
	int i, count, key;
	RK_input_compose_press_t *comp;

	memset(strKey, 0, sizeof(strKey));
	count = key_code;
	va_start(keys_ptr, key_code);
	for (i = 0; i < count; i++) {
		key = va_arg(keys_ptr, int);
		memset(tmp, 0, sizeof(tmp));

		snprintf(tmp, sizeof(tmp), "%d ", key);
		strcat(strKey, tmp);
	}
	keys = (char*) malloc(sizeof(char) * (strlen(strKey) + 1));
	strcpy(keys, strKey);

	comp = (RK_input_compose_press_t*) malloc(sizeof(RK_input_compose_press_t));
	comp->cb = cb;
	comp->keys = keys;
	comp->time = time;

	comp->next = m_compose_press_head;
	m_compose_press_head = comp;

	return 0;
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
