int configure_stream(snd_pcm_t *handle, int format, unsigned int rate,
                     unsigned int *actual_rate, unsigned int *period_us,
                     snd_pcm_uframes_t *period_bytes, unsigned int *num_periods);
void *audio_io_thread(void *ptr); 
