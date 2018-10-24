#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <zmq.h>
#include <alsa/asoundlib.h>

//#define DEBUG            1     /* enable debugging prints */

#define DEFAULT_DELAY      2500  /* ms */
#define MAX_DELAY_S        120   /* seconds */
#define PLAYBACK_BUFFER    2     /* number of periods to keep in sound card playback buffer */
#define IN_INTERFACE       "hw:1"
#define OUT_INTERFACE      "hw:0"
#define IDLE_TIME          1000 /* uS in idle loop */

/*
 * UI Command control format
 *
 * NULL terminated ASCII string format: "[char]:[value]"
 *
 * Command       Client->Server                Server->Client
 * -------       --------------                --------------
 * "D:1540"      set delay to 1.54 seconds     report current delay of 1.54 seconds
 * "D:"          request current delay         N/A
 * "B:100"       N/A                           Buffer full (playing normal with delay)
 * "B:[0-99]"    N/A                           Buffer filling (playing at 1/2 speed to fill buffer)
 * "B:[101-199]" N/A                           Buffer emptying (playing at 2x speed to empty buffer)
 * "B:"          request current buffer status 
 */
#define UI_OUT             "ipc:///tmp/nojobuck_status"
#define UI_IN              "ipc:///tmp/nojobuck_cmd"
#define MAX_UI_CMD         16

/* Audio Params */
#define RATE               44100
#define FORMAT             SND_PCM_FORMAT_S16_LE
#define NUM_CHANS          2
#define ACCESS_TYPE        SND_PCM_ACCESS_RW_INTERLEAVED
#define BYTES_PER_FRAME    ((snd_pcm_format_width(FORMAT)/8) * NUM_CHANS)
#define FRAMES_PER_PERIOD  256
#define BYTES_PER_PERIOD   (FRAMES_PER_PERIOD * BYTES_PER_FRAME)

#define CAPTURE_PTR(x) (((x)->buffer) + ((x)->capture_period * BYTES_PER_PERIOD))
#define PLAY_PTR(x) (((x)->buffer) + ((x)->play_period * BYTES_PER_PERIOD))
#define EVEN_FRAME(x) (!((x)->capture_period % 2))

typedef enum playback_state {
    STOP = 0,
    BUFFER,
    PLAY,
    DOUBLE,
} playback_state;

typedef struct global_data {
    playback_state state;
    void *ui_out;
    void *ui_in;

    snd_pcm_t *cap_hndl;
    snd_pcm_t *play_hndl;

    pthread_t audio_thread;

    unsigned long delay_ms;      /* delay in ms */
    unsigned int delay_periods;  /* delay in periods */
    unsigned int last_delay_periods;/* Updated when in PLAY mode */

    unsigned int period_time;    /* length of a period in us */

    unsigned int play_rate, cap_rate;

    unsigned int num_periods;    /* # of periods in buffer */
    unsigned int play_period, capture_period; /* period index into buffer */
    uint8_t *buffer;
} globals;

void update_delay(globals *config, unsigned int delay_ms) {
    if (config) {
        config->delay_ms = delay_ms;
        config->delay_periods = (delay_ms * 1000) / config->period_time;
#ifdef DEBUG
        printf ("Updated delay to %.1f sec (%d periods)\n",
                config->delay_ms / 1000.0, config->delay_periods);
#endif
    }
}

/* number of periods behind playback is from capture */
unsigned int playback_delta(globals *config) {
    if (!config) {
        return 0;
    }
    if (config->capture_period >= config->play_period) {
        return (config->capture_period - config->play_period);
    } else {
        return ((config->num_periods - config->play_period) + config->capture_period);
    }
}

/*
 * return < 0 on err
 * return rate on success
 */
