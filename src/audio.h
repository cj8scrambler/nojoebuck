#ifndef __AUDIO_H
#define __AUDIO_H
#include "nojoebuck.h"

int configure_stream(snd_pcm_t *handle, int format, unsigned int rate,
                     unsigned int *actual_rate, unsigned int *period_us,
                     snd_pcm_uframes_t *period_bytes, unsigned int *num_periods);
void *audio_io_thread(void *ptr); 
unsigned int get_actual_delta(buffer_config_t *bc);
#endif
