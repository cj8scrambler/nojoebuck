#define ALSA_PCM_NEW_HW_PARAMS_API

#include <stdio.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

#include "audio.h"
#include "n2.h"

#define HYSTERESIS  2  /* number of periods still considered in sync */
#define PERIODS_IN_ALSABUF  10  /* Number of periods to keep in the ALSA buffer */
#define CAPTURE_PTR(x) (((x)->buffer) + ((x)->cap * (x)->period_bytes))
#define PLAY_PTR(x) (((x)->buffer) + ((x)->play * (x)->period_bytes))

/* get actual delta in periods */
static unsigned int get_actual_delta(buffer_config_t *bc)
{
  /* Count the periods in the ALSA playback buffer */
  unsigned int delta = bc->num_periods -  snd_pcm_avail(bc->play_hndl) / bc->period_frames;
  if (!bc) {
    fprintf(stderr, "%s(): Invalid call\n", __func__);  
    return 0;
  }

  pthread_mutex_lock(&bc->lock);

  if (bc->cap >= bc->play) {
    delta +=  bc->cap - bc->play;
  } else {
    delta += (bc->num_periods - bc->play) + bc->cap;
  }
  pthread_mutex_unlock(&bc->lock);

  return delta;
}

void advance_ptr(buffer_config_t *bc, unsigned int *ptr) {
  if (!bc || !ptr)
    return;

  if (++(*ptr) == bc->num_periods) {
    *ptr = 0;
  }

//  if (bc->cap == bc->play) {
//    if (++(bc->play) == bc->num_periods) {
//      bc->play = 0;
//    }
//    fprintf(stderr, "Warning: buffer overflow!\n");
//  }
}

void advance_play_ptr(buffer_config_t *bc) {
  advance_ptr(bc, &(bc->play));
}

void advance_cap_ptr(buffer_config_t *bc) {
  return advance_ptr(bc, &(bc->cap));
}

int write_playback_period(buffer_config_t *bc) {
  int err = snd_pcm_writei(bc->play_hndl, PLAY_PTR(bc), bc->period_frames);

  if (err == -EPIPE) {
    printf("Warning: playback buffer underrun.\n");
    snd_pcm_prepare(bc->play_hndl);
    err = 0;
  } else if (err < 0) {
    fprintf (stderr, "Write to audio interface failed (%s)\n", snd_strerror (err));
    bc->state = STOP;
  } else if (err != bc->period_frames) {
    fprintf(stderr, "Warning: only wrote %d/%ld frames\n", err, bc->period_frames);
    err = 0;
  } else {
    err = 0;
  }

  return err;
}

void *audio_io_thread(void *ptr) {
  int err;
  static bool alternate = 0;
  buffer_config_t *bc = (buffer_config_t *)ptr;
  int actual_delta;
  unsigned int diff, period;

  while (bc->state) {
    actual_delta = get_actual_delta(bc);
    diff = abs(actual_delta - bc->target_delta_p);

    /* Blocking read from capture interface (provies throttle to while loop */
    if ((err = snd_pcm_readi(bc->cap_hndl, CAPTURE_PTR(bc), bc->period_frames))
        != bc->period_frames) {
      fprintf (stderr, "Read from audio interface failed (%s)\n", snd_strerror (err));
      bc->state = STOP;
      goto done;
    }
    advance_cap_ptr(bc);

    /* Loop from: # of periods currently in the ALSA playback buffer 
     * to PERIODS_IN_ALSABUF
     */
    for (period =  bc->num_periods -  snd_pcm_avail(bc->play_hndl) / bc->period_frames;
         period < PERIODS_IN_ALSABUF; period++) {

      /* Give up if we're out of frames to send */
      if (bc->play == bc->cap) {
        break;
      }

      /* Write one period to playback */
      if (write_playback_period(bc) != 0) {
        bc->state = STOP;
        goto done;
      } 

      /* advancing of playbck ptr depends on state */
      if (diff <= HYSTERESIS) {
        /* advance 1 for each frame played in PLAY mode */
        bc->state = PLAY;
        advance_play_ptr(bc);
      } else if (actual_delta  < bc->target_delta_p) {
        /* advance 1/2 for each frame played in BUFFER mode */
        bc->state = BUFFER;
        if (alternate) {
          advance_play_ptr(bc);
        }
      } else {
        /* advance 2 for each frame played in BUFFER mode */
        bc->state = DOUBLE;
        advance_play_ptr(bc);
        advance_play_ptr(bc);
      }
      alternate = !alternate;
    }

    printf("STATE: %s  CAP: %d  PLAY: %d  DELTA: %d/%d  ALSABUF: %ld/%d\n",
           STATE_NAME(bc->state),
           bc->cap, bc->play, actual_delta, bc->target_delta_p,
           bc->num_periods -  snd_pcm_avail(bc->play_hndl) / bc->period_frames,
	   PERIODS_IN_ALSABUF);
  }

done:
  return NULL;
}


/*
 * return 0 on success; negative on error
 */
int configure_stream(snd_pcm_t *handle, int format, unsigned int rate,
                     unsigned int *actual_rate, unsigned int *period_us,
                     snd_pcm_uframes_t *period_frames, unsigned int *num_periods) {
  int dir, err = -1;
  snd_pcm_hw_params_t *hw_params;

  if (!handle || !actual_rate || !period_us || !num_periods) {
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

  if ((err = snd_pcm_hw_params_set_access(handle, hw_params,
                                          SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    fprintf(stderr, "cannot set access type (%s)\n", snd_strerror(err));
    goto exit;
  }

  if ((err = snd_pcm_hw_params_set_format(handle, hw_params, format)) < 0) {
    fprintf(stderr, "cannot set sample format (%s)\n",
            snd_strerror(err));
    goto exit;
  }

  if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rate, 0)) < 0) {
    fprintf(stderr, "cannot set sample rate (%s)\n", snd_strerror(err));
    goto exit;
  }
  *actual_rate = rate;

  if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, 2)) < 0) {
    fprintf(stderr, "cannot set channel count (%s)\n", snd_strerror(err));
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

  snd_pcm_hw_params_get_period_time(hw_params, period_us, &dir);
  snd_pcm_hw_params_get_period_size(hw_params, period_frames, &dir);
  snd_pcm_hw_params_get_periods(hw_params, num_periods, &dir);

exit:
  snd_pcm_hw_params_free (hw_params);
  return err;
}