int configure_stream(globals *config, snd_pcm_t *handle, unsigned int rate) {
    unsigned long tmp;
    int dir, err = -1;
    snd_pcm_hw_params_t *hw_params;

    if (!config || !handle) {
        fprintf(stderr, "Invalid call to configure stream\n");
        goto exit;
    }

    if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0) {
        fprintf(stderr, "cannot allocate hardware parameter structure (%s)\n",
                 snd_strerror(err));
        goto exit;
    }

    if ((err = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
        fprintf(stderr, "cannot initialize hardware parameter structure (%s)\n",
                 snd_strerror(err));
        goto exit;
    }

    if ((err = snd_pcm_hw_params_set_access(handle,
				            hw_params,
					    ACCESS_TYPE)) < 0) {
        fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
        goto exit;
    }

    if ((err = snd_pcm_hw_params_set_format(handle, hw_params, FORMAT)) < 0) {
        fprintf(stderr, "cannot set sample format (%s)\n",
                snd_strerror(err));
       goto exit;
    }

    if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rate, 0)) < 0) {
        fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
        goto exit;
    }

    if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, NUM_CHANS)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        goto exit;
    }

    tmp = FRAMES_PER_PERIOD;
    if ((err = snd_pcm_hw_params_set_period_size_near(handle, hw_params, &tmp, &dir)) < 0) {
        fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
        goto exit;
    }
    if (tmp != FRAMES_PER_PERIOD) {
        fprintf (stderr, "Error: Could not set period size (%ld != %d)\n",
                 tmp, FRAMES_PER_PERIOD);
        goto exit;
    }

    if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
        goto exit;
    }
	
    if ((err = snd_pcm_prepare (handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                 snd_strerror (err));
        goto exit;
    }

    snd_pcm_hw_params_get_period_time(hw_params, &config->period_time, NULL);

    err = rate;
exit:
    snd_pcm_hw_params_free (hw_params);
    return err;
}

int send_frame(globals *config) {
    unsigned int count;
    int ret = 0;

#ifdef DEBUG
    printf("P %d: [%d/%d]  ", config->play_period, playback_delta(config), config->delay_periods);
#endif
    if ((count = snd_pcm_writei(config->play_hndl, PLAY_PTR(config), FRAMES_PER_PERIOD)) == -EPIPE) {
        printf("playback buffer underrun.\n");
        snd_pcm_prepare(config->play_hndl);
    } else if (count < 0) {
        printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(count));
	ret = -1;
    } else {
        if (count != FRAMES_PER_PERIOD) {
            printf("only wrote %d/%d frames of the block\n", count, FRAMES_PER_PERIOD);
        }
    }

    return ret;
}

void advance_play_ptr(globals *config)
{
    if (++(config->play_period) == config->num_periods)
    {
        config->play_period = 0;
    }
    /* if this causes a collision with capture ptr, then undo it */
    if (config->play_period == config->capture_period) {
        printf("avoid playback overrun\n");
        if (config->play_period == 0) {
            config->play_period = config->num_periods - 1;
	} else {
            config->play_period--;
        }
    }
}

int send_buffer_status(globals *config) {
    char buffer[MAX_UI_CMD+1];
    unsigned int delta;
    int ret;

    switch(config->state) {
    case PLAY:
        snprintf(buffer, MAX_UI_CMD, "B:100");
        break;
    case BUFFER:
        delta = playback_delta(config); 
        snprintf(buffer, MAX_UI_CMD, "B:%d", (int)(100 * ((delta / (float) config->delay_periods) + 0.005)));
        break;
    case DOUBLE:
        delta = playback_delta(config); 
        if (config->delay_periods > delta) {
            snprintf(buffer, MAX_UI_CMD, "B:100");
	} else {
            snprintf(buffer, MAX_UI_CMD, "B:%d",
                100 + (int)(100 * (((delta - config->delay_periods) / (float) (config->last_delay_periods - config->delay_periods)) + .005)));
        }
        break;
    default:
        fprintf(stderr, "Received B: in inalid state: %d\n", config->state);
        buffer[0] = '\0';
        break;
    }

#ifdef DEBUG
    printf("\nSending: %s:", buffer);
#endif
    if (0 != (ret = zmq_send(config->ui_out, buffer, strlen(buffer), ZMQ_NOBLOCK))) {
        if (errno == EAGAIN) {
            /* ignore missing client */
            ret = 0;
#ifdef DEBUG
            printf("  NO CLIENT\n");
#endif
        } else {
            fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
                    buffer, strerror(errno));
        }
    }
