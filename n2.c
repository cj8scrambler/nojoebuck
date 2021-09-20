/* Use the newer ALSA API */
#include <stdio.h>
#include <alsa/asoundlib.h>

#include "n2.h"
#include "settings.h"
#include "audio.h"

/* Make sure both streams can be configured identically */
int config_both_streams(settings_t *settings, buffer_config_t *bc) {

  int ret = -1;
  unsigned int cap_actual_rate, play_actual_rate;
  unsigned int cap_num_periods, play_num_periods;
  unsigned int cap_period_time, play_period_time;
  snd_pcm_uframes_t cap_period_frames, play_period_frames;

  if ((ret = configure_stream(bc->cap_hndl, settings->format, settings->rate,
                              &cap_actual_rate, &cap_period_time,
                              &cap_period_frames, &cap_num_periods)) < 0) {
    fprintf(stderr, "Error configureing capture interface\n"); 
    return ret;
  }

  if ((ret = configure_stream(bc->play_hndl, settings->format, settings->rate,
                              &play_actual_rate, &play_period_time,
                              &play_period_frames, &play_num_periods)) < 0) {
    fprintf(stderr, "Error configureing playback interface\n"); 
    return ret;
  }

  if (cap_actual_rate != play_actual_rate) {
    fprintf(stderr, "Error: mismatch in bitrates.  cap: %d  play: %d\n",
            cap_actual_rate, play_actual_rate);
    return -1;
  }

  if (cap_num_periods != play_num_periods) {
    fprintf(stderr, "Error: mismatch in num periods.  cap: %d  play: %d\n",
            cap_num_periods, play_num_periods);
    return -1;
  }

  if (cap_period_time != play_period_time) {
    fprintf(stderr, "Error: mismatch in period time.  cap: %d  play: %d\n",
            cap_period_time, play_period_time);
    return -1;
  }

  if (cap_period_frames != play_period_frames) {
    fprintf(stderr, "Error: mismatch in period frames.  cap: %ld  play: %ld\n",
            cap_period_frames, play_period_frames);
    return -1;
  }

  pthread_mutex_lock(&bc->lock);
  /* Frame size is 2 bytes (for 16-bit) * 2 chans */
  bc->frame_bytes = (settings->bits / 8) * 2;
  bc->period_time = cap_period_time;
  bc->period_frames = cap_period_frames;
  bc->period_bytes = cap_period_frames * (bc->frame_bytes);
  bc->num_periods = cap_num_periods;

  if (settings->verbose) {
    printf("Audio Parameters:\n");
    printf("  Period (us):     %d\n", bc->period_time);
    printf("  Period (frames): %ld\n", bc->period_frames);
    printf("  Period (bytes):  %ld\n", bc->period_bytes);
    printf("  Num Periods:     %d\n", bc->num_periods);
    printf("  Calc ALSA Buffer (bytes):  %ld\n",
           bc->num_periods * bc->period_bytes);
    printf("  Calc ALSA Buffer (ms):     %.1f\n",
           (bc->num_periods * bc->period_time) / (1000.0));
  }
  pthread_mutex_unlock(&bc->lock);

  return 0;
}

int main(int argc, char *argv[]) {

  int ret;

  pthread_t thread;

  buffer_config_t buffer_config = { 0 };

  /* Default settings */
  settings_t settings = {
    .cap_int = "default",
    .play_int = "default",
    .bits = 16,
    .rate = 48000,
    .memory = 32*1024*1024,
    .verbose = 0,
    .delay_ms = 2500,
  };

  settings_get_opts(&settings, argc, argv);

  if ((ret = snd_pcm_open(&(buffer_config.cap_hndl), settings.cap_int,
                          SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n",
            settings.cap_int, snd_strerror(ret));
    exit(1);
  }

  if ((ret = snd_pcm_open(&(buffer_config.play_hndl), settings.play_int,
                          SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n",
            settings.play_int, snd_strerror(ret));
    exit(1);
  }

  if ((ret = config_both_streams(&settings, &buffer_config)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n",
            settings.play_int, snd_strerror(ret));
    exit(1);
  }

  pthread_mutex_lock(&(buffer_config.lock));
  buffer_config.state = BUFFER;
  buffer_config.target_delta_p = (settings.delay_ms * 1000) /  buffer_config.period_time;
  buffer_config.buffer = malloc(settings.memory);
  pthread_mutex_unlock(&(buffer_config.lock));

  if (!buffer_config.buffer) {
    fprintf(stderr, "Could allocate buffer memory\n");
    exit(1);
  }

  if(pthread_create(&thread, NULL, audio_io_thread, &buffer_config)) {
    fprintf(stderr, "Could not create audio I/O thread\n");
    goto cleanup;
  }

  if (settings.verbose) {
    printf("Buffer:\n");
    printf("  Size:         %d MB\n", settings.memory/1024/1024);
    printf("  Num Periods:  %d\n", buffer_config.num_periods);
    printf("  Target Delay: %d ms\n", settings.delay_ms);
    printf("  Target Delay: %d periods\n", buffer_config.target_delta_p);
    printf("  Max Delay:    %.1f seconds\n", (settings.memory / buffer_config.period_bytes) *
                                             (buffer_config.period_time / 1000000.0));
  }

  while (buffer_config.state)
  {
    pause();
  }

  pthread_join(thread, NULL);

cleanup:
  free(buffer_config.buffer);
} 
