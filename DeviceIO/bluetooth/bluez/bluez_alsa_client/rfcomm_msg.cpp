#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <alsa/asoundlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "rfcomm_msg.h"

RK_BT_HFP_CALLBACK g_rfcomm_hfp_cb;
static int g_hfp_sockfd;

void *thread_get_ba_msg(void *arg)
{
	int ret = 0;
	char buff[100] = {0};
	struct sockaddr_un clientAddr;
	struct sockaddr_un serverAddr;
	socklen_t addr_len;

	g_hfp_sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (g_hfp_sockfd < 0) {
		printf("Create socket failed!\n");
		return NULL;
	}

	serverAddr.sun_family = AF_UNIX;
	strcpy(serverAddr.sun_path, "/tmp/rk_deviceio_rfcomm_status");

	system("rm -rf /tmp/rk_deviceio_rfcomm_status");
	ret = bind(g_hfp_sockfd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
	if (ret < 0) {
		printf("Bind Local addr failed!\n");
		return NULL;
	}

	while(1) {
		memset(buff, 0, sizeof(buff));
		ret = recvfrom(g_hfp_sockfd, buff, sizeof(buff), 0, (struct sockaddr *)&clientAddr, &addr_len);
		if (ret <= 0) {
			if (ret == 0)
				printf("###### FUCN:%s. socket closed!\n", __func__);
			break;
		}
		printf("###### FUCN:%s. Received a malformed message(%s)\n", __func__, buff);

		if (strstr(buff, "rfcomm status:hfp_hf_ring;")) {
			if (g_rfcomm_hfp_cb)
				g_rfcomm_hfp_cb(RK_BT_HFP_RING_EVT);
		} else if (strstr(buff, "rfcomm status:hfp_slc_connected;")) {
			if (g_rfcomm_hfp_cb)
				g_rfcomm_hfp_cb(RK_BT_HFP_CONNECT_EVT);
		} else if (strstr(buff, "rfcomm status:hfp_slc_disconnected;")) {
			if (g_rfcomm_hfp_cb)
				g_rfcomm_hfp_cb(RK_BT_HFP_DISCONNECT_EVT);
		} else if (strstr(buff, "rfcomm status:hfp_hf_connected;")) {
			if (g_rfcomm_hfp_cb)
				g_rfcomm_hfp_cb(RK_BT_HFP_AUDIO_OPEN_EVT);
		} else if (strstr(buff, "rfcomm status:hfp_hf_disconnected;")) {
			rfcomm_hfp_close_audio_path();
			if (g_rfcomm_hfp_cb)
			    g_rfcomm_hfp_cb(RK_BT_HFP_AUDIO_CLOSE_EVT);
		} else {
			printf("FUCN:%s. Received a malformed message(%s)\n", __func__, buff);
		}
	}

	if (g_hfp_sockfd > 0) {
		close(g_hfp_sockfd);
		g_hfp_sockfd = 0;
	}

	printf("###### FUCN:%s exit!\n", __func__);
	return NULL;
}

int rfcomm_listen_ba_msg_start()
{
	pthread_t tid;

	/* Create a thread to listen for Bluezalsa hfp-hf status. */
	pthread_create(&tid, NULL, thread_get_ba_msg, NULL);

	return 0;
}

int rfcomm_listen_ba_msg_stop()
{
	if (g_hfp_sockfd > 0) {
		close(g_hfp_sockfd);
		g_hfp_sockfd = 0;
	}
	/* wait for  thread_get_ba_msg exit */
	usleep(200);
	return 0;
}

void rfcomm_hfp_hf_regist_cb(RK_BT_HFP_CALLBACK cb)
{
	g_rfcomm_hfp_cb = cb;
}

/***********************************************************
 * Audio path config api
 ***********************************************************/

static int g_audio_path_valid_flag = 0;
static snd_pcm_t *local_play_pcm, *local_record_pcm;
static snd_pcm_t *bt_play_pcm, *bt_record_pcm;
pthread_t g_playback_tid, g_capture_tid;

typedef struct _setup_pcm_param {
	unsigned char channel;
	unsigned int samplerate;
	unsigned int buffer_time;
	unsigned int period_time;
	snd_pcm_uframes_t start_threshold;
	snd_pcm_format_t format;
} setup_pcm_param;

static int set_hw_params(snd_pcm_t *pcm, setup_pcm_param *param, char **msg)
{
	const snd_pcm_access_t access = SND_PCM_ACCESS_RW_INTERLEAVED;
	snd_pcm_format_t format;
	snd_pcm_hw_params_t *params;
	int channels, rate;
	unsigned int *buffer_time;
	unsigned int *period_time;
	char buf[256];
	int dir;
	int err;

	channels = param->channel;
	rate = param->samplerate;
	buffer_time = &(param->buffer_time);
	period_time = &(param->period_time);
	format = param->format;

	snd_pcm_hw_params_alloca(&params);

	if ((err = snd_pcm_hw_params_any(pcm, params)) != 0) {
		snprintf(buf, sizeof(buf), "Set all possible ranges: %s", snd_strerror(err));
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_access(pcm, params, access)) != 0) {
		snprintf(buf, sizeof(buf), "Set assess type: %s: %s", snd_strerror(err), snd_pcm_access_name(access));
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_format(pcm, params, format)) != 0) {
		snprintf(buf, sizeof(buf), "Set format: %s: %s", snd_strerror(err), snd_pcm_format_name(format));
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_channels(pcm, params, channels)) != 0) {
		snprintf(buf, sizeof(buf), "Set channels: %s: %d", snd_strerror(err), channels);
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_rate(pcm, params, rate, 0)) != 0) {
		snprintf(buf, sizeof(buf), "Set sampling rate: %s: %d", snd_strerror(err), rate);
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_buffer_time_near(pcm, params, buffer_time, &dir)) != 0) {
		snprintf(buf, sizeof(buf), "Set buffer time: %s: %u", snd_strerror(err), *buffer_time);
		goto fail;
	}
	if ((err = snd_pcm_hw_params_set_period_time_near(pcm, params, period_time, &dir)) != 0) {
		snprintf(buf, sizeof(buf), "Set period time: %s: %u", snd_strerror(err), *period_time);
		goto fail;
	}
	if ((err = snd_pcm_hw_params(pcm, params)) != 0) {
		snprintf(buf, sizeof(buf), "%s", snd_strerror(err));
		goto fail;
	}

	return 0;

