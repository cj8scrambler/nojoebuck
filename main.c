#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define DEFAULT_DELAY      2500  /* ms */
#define MAX_DELAY_S        120   /* seconds */
#define IN_INTERFACE       "hw:1"
#define OUT_INTERFACE      "hw:1"

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

typedef struct global_data {
    unsigned int delay_ms;       /* delay in ms */
    unsigned int delay_periods;  /* delay in periods */

    unsigned int period_time;    /* length of a period in us */

    unsigned int play_period, capture_period;
    unsigned int play_rate, cap_rate;

    unsigned int num_periods;    /* # of periods in buffer */
    uint8_t *buffer;
} globals;

void update_delay(globals *config, unsigned int delay_ms) {
    if (config) {
        config->delay_ms = delay_ms;
        config->delay_periods = (delay_ms * 1000) / config->period_time;
        printf ("Updated delay to %.1f sec (%d periods)\n",
                config->delay_ms / 1000.0, config->delay_periods);
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


int main(int argc, char* argv[])
{
    globals config;
    int err;
    unsigned int count;
    unsigned long size;
    snd_pcm_t *cap_hndl;
    snd_pcm_t *play_hndl;

    if ((err = snd_pcm_open(&cap_hndl, IN_INTERFACE,
                             SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                 IN_INTERFACE, snd_strerror(err));
        goto done;
    }
    if ((config.cap_rate = configure_stream(&config, cap_hndl, RATE)) < 0) {
        goto close_cap;
    }
    fprintf(stdout, "audio capture initialized at %.1fKHz\n", config.cap_rate / 1000.0);

    if ((err = snd_pcm_open(&play_hndl, OUT_INTERFACE,
                             SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                 OUT_INTERFACE, snd_strerror(err));
        goto close_cap;
    }
    if ((config.play_rate = configure_stream(&config, play_hndl, config.cap_rate)) < 0) {
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
    fprintf(stdout, "Initialized:\n");
    fprintf(stdout, "  Period: %.1f ms  (%d bytes)\n", config.period_time/1000.0, BYTES_PER_PERIOD); 
    fprintf(stdout, "  Num Periods: %d\n", config.num_periods);
    fprintf(stdout, "  Buffer size: %.1f KB\n", size/1000.0);

    config.play_period = 0;
    config.capture_period = 0;

    while (1) {
        /* capture a period */
        if (++(config.capture_period) == config.num_periods)
	{
            fprintf(stderr, "Write wraparound\n");
            config.capture_period = 0;
        }
	if (config.capture_period == config.play_period)
	{
	    if (++(config.play_period) == config.num_periods)
            {
                config.play_period = 0;
            }
            fprintf(stderr, "Warning: buffer overflow!\n");
        }

        if ((err = snd_pcm_readi(cap_hndl, CAPTURE_PTR(&config), FRAMES_PER_PERIOD)) != FRAMES_PER_PERIOD) {
            fprintf (stderr, "read from audio interface failed (%s)\n", snd_strerror (err));
            goto free;
        }
//printf("C: %d/%d\n", playback_delta(&config), config.delay_periods);
//fflush(stdout);

        /* if play buffer is more than DELAY behind record buffer then play */
        if (playback_delta(&config) >= config.delay_periods) {
            /* TODO: if delta > delay_periods: increase playback speed to catch up */
            if ((count = snd_pcm_writei(play_hndl, PLAY_PTR(&config), FRAMES_PER_PERIOD)) == -EPIPE) {
                printf("buffer underrun.\n");
                snd_pcm_prepare(play_hndl);
            } else if (count < 0) {
                printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(count));
                goto free;
            } else {
//printf("P\n");
//fflush(stdout);
                if (count != FRAMES_PER_PERIOD) {
                    printf("only wrote %d/%d frames of the block\n", count, FRAMES_PER_PERIOD);
                }
            }

            if (++(config.play_period) == config.num_periods)
            {
                config.play_period = 0;
            }
        }
    }
free:
    free(config.buffer);
close_play:
    snd_pcm_close (play_hndl);
close_cap:
    snd_pcm_close (cap_hndl);
done:
    printf("Shutting Down\n");
}
