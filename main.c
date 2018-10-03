#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#define DEFAULT_DELAY  2500  /* ms */
#define MAX_DELAY      5  /* seconds */
#define IN_INTERFACE   "hw:1"
#define OUT_INTERFACE  "hw:0"
#define FRAMES_PER_BLOCK     128

#define RATE           44100
#define FORMAT         SND_PCM_FORMAT_S16_LE
#define NUM_CHANS      2
#define ACCESS_TYPE    SND_PCM_ACCESS_RW_INTERLEAVED
#define IDLE_TIME      1000 /* uS in idle loop */

#define CAPTURE_PTR(x) ((x->buffer) + (x->capture_block * x->block_size))
#define PLAY_PTR(x) ((x->buffer) + (x->play_block * x->block_size))
#define DELAY_BLOCKS(x)  (((x) * FRAMES_PER_BLOCK * 1000) / RATE)

struct global_data {
    bool run;
    int delay_blocks;  /* delay in blocks */
    int delay_ms;      /* delay in ms */
    int blocks;        /* blocks in buffer */
    int block_size;
    int play_block, capture_block;
    int play_rate, cap_rate;
    char *buffer;
};

/*
 * return < 0 on err
 * return rate on success
 */
int configure_stream(snd_pcm_t *handle, unsigned int rate) {
    int err;
    unsigned int period;
    snd_pcm_uframes_t frames;
    snd_pcm_hw_params_t *hw_params;

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

    if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
        fprintf(stderr, "cannot set parameters (%s)\n", snd_strerror(err));
        goto exit;
    }
    snd_pcm_hw_params_free (hw_params);
	
    if ((err = snd_pcm_prepare (handle)) < 0) {
        fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
                 snd_strerror (err));
        goto exit;
    }

    snd_pcm_hw_params_get_period_size(hw_params, &frames, 0);
    snd_pcm_hw_params_get_period_time(hw_params, &period, NULL);
    printf("frames=%ld  buffer size: %ld  period: %d\n",
           frames, frames * NUM_CHANS * (snd_pcm_format_width(FORMAT) / 8), period);

    err = rate;
exit:
    return err;
}

void *audio_in(void *ptr) {
    struct global_data *data = ptr;
    int err;
    snd_pcm_t *cap_hndl;

    printf("Starting audio capture\n");

    if ((err = snd_pcm_open(&cap_hndl, IN_INTERFACE,
                             SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                 IN_INTERFACE, snd_strerror(err));
        goto done;
    }

    if ((data->cap_rate = configure_stream(cap_hndl, RATE)) < 0) {
        goto done;
    }

    fprintf(stdout, "audio capture initialized at %.1fKHz\n", data->cap_rate / 1000.0);

    while (data->run) {
        if (++(data->capture_block) == data->blocks)
	{
            fprintf(stderr, "Write wraparound\n");
            data->capture_block = 0;
        }
	if (data->capture_block == data->play_block)
	{
	    if (++(data->play_block) == data->blocks)
            {
                data->play_block = 0;
            }
            fprintf(stderr, "Warning: buffer overflow!\n");
        }

        if ((err = snd_pcm_readi(cap_hndl, CAPTURE_PTR(data), FRAMES_PER_BLOCK)) != FRAMES_PER_BLOCK) {
            fprintf (stderr, "read from audio interface failed (%s)\n", snd_strerror (err));
            goto close;
        }
    }

    printf ("Shutting down capture thread\n");
close:
    snd_pcm_close (cap_hndl);
done:
    data->run = false;
    return NULL;
}

void *audio_out(void *ptr) {
    struct global_data *data = ptr;
    int err;
    unsigned int count;
    snd_pcm_t *play_hndl;

    printf ("Starting playback thread\n");

    if ((err = snd_pcm_open(&play_hndl, OUT_INTERFACE, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "cannot open audio device %s (%s)\n",
                 IN_INTERFACE, snd_strerror(err));
        goto done;
    }

    /* wait for cap_rate to be  initialized */
    while (!data->cap_rate && data->run) {
        usleep(IDLE_TIME);
    }

    if ((data->play_rate = configure_stream(play_hndl, data->cap_rate)) < 0) {
        goto done;
    }

    if (data->play_rate != data->cap_rate) {
        fprintf(stderr, "Error: Capture rate (%d) != Playback rate (%d)\n",
                data->cap_rate, data->play_rate); 
        goto done;
    }

    fprintf(stdout, "audio playback initialized at %.1fKHz\n", data->play_rate / 1000.0);

    while (data->run) {
        /* if play buffer is more than DELAY seconds behind record buffer */
        if ((count = snd_pcm_writei(play_hndl, PLAY_PTR(data), FRAMES_PER_BLOCK)) == -EPIPE) {
            printf("buffer underrun.\n");
            snd_pcm_prepare(play_hndl);
        } else if (count < 0) {
            printf("ERROR. Can't write to PCM device. %s\n", snd_strerror(count));
        } else {
            if (count != FRAMES_PER_BLOCK) {
                printf("only wrote %d/%d frames of the block\n", count, FRAMES_PER_BLOCK);
            }

            if (++(data->play_block) == data->blocks)
            {
                data->play_block = 0;
            }
        }
    }
    printf ("Shutting down playback thread\n");

done:
    data->run = false;
    return NULL;
}

int main(int argc, char* argv[])
{
    struct global_data data;
    pthread_t input, output;
    long size;
    char c;

    data.run = true;
    data.delay_ms = DEFAULT_DELAY;
    data.delay_blocks = DELAY_BLOCKS(DEFAULT_DELAY);
    data.block_size = FRAMES_PER_BLOCK * (snd_pcm_format_width(FORMAT) / 8);

    size = MAX_DELAY * NUM_CHANS * RATE * (snd_pcm_format_width(FORMAT) / 8);
    if (size % data.block_size)
    {
        data.blocks = (size / data.block_size) + 1;
    }
    else
    {
        data.blocks = (size / data.block_size);
    }
    size = data.blocks * data.block_size;
    data.buffer = malloc(size);
    if (!data.buffer) {
        fprintf(stderr, "Could allocate buffer memory\n");
        return 1;
    }
    printf("malloc'ed %ld KB\n", size / 1024);

    data.play_block = 0;
    data.capture_block = 0;

    if(pthread_create(&input, NULL, audio_in, &data)) {
        fprintf(stderr, "Could not create audio input thread\n");
        return 1;
    }	  

    if(pthread_create(&output, NULL, audio_out, &data)) {
        fprintf(stderr, "Could not create audio output thread\n");
        return 1;
    }	  

    c = getchar();
    while (data.run && (c != 'q')) {
        if (c == 'k') {
            if (data.delay_ms < (MAX_DELAY * 1000 - 100)) {
                data.delay_ms += 100;
	    } else {
                data.delay_ms = MAX_DELAY * 1000;
	    }
	} else if (c == 'j') {
            if (data.delay_ms > 100) {
                data.delay_ms -= 100;
	    } else {
                data.delay_ms = 0;
	    }
	}
	data.delay_blocks = DELAY_BLOCKS(data.delay_ms);
	printf ("New delay: %2.1fS (%d blocks)\n", data.delay_ms / 1000.0, data.delay_blocks);
        scanf("%c", &c);
        c = getchar();
    }
    printf("Shutting Down\n");
    data.run = false;
    pthread_join(input, NULL);
    pthread_join(output, NULL);
    free(data.buffer);
}
