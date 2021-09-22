#define ALSA_PCM_NEW_HW_PARAMS_API

#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>

#include "audio.h"
#include "n2.h"

#define HYSTERESIS  1  /* number of periods still considered in sync */
#define PERIODS_IN_ALSABUF  5  /* Number of periods to keep in the ALSA buffer */
#define CAPTURE_PTR(x) (((x)->buffer) + ((x)->cap * (x)->period_bytes))
#define PLAY_PTR(x) (((x)->buffer) + ((x)->play * (x)->period_bytes))

static void advance_ptr(buffer_config_t *bc, unsigned int *ptr) {
  if (!bc || !ptr)
    return;

  if (++(*ptr) == bc->mem_num_periods) {
    *ptr = 0;
  }
}

static void advance_play_ptr(buffer_config_t *bc) {
  advance_ptr(bc, &(bc->play));
}

static void advance_cap_ptr(buffer_config_t *bc) {
  return advance_ptr(bc, &(bc->cap));
}

static int write_playback_period(buffer_config_t *bc) {
  int err;
  int frameno, byteno;
  uint8_t *audiodata = NULL;
  uint8_t *buf = NULL;
  int dataframes;

  switch (bc->state){
  case PLAY:
    dataframes = bc->period_frames;
    audiodata = PLAY_PTR(bc);
    break;
  case BUFFER:
    dataframes = 2 * bc->period_frames;
    buf = malloc(dataframes * bc->frame_bytes);
    audiodata = buf;
    if (!audiodata) {
      fprintf(stderr, "%s() Memory error\n", __func__);
      return -ENOMEM;
    }
    for (frameno = 0, byteno = 0; frameno < bc->period_frames; frameno++) {
      /* copy each frame twice */
      memcpy(&(audiodata[byteno]),
             PLAY_PTR(bc) + (frameno * bc->frame_bytes),
             bc->frame_bytes);
      byteno += bc->frame_bytes;
      memcpy(&(audiodata[byteno]),
             PLAY_PTR(bc) + (frameno * bc->frame_bytes),
             bc->frame_bytes);
      byteno += bc->frame_bytes;
    }
    break;
  case DOUBLE:
    dataframes = bc->period_frames / 2;
    buf = malloc(dataframes * bc->frame_bytes);
    audiodata = buf;
    if (!audiodata) {
      fprintf(stderr, "%s() Memory error\n", __func__);
      return -ENOMEM;
    }
    /* copy every other frame */
    for (frameno = 0, byteno = 0; frameno < bc->period_frames; ) {
      memcpy(&(audiodata[byteno]),
             PLAY_PTR(bc) + (frameno * bc->frame_bytes),
             bc->frame_bytes);
      frameno += 2;
      byteno += bc->frame_bytes;
    }
    break;
  default:
    fprintf(stderr, "%s() Unkown state: %d\n", __func__, bc->state);
    return -EINVAL;
  }

  err = snd_pcm_writei(bc->play_hndl, audiodata, dataframes);
  if (err == -EPIPE) {
    printf("Warning: playback buffer underrun.\n");
    snd_pcm_prepare(bc->play_hndl);
    err = 0;
  } else if (err < 0) {
    fprintf (stderr, "Write to audio interface failed (%s)\n", snd_strerror (err));
  } else if (err != dataframes) {
    fprintf(stderr, "Warning: only wrote %d/%ld frames\n", err, bc->period_frames);
    err = -1;
  } else {
    err = 0;
  }

  if (buf) {
    free(buf);
  }
  return err;
}

/* get actual delta in periods */
unsigned int get_actual_delta(buffer_config_t *bc)
{
  /* number of periods in ALSA playback buffer */
  unsigned int delta = bc->alsa_num_periods - snd_pcm_avail(bc->play_hndl) / bc->period_frames;
  if (!bc) {
    fprintf(stderr, "%s(): Invalid call\n", __func__);  
    return 0;
  }

  pthread_mutex_lock(&bc->lock);
  if (bc->cap >= bc->play) {
    delta +=  bc->cap - bc->play;
  } else {
    delta += (bc->mem_num_periods - bc->play) + bc->cap;
  }
  pthread_mutex_unlock(&bc->lock);

  return delta;
}

void *audio_io_thread(void *ptr) {
  int err;
  buffer_config_t *bc = (buffer_config_t *)ptr;
  int last_state = STOP;
  struct timeval initial_time, now_time;
  int actual_delta;
  unsigned int diff, period;

  gettimeofday(&initial_time, NULL);

  while (bc->state) {
    actual_delta = get_actual_delta(bc);
    diff = abs(actual_delta - bc->target_delta_p);

    /* Blocking read from capture interface (provies throttle to while loop) */
    if ((err = snd_pcm_readi(bc->cap_hndl, CAPTURE_PTR(bc), bc->period_frames))
        != bc->period_frames) {
      fprintf (stderr, "Read from audio interface failed (%s)\n", snd_strerror (err));
      continue;
    }

    advance_cap_ptr(bc);

    /* Loop from: # of periods currently in the ALSA playback buffer 
     * to PERIODS_IN_ALSABUF
     */
    for (period = bc->alsa_num_periods -  snd_pcm_avail(bc->play_hndl) / bc->period_frames;
         period < PERIODS_IN_ALSABUF; period++) {

      /* Give up if we're out of frames to send */
      if (bc->play == bc->cap) {
        break;
      }

      if (diff <= HYSTERESIS) {
        bc->state = PLAY;
      } else if (actual_delta < bc->target_delta_p) {
        bc->state = BUFFER;
      } else {
        bc->state = DOUBLE;
      }

      /*
       *  Write one period to playback interface either streched,
       *  normal or compressed based on state
       */
      if (write_playback_period(bc) != 0) {
        continue;
      } 
      advance_play_ptr(bc);
    }

    if ((last_state != bc->state) || bc->verbose) {
      long delta_us;
      gettimeofday(&now_time, NULL);
      delta_us = (now_time.tv_sec - initial_time.tv_sec) * 1000000 +
                 ((int)now_time.tv_usec - (int)initial_time.tv_usec);
      printf("%8.03f  STATE: %-8.8s CAP: %-4d  PLAY: %-4d  DELTA: %4d/%-4d  ALSABUF: %ld/%d\n",
             delta_us / 1000000.0, STATE_NAME(bc->state), bc->cap, bc->play, actual_delta, bc->target_delta_p,
             bc->alsa_num_periods -  snd_pcm_avail(bc->play_hndl) / bc->period_frames,
	     PERIODS_IN_ALSABUF);
    }
    last_state = bc->state;
  }

  return NULL;
}

int configure_stream(snd_pcm_t *handle, int format, unsigned int rate,
                     unsigned int *actual_rate, unsigned int *period_us,
                     snd_pcm_uframes_t *period_frames, unsigned int *alsa_num_periods) {
  int dir, err = -1;
  snd_pcm_hw_params_t *hw_params;

  if (!handle || !actual_rate || !period_us || !alsa_num_periods) {
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
  snd_pcm_hw_params_get_periods(hw_params, alsa_num_periods, &dir);

exit:
  snd_pcm_hw_params_free (hw_params);
  return err;
}
