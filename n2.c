/* Use the newer ALSA API */
#include <stdio.h>
#include <alsa/asoundlib.h>

#include "settings.h"
#include "audio.h"

typedef enum playback_state {
    STOP = 0,
    BUFFER,
    PLAY,
    DOUBLE,
} playback_state;

/* Make sure both streams can be configured identically */
int config_btoh_streams(snd_pcm_t *cap_hndl, snd_pcm_t *play_hndl,
                        settings_t *settings, unsigned int *period_time,
                        snd_pcm_uframes_t *period_bytes) {

  int ret = -1;
  unsigned int cap_actual_rate, play_actual_rate;
  unsigned int cap_num_periods, play_num_periods;
  unsigned int cap_period_time, play_period_time;
  snd_pcm_uframes_t cap_period_bytes, play_period_bytes;

  if ((ret = configure_stream(cap_hndl, settings->format, settings->rate,
                             &cap_actual_rate, &cap_period_time,
                             &cap_period_bytes, &cap_num_periods)) < 0) {
    fprintf(stderr, "Error configureing capture interface\n"); 
    return ret;
  }

  if ((ret = configure_stream(play_hndl, settings->format, settings->rate,
                              &play_actual_rate, &play_period_time,
                              &play_period_bytes, &play_num_periods)) < 0) {
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

  if (cap_period_bytes != play_period_bytes) {
    fprintf(stderr, "Error: mismatch in period time.  cap: %ld  play: %ld\n",
            cap_period_bytes, play_period_bytes);
    return -1;
  }

  if (settings->verbose) {
    printf("Audio Parameters:\n");
    printf("  Period (us):     %d\n", cap_period_time);
    printf("  Period (bytes):  %ld\n", cap_period_bytes);
    printf("  Num Periods:     %d\n", cap_num_periods);
    printf("  Calc Buffer (bytes):  %ld\n", cap_num_periods * cap_period_bytes);
    printf("  Calc Buffer (ms):     %.1f\n", (cap_num_periods * cap_period_time) / (1000.0));
  }

  *period_time = cap_period_time;
  *period_bytes = cap_period_bytes;

  return 0;
}

int main(int argc, char *argv[]) {

  int ret;

  snd_pcm_t *cap_hndl;
  snd_pcm_t *play_hndl;

  unsigned int period_time;    /* length of a period in us */
  snd_pcm_uframes_t period_bytes;

  uint8_t *buffer;

  /* Default settings */
  settings_t settings = {
    .cap_int = "default",
    .play_int = "default",
    .bits = 16,
    .rate = 44100,
    .memory = 32*1024*1024,
    .verbose = 0
  };

  settings_get_opts(&settings, argc, argv);

  if ((ret = snd_pcm_open(&cap_hndl, settings.cap_int,
                          SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n",
            settings.cap_int, snd_strerror(ret));
    exit(1);
  }

  if ((ret = snd_pcm_open(&play_hndl, settings.play_int,
                          SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n",
            settings.play_int, snd_strerror(ret));
    exit(1);
  }

  if ((ret = config_btoh_streams(cap_hndl, play_hndl,
                                 &settings, &period_time,
                                 &period_bytes)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n",
            settings.play_int, snd_strerror(ret));
    exit(1);
  }

  buffer = malloc(settings.memory);
  if (!buffer) {
    fprintf(stderr, "Could allocate buffer memory\n");
    exit(1);
  }

  if (settings.verbose) {
    printf("Buffer:\n");
    printf("  Size:      %d MB\n", settings.memory/1024/1024);
    printf("  Max Delay: %.1f seconds\n", (settings.memory/period_bytes) *
                                  (period_time / 1000000.0));
  }

//cleanup:
  free(buffer);
} 
