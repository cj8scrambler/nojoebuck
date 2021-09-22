#ifndef __N2_H
#define __N2_H

#include <pthread.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

typedef enum playback_state {
    STOP = 0,
    BUFFER,
    PLAY,
    DOUBLE,
} playback_state_t;

#define STATE_NAME(x)  \
    (x == STOP)?"STOP": \
    (x == BUFFER)?"BUFFER": \
    (x == PLAY)?"PLAY":"DOUBLE"

typedef struct buffer_config {
  /* unprotected paramters (only set once) */
  bool verbose;;
  snd_pcm_t *cap_hndl;
  snd_pcm_t *play_hndl;
  unsigned int alsa_num_periods;   /* Number of periods in ALSA buffer */
  unsigned int mem_num_periods;    /* Number of periods in app memory buffer */
  unsigned int period_time;        /* period length in uS */
  snd_pcm_uframes_t period_bytes;  /* size of period in bytes */
  unsigned int frame_bytes;        /* size of frame in bytes */
  snd_pcm_uframes_t period_frames; /* number of frames in a period */
  unsigned int min_delay_ms;       /* 5 ALSA periods */
  unsigned int max_delay_ms;       /* Max delay (based on app memory) */

  /* Paramenters protected by lock */
  pthread_mutex_t lock;  
  uint8_t *buffer;      /* Application memory buffer for time delay */
  unsigned int play;    /* playback period number (0 - mem_num_periods-1) */
  unsigned int cap;     /* capture period number (0 - mem_num_periods-1) */

  unsigned int target_delta_p; /* target delta in periods */

  playback_state_t state;
} buffer_config_t;
#endif
