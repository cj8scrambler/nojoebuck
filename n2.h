#include <pthread.h>

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
  snd_pcm_t *cap_hndl;
  snd_pcm_t *play_hndl;
  unsigned int period_time;
  unsigned int num_periods;
  snd_pcm_uframes_t period_bytes;

  /* Paramenters protected by lock */
  pthread_mutex_t lock;  
  uint8_t *buffer;      /* Application memory buffer for time delay */
  unsigned int play;    /* playback period number (0 - num_periods-1) */
  unsigned int cap;     /* capture period number (0 - num_periods-1) */

  unsigned int target_delta_p; /* target delta in periods */

  playback_state_t state;
} buffer_config_t;

