#define ALSA_PCM_NEW_HW_PARAMS_API

#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>

#include "audio.h"
#include "n2.h"

//#define HYSTERESIS  1  /* number of periods still considered in sync */
#define HYSTERESIS  11  /* number of ms still considered in sync */
#define PERIODS_IN_ALSABUF  10  /* Number of periods to keep in the ALSA buffer */
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
  float src_frame;
  int dst_frame;
  float target_frame = 0;
  uint8_t *audiodata = NULL;
  uint8_t *buf = NULL;
  int dataframes;

  /*
   *  bc->state enums map to playback rates where PLAY is the denominator i.e.:
   *    BUFFER_1_8 / PLAY = 0.125  (playback at 12.5% speed)
   *    PURGE_32_8 / PLAY = 4      (playback at 400% speed)
   */
  float playback_rate = ((float) bc->state) / PLAY;

  /* rate at which frames should be skipped (used for speeding up playback */
  float frame_skip = (bc->state > PLAY) ? playback_rate : 1;

  /* rate at which frames should be duplicated (used for slowing down playback) */
  float frame_dup = (bc->state < PLAY) ? 1.0 / playback_rate : 1;

  if (bc->state == PLAY) {
    /* no copy needed for regular speed playback */
    dataframes = bc->period_frames;
    audiodata = PLAY_PTR(bc);
  } else {
    /* create a data buffer which is stretched/compressed based on state */
    dataframes = (int)((float) bc->period_frames * (1.0 / playback_rate) + 0.5);
    //printf("Playback rate: %.3f (%s)  frame_skip: %.2f  frame_dup: %.2f  "
    //       "frame_size: %ld (%ld bytes)  buffer_size: %d (%d bytes)\n",
    //       playback_rate, STATE_NAME(bc->state), frame_skip, frame_dup,
    //       bc->period_frames, bc->period_frames * bc->frame_bytes,
    //       dataframes, dataframes * bc->frame_bytes);
    buf = malloc(dataframes * bc->frame_bytes);
    audiodata = buf;
    if (!audiodata) {
      fprintf(stderr, "%s() Memory error\n", __func__);
      return -ENOMEM;
    }

    /* src frame is accumulated as a float, but cast to int when used as offset */
    for (src_frame = 0.0, dst_frame = 0; src_frame < bc->period_frames;) {
      target_frame += frame_dup;
      while (dst_frame < (int) target_frame) {
        //printf("  source frame %d (%.3f) -> dst frame %d  [target: %.3f]\n",
        //       (int)src_frame, src_frame, dst_frame, target_frame);
        /* copy each frame twice */
        memcpy(&(audiodata[dst_frame * bc->frame_bytes]),
               PLAY_PTR(bc) + (((int)src_frame) * bc->frame_bytes),
               bc->frame_bytes);
        dst_frame++;
      }
      src_frame += frame_skip;
    }
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
  int actual_delta_p;
  int time_off_ms;
  unsigned int period;

  gettimeofday(&initial_time, NULL);

  while (bc->state) {
    actual_delta_p = get_actual_delta(bc);
    time_off_ms = ((int)((bc->target_delta_p - actual_delta_p) * bc->period_time)) / 1000;

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
printf("Abort writes at %d/%d because out of frames\n", period, PERIODS_IN_ALSABUF);
        break;
      }

      if (time_off_ms < -5000) {
        bc->state = PURGE_32_8;
      } else if (time_off_ms < -1500) {
        bc->state = PURGE_16_8;
      } else if (time_off_ms < -500) {
        bc->state = PURGE_12_8;
      } else if (time_off_ms < -HYSTERESIS) {
        bc->state = PURGE_10_8;
      } else if (time_off_ms < HYSTERESIS) {
        bc->state = PLAY;
      } else if (time_off_ms < 300) {
        bc->state = BUFFER_7_8;
      } else if (time_off_ms < 1000) {
        bc->state = BUFFER_6_8;
      } else if (time_off_ms < 3000) {
        bc->state = BUFFER_4_8;
      } else if (time_off_ms < 6000) {
        bc->state = BUFFER_2_8;
      } else {
        bc->state = BUFFER_1_8;
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
      printf("%8.03f  STATE: %-10.10s CAP: %-4d  PLAY: %-4d  DELAY: %3.3f  "
             "DELTA: %4d/%-4d  ALSABUF: %ld/%d\n",
             delta_us / 1000000.0, STATE_NAME(bc->state), bc->cap, bc->play,
             (bc->target_delta_p * bc->period_time) / 1000000.0, actual_delta_p,
             bc->target_delta_p,
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