#ifdef DEBUG
    else printf("  SUCCESS\n");
#endif

    return ret;
}

void *audio_thread(void *ptr) {
    globals *config = ptr;
    unsigned int err, delta;

    while(config->state) {
        /* capture 1 period (blocking call provides thread timing) */
        if ((err = snd_pcm_readi(config->cap_hndl, CAPTURE_PTR(config), FRAMES_PER_PERIOD)) != FRAMES_PER_PERIOD) {
            fprintf (stderr, "read from audio interface failed (%s)\n", snd_strerror (err));
            config->state = STOP;
            return NULL;
        }
#ifdef DEBUG
        printf("\nC %d: [%d/%d]  ", config->capture_period, playback_delta(config), config->delay_periods);
        fflush(stdout);
#endif

        /* move capture pointer */
        if (++(config->capture_period) == config->num_periods)
	{
            config->capture_period = 0;
        }
	if (config->capture_period == config->play_period)
	{
	    if (++(config->play_period) == config->num_periods)
            {
                config->play_period = 0;
            }
            fprintf(stderr, "Warning: buffer overflow!\n");
        }

        /* play back 1 period (PLAY buffer fill maybe more than 1) */
        delta = playback_delta(config); 
        if (delta < config->delay_periods) {
            /* Need to pass PLAYBACK_BUFFER hysterises to move from PLAY->BUFFER */
            if ((config->state != PLAY) ||
                (delta < config->delay_periods - PLAYBACK_BUFFER)) {
                config->state = BUFFER;
	        config->last_delay_periods = config->delay_periods;
            }
	} else if (delta > (config->delay_periods)) {
            config->state = DOUBLE;
        } else {
            config->state = PLAY;
	    config->last_delay_periods = config->delay_periods;
	}

        switch (config->state) {
        case PLAY:
            /* Keep PLAYBACK_BUFFER periods in the playback queu */
            while (delta > (config->delay_periods - PLAYBACK_BUFFER)) {
                if (0 != send_frame(config)) {
                    goto exit;
                }
		advance_play_ptr(config);
                delta = playback_delta(config); 
            }
            break;
        case BUFFER:
            if (delta) {
                /*
		 * We want to stretch out playback here, so play 1 frame,
		 * but only increment the play ptr every other time
		 */
                if (0 != send_frame(config)) {
                    goto exit;
                }
		if (EVEN_FRAME(config)) {
                    advance_play_ptr(config);
                }
                send_buffer_status (config);
            }
            break;
        case DOUBLE:
            /*
             * We want speed up playback here, so play 1 frame, but
             * increment the play ptr twice
             */
            if (0 != send_frame(config)) {
                goto exit;
            }
            advance_play_ptr(config);
            advance_play_ptr(config);
            send_buffer_status (config);
            break;
        default:
            /* make the compiler happy */
            break;
        }
    }
exit:
    config->state = STOP;
    return NULL;
}

