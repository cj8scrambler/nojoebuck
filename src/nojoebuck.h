#ifndef __NOJOEBUCK_H
#define __NOJOEBUCK_H

#include <pthread.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>

typedef enum playback_state {
  STOP       =  0,
  BUFFER_1_8 =  1,
  BUFFER_2_8 =  2,
  BUFFER_4_8 =  4,
  BUFFER_6_8 =  6,
  BUFFER_7_8 =  7,
  PLAY       =  8,
  PURGE_10_8 = 10,
  PURGE_12_8 = 12,
  PURGE_16_8 = 16,
  PURGE_32_8 = 32,
} playback_state_t;

#define STATE_NAME(x)  \
  (x == STOP)?"STOP": \
  (x == BUFFER_1_8)?"BUFFER 12%": \
  (x == BUFFER_2_8)?"BUFFER 25%": \
  (x == BUFFER_4_8)?"BUFFER 50%": \
  (x == BUFFER_6_8)?"BUFFER 75%": \
  (x == BUFFER_7_8)?"BUFFER 87%": \
  (x == PLAY)?"PLAY": \
  (x == PURGE_10_8)?"PURGE 125%": \
  (x == PURGE_12_8)?"PURGE 150%": \
  (x == PURGE_16_8)?"PURGE 200%": "PURGE 400%"

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

int get_buf_pct(buffer_config_t *bc);
#endif