fail:
	if (msg != NULL)
		*msg = strdup(buf);
	return err;
}

static int set_sw_params(snd_pcm_t *pcm, snd_pcm_uframes_t buffer_size,
		snd_pcm_uframes_t period_size, snd_pcm_uframes_t start_threshold, char **msg)
{
	snd_pcm_sw_params_t *params;
	snd_pcm_uframes_t threshold;
	char buf[256];
	int err;

	snd_pcm_sw_params_alloca(&params);

	if ((err = snd_pcm_sw_params_current(pcm, params)) != 0) {
		snprintf(buf, sizeof(buf), "Get current params: %s", snd_strerror(err));
		goto fail;
	}

	/* start the transfer when the buffer is full (or almost full) */
	if (start_threshold == 0)
		threshold = (buffer_size / period_size) * period_size;
	else
		threshold = start_threshold;
	if ((err = snd_pcm_sw_params_set_start_threshold(pcm, params, threshold)) != 0) {
		snprintf(buf, sizeof(buf), "Set start threshold: %s: %lu", snd_strerror(err), threshold);
		goto fail;
	}

	/* allow the transfer when at least period_size samples can be processed */
	if ((err = snd_pcm_sw_params_set_avail_min(pcm, params, period_size)) != 0) {
		snprintf(buf, sizeof(buf), "Set avail min: %s: %lu", snd_strerror(err), period_size);
		goto fail;
	}

	if ((err = snd_pcm_sw_params(pcm, params)) != 0) {
		snprintf(buf, sizeof(buf), "%s", snd_strerror(err));
		goto fail;
	}

	return 0;

fail:
	if (msg != NULL)
		*msg = strdup(buf);
	return err;
}