int main(int argc, char* argv[])
{
    globals config;
    int err;
    unsigned long size;
    void *zmq_context_in = zmq_ctx_new();
    void *zmq_context_out = zmq_ctx_new();
    char buffer[MAX_UI_CMD+1];

    memset(&config, 0, sizeof(config));

    if ((err = snd_pcm_open(&(config.cap_hndl), IN_INTERFACE,
                             SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                 IN_INTERFACE, snd_strerror(err));
        goto free_contexts;
    }
    if ((config.cap_rate = configure_stream(&config, config.cap_hndl, RATE)) < 0) {
        goto close_cap;
    }
    fprintf(stdout, "audio capture initialized at %.1fKHz\n", config.cap_rate / 1000.0);

    if ((err = snd_pcm_open(&(config.play_hndl), OUT_INTERFACE,
                             SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                 OUT_INTERFACE, snd_strerror(err));
        goto close_cap;
    }
    if ((config.play_rate = configure_stream(&config, config.play_hndl, config.cap_rate)) < 0) {
        goto close_play;
    }
    if (config.play_rate != config.cap_rate) {
        fprintf(stderr, "Error: Capture rate (%d) != Playback rate (%d)\n",
                config.cap_rate, config.play_rate); 
        goto close_play;
    }
    fprintf(stdout, "audio playback initialized at %.1fKHz\n", config.play_rate / 1000.0);

    update_delay(&config, DEFAULT_DELAY);

    config.num_periods = (MAX_DELAY_S * 1000000) / config.period_time;
    if ( (MAX_DELAY_S * 1000000) % config.period_time) {
        config.num_periods++;
    }
    size = config.num_periods * BYTES_PER_PERIOD;
    config.buffer = malloc(size);
    if (!config.buffer) {
        fprintf(stderr, "Could allocate buffer memory\n");
        return 1;
    }

    config.ui_out = zmq_socket (zmq_context_out, ZMQ_PUSH);
    if (0 != zmq_bind (config.ui_out, UI_OUT)) {
        fprintf(stderr, "Could not create outgoing zmq socket\n");
        goto free;
    }

    config.ui_in = zmq_socket (zmq_ctx_new(), ZMQ_PULL);
    if (0 != zmq_bind (config.ui_in, UI_IN)) {
        fprintf(stderr, "Could not create incoming zmq socket\n");
        goto close_out_socket;
    }

    if(pthread_create(&(config.audio_thread), NULL, audio_thread, &config)) {
        fprintf(stderr, "Could not create audio thread\n");
        goto close_in_socket;
    }

    fprintf(stdout, "Initialized:\n");
    fprintf(stdout, "  Period: %.1f ms  (%d bytes)\n", config.period_time/1000.0, BYTES_PER_PERIOD); 
    fprintf(stdout, "  Num Periods: %d\n", config.num_periods);
    fprintf(stdout, "  Buffer size: %.1f KB\n", size/1000.0);

    config.state = BUFFER;
    config.play_period = 0;
    config.capture_period = 0;

    while (config.state) {
	size = zmq_recv (config.ui_in, buffer, MAX_UI_CMD, 0);
        if (size < 0) {
            fprintf(stderr, "Error receiving zmq msg: %s\n", strerror(errno));
        } else {
            char *token;
	    buffer[size] = '\0';
	    token = strtok(buffer, ":");
	    if (token && !strcmp(token, "D")) {
	        token = strtok(NULL, ":");
		if (token) {
		    unsigned long val = strtol(token, NULL, 10);
                    if (val <= (MAX_DELAY_S * 1000)) {
                        update_delay(&config, val);
                    }
                } else {
		    /* no value: must be a query; respond with current delay */
                    snprintf(buffer, MAX_UI_CMD, "D:%ld", config.delay_ms);
#ifdef DEBUG
                    printf("Sending: %s\n", buffer);
#endif
                    if (0 != zmq_send (config.ui_out, buffer, strlen(buffer), 0)) {
                        fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
                                buffer, strerror(errno));
                    }
                }
            } else if (token && !strcmp(token, "B")) {
                /* Don't even look for a value; must be a query; respond with current buffer */
                send_buffer_status(&config);
            } else {
                    fprintf(stderr, "Received invalid UI command: %s\n", buffer);
            }
        }
    }

    pthread_cancel(config.audio_thread);
    pthread_join(config.audio_thread, NULL);
close_in_socket:
    zmq_close (config.ui_in);
close_out_socket:
    zmq_close (config.ui_out);
free:
    free(config.buffer);
close_play:
    snd_pcm_close(config.play_hndl);
close_cap:
    snd_pcm_close(config.cap_hndl);
free_contexts:
    zmq_ctx_destroy (zmq_context_out);
    zmq_ctx_destroy (zmq_context_in);
    printf("Shutting Down\n");
}
