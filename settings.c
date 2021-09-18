/* Use the newer ALSA API */
#define ALSA_PCM_NEW_HW_PARAMS_API

#include <stdio.h>
#include <alsa/asoundlib.h>
#include <getopt.h>

#include "settings.h"

/* Show usage and exit with retcode */
static void usage(settings_t *settings, int retcode)
{
  printf("nojoebuck [options]...\n");
  printf("  -b, --bits=[16|24|32]  Bit depth.  Default: %d\n", settings->bits);
  printf("  -c, --capture=NAME     Name of capture interface (list with aplay -L)."
         "  Default: %s\n", settings->cap_int);
  printf("  -h, --help             This usage message\n");
  printf("  -m, --memory=SIZE      Memory buffer to reserve in MB.  Default: %.1f\n",
         settings->memory/(1024.0*1024.0));
  printf("  -p, --playback=NAME    Name of playback interface (list with aplay -L)."
         "  Default: %s\n", settings->play_int);
  printf("  -r, --rate=RATE        Sample rate.  Default: %d\n", settings->rate);

  exit(retcode);
}

/* parses arguments and updates global settings with user's input */
void settings_get_opts(settings_t *settings, int argc, char *argv[])
{
  int c = 0;
  int option_index = 0;
  long v;

  while (c != -1)
  {
    static struct option long_options[] =
    {
      {"bits",      required_argument,  NULL, 'b'},
      {"capture",   required_argument,  NULL, 'c'},
      {"help",      no_argument,        NULL, 'h'},
      {"memory",    required_argument,  NULL, 'm'},
      {"playback",  required_argument,  NULL, 'p'},
      {"rate",      required_argument,  NULL, 'r'},
      {"verbose",   no_argument,        NULL, 'v'},
      {NULL, 0, NULL, 0}
    };

    c = getopt_long (argc, argv, "b:c:hm:p:r:v",
                       long_options, &option_index);

    /* Detect the end of the options. */
    switch (c)
    {
      case -1:
        /* No more options left */
        break;

      case 'b':
        v = atol(optarg);
        if ((v != 16) && (v != 24) && (v != 32)) {
            printf ("option -b: invalid bit depth\n");
            usage(settings, -1);
        }
        settings->bits = v;
        break;

      case 'h':
      case '?':
	usage(settings, 0);
        exit(0);

      case 'v':
	settings->verbose = 1;
        break;

      case 'c':
        strncpy(settings->cap_int, optarg, MAX_AUDIO_DEVNAME_LEN);
        settings->cap_int[MAX_AUDIO_DEVNAME_LEN-1] = '\0';
        break;

      case 'm':
        settings->memory = atol(optarg) * 1024 * 1024;
        break;

      case 'p':
        strncpy(settings->play_int, optarg, MAX_AUDIO_DEVNAME_LEN);
        settings->play_int[MAX_AUDIO_DEVNAME_LEN-1] = '\0';
        break;

      case 'r':
        settings->rate = atol(optarg);
        break;

      default:
        usage(settings, -1);
    }
  } 

  /* Update format based on bits */
  if (settings->bits == 16)
      settings->format = SND_PCM_FORMAT_S16_LE;
  else if (settings->bits == 24)
      settings->format = SND_PCM_FORMAT_S24_LE;
  else if (settings->bits == 32)
      settings->format = SND_PCM_FORMAT_S32_LE;

  if (settings->verbose) {
    printf("Settings:\n");
    printf("  Capture:   %s\n", settings->cap_int);
    printf("  Playback:  %s\n", settings->play_int);
    printf("  Rate:      %d\n", settings->rate);
    printf("  Depth:     %d [%s (%s)]\n", settings->bits,
           snd_pcm_format_name(settings->format),
           snd_pcm_format_description(settings->format));
    printf("  Memory:    %dMB\n", settings->memory/1024/1024);
  }
}