static int setup_pcm_handle(char *name, snd_pcm_t *pcm, setup_pcm_param *param)
{
	char *msg = NULL;
	int err = 0;
	snd_pcm_uframes_t buffer_size, period_size;

	if ((err = set_hw_params(pcm, param, &msg)) != 0) {
		printf("Couldn't set %s HW parameters: %s", name, msg);
		goto dofail;
	}

	if ((err = snd_pcm_get_params(pcm, &buffer_size, &period_size)) != 0) {
		printf("Couldn't get %s PCM parameters: %s", name, snd_strerror(err));
		goto dofail;
	}

	printf("Used configuration for %s:\n"
			"  PCM buffer time: %u us (%zu bytes)\n"
			"  PCM period time: %u us (%zu bytes)\n"
			"  Sampling rate: %u Hz\n"
			"  Channels: %u\n"
			"  StartThreshold: %u\n",
			name,
			param->buffer_time, snd_pcm_frames_to_bytes(pcm, buffer_size),
			param->period_time, snd_pcm_frames_to_bytes(pcm, period_size),
			param->samplerate, param->channel, param->start_threshold);

	if ((err = set_sw_params(pcm, buffer_size, period_size, param->start_threshold, &msg)) != 0) {
		printf("Couldn't set SW parameters: %s", msg);
		goto dofail;
	}

	if ((err = snd_pcm_prepare(pcm)) != 0) {
		printf("Couldn't prepare PCM: %s", snd_strerror(err));
		goto dofail;
	}

	return 0;

dofail:
	if (msg)
		free(msg);
	return -1;
}

static void *thread_record_start(void *arg)
{
	snd_pcm_uframes_t buffer_size = 0, period_size = 0;
	int period_bytes = 0;
	char *buffer = NULL;
	int ret = 0;

	snd_pcm_get_params(bt_play_pcm, &buffer_size, &period_size);
	period_bytes = snd_pcm_frames_to_bytes(bt_play_pcm, buffer_size);
	buffer = (char *)malloc(period_bytes);
	if (!buffer) {
		printf("ERROR:%s no space left!\n", __func__);
		return NULL;
	}
	memset(buffer, 0, period_size);

	printf("#%s start... period_bytes = %d\n", __func__, period_bytes);

	while (g_audio_path_valid_flag) {
		memset(buffer, 0, period_bytes);
		ret = snd_pcm_readi(local_record_pcm, buffer , period_size);
		if (ret < 0) {
			if (ret == -EPIPE) {
				printf( "Xrun occurred: LocalCaputure\n");
				snd_pcm_prepare(local_record_pcm);
			} else {
				printf("ERROR: %s read frame error=%d\n", __func__, ret);
				return NULL;
			}
		}

		ret = snd_pcm_writei(bt_play_pcm, buffer, period_size);
		if (ret < 0) {
			if (ret == -EPIPE) {
				printf( "Xrun occurred: BtPlayback\n");
				snd_pcm_prepare(bt_play_pcm);
			} else {
				printf("ERROR: %s write frame error=%d\n", __func__, ret);
				return NULL;
			}
		}
	}

	printf("#%s end... \n", __func__);

	snd_pcm_close(local_record_pcm);
	snd_pcm_close(bt_play_pcm);
	local_record_pcm = NULL;
	bt_play_pcm = NULL;
	free(buffer);

	return NULL;
}

static void *thread_playback_start(void *arg)
{
	snd_pcm_uframes_t buffer_size = 0, period_size = 0;
	int period_bytes = 0;
	char *buffer = NULL;
	int ret = 0;

	snd_pcm_get_params(local_play_pcm, &buffer_size, &period_size);
	period_bytes = snd_pcm_frames_to_bytes(local_play_pcm, buffer_size);
	buffer = (char *)malloc(period_bytes);
	if (!buffer) {
		printf("ERROR:%s no space left!\n", __func__);
		return NULL;
	}
	memset(buffer, 0, period_size);

	printf("#%s start... period_bytes = %d\n", __func__, period_bytes);

	while (g_audio_path_valid_flag) {
		memset(buffer, 0, period_bytes);
		ret = snd_pcm_readi(bt_record_pcm, buffer , period_size);
		if (ret < 0) {
			if (ret == -EPIPE) {
				printf( "Xrun occurred: BtCaputure\n");
				snd_pcm_prepare(bt_record_pcm);
			} else {
				printf("ERROR: %s read frame error=%d\n", __func__, ret);
				return NULL;
			}
		}

		ret = snd_pcm_writei(local_play_pcm, buffer, period_size);
		if (ret < 0) {
			if (ret == -EPIPE) {
				printf( "Xrun occurred: LocalPlayback\n");
				snd_pcm_prepare(local_play_pcm);
			} else {
				printf("ERROR: %s write frame error=%d\n", __func__, ret);
				return NULL;
			}
		}
	}

	printf("#%s end...\n", __func__);

	snd_pcm_close(local_play_pcm);
	snd_pcm_close(bt_record_pcm);
	local_play_pcm = NULL;
	bt_record_pcm = NULL;
	free(buffer);

	return NULL;
}

