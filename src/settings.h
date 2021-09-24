#ifndef __SETTINGS_H
#define __SETTINGS_H

#define MAX_AUDIO_DEVNAME_LEN  64

typedef struct settings {
  char cap_int[MAX_AUDIO_DEVNAME_LEN];
  char play_int[MAX_AUDIO_DEVNAME_LEN];
  uint32_t rate;
  uint32_t memory;
  uint8_t bits;
  uint8_t verbose;
  snd_pcm_format_t format;
  uint32_t delay_ms;
  uint8_t wait;
} settings_t;

void settings_get_opts(settings_t *settings, int argc, char *argv[]);
#endif