int rfcomm_hfp_open_audio_path()
{
	int err;
	setup_pcm_param pcm_param;

	if (g_audio_path_valid_flag) {
		printf("WARNING: Hfp audio path has already be opened!\n");
		return 0;
	}

	/* Open and setup LocalPlayback audio handle */
	if ((err = snd_pcm_open(&local_play_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0)) != 0) {
		printf("Couldn't open local playback PCM: %s", snd_strerror(err));
		goto fail;
	}
	pcm_param.channel = 2;
	pcm_param.samplerate = 8000;
	pcm_param.buffer_time = 500000;
	pcm_param.period_time = 100000;
	pcm_param.format = SND_PCM_FORMAT_S16_LE;
	pcm_param.start_threshold = 0; //default
	if (err = setup_pcm_handle("LocalPlayback", local_play_pcm, &pcm_param) != 0) {
		printf("Set up Local Playback audio path failed!\n");
		goto fail;
	}

	/* Open and setup LocalCaputure audio handle */
	if ((err = snd_pcm_open(&local_record_pcm, "2mic_loopback", SND_PCM_STREAM_CAPTURE, 0)) != 0) {
		printf("Couldn't open local capture PCM: %s", snd_strerror(err));
		goto fail;
	}
	pcm_param.channel = 2;
	pcm_param.samplerate = 8000;
	pcm_param.buffer_time = 100000;
	pcm_param.period_time = 20000;
	pcm_param.format = SND_PCM_FORMAT_S16_LE;
	pcm_param.start_threshold = 1;
	if (err = setup_pcm_handle("LocalCaputure", local_record_pcm, &pcm_param) != 0) {
		printf("Set up Local Caputure audio path failed!\n");
		goto fail;
	}

	/* Open and setup BtPlayback audio handle */
	if ((err = snd_pcm_open(&bt_play_pcm, "hw:1,0", SND_PCM_STREAM_PLAYBACK, 0)) != 0) {
		printf("Couldn't open bt playback PCM: %s", snd_strerror(err));
		goto fail;
	}
	pcm_param.channel = 2;
	pcm_param.samplerate = 8000;
	pcm_param.buffer_time = 100000;
	pcm_param.period_time = 20000;
	pcm_param.format = SND_PCM_FORMAT_S16_LE;
	pcm_param.start_threshold = 0;
	if (err = setup_pcm_handle("BtPlayback", bt_play_pcm, &pcm_param) != 0) {
		printf("Set up Bt Playback audio path failed!\n");
		goto fail;
	}

	/* Open and setup BtCaputure audio handle */
	if ((err = snd_pcm_open(&bt_record_pcm, "hw:1,0", SND_PCM_STREAM_CAPTURE, 0)) != 0) {
		printf("Couldn't open bt capture PCM: %s", snd_strerror(err));
		goto fail;
	}
	pcm_param.channel = 2;
	pcm_param.samplerate = 8000;
	pcm_param.buffer_time = 100000;
	pcm_param.period_time = 20000;
	pcm_param.format = SND_PCM_FORMAT_S16_LE;
	pcm_param.start_threshold = 1;
	if (err = setup_pcm_handle("BtCaputure", bt_record_pcm, &pcm_param) != 0) {
		printf("Set up Bt Caputure audio path failed!\n");
		goto fail;
	}

	g_audio_path_valid_flag = 1;
	/* Create a thread to listen for Bluezalsa hfp-hf status. */
	pthread_create(&g_playback_tid, NULL, thread_playback_start, NULL);
	/* Create a thread to listen for Bluezalsa hfp-hf status. */
	pthread_create(&g_capture_tid, NULL, thread_record_start, NULL);

	return 0;

fail:
	if (local_play_pcm)
		snd_pcm_close(local_play_pcm);
	if (local_record_pcm)
		snd_pcm_close(local_record_pcm);
	if (bt_play_pcm)
		snd_pcm_close(bt_play_pcm);
	if (bt_record_pcm)
		snd_pcm_close(bt_record_pcm);

	local_play_pcm = NULL;
	local_record_pcm = NULL;
	bt_play_pcm = NULL;
	bt_record_pcm = NULL;

	g_audio_path_valid_flag = 0;

	return -1;
}

int rfcomm_hfp_close_audio_path()
{
	void *status;

	printf("#%s is called! flag=%d\n", __func__, g_audio_path_valid_flag);

	if (g_audio_path_valid_flag) {
		g_audio_path_valid_flag = 0;
		pthread_join(g_playback_tid, &status);
		pthread_join(g_capture_tid, &status);
	}

	return 0;
}
