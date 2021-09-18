#define ALSA_PCM_NEW_HW_PARAMS_API

#include <stdio.h>
#include <alsa/asoundlib.h>

#include "audio.h"

/*
 * return 0 on success; negative on error
 */
int configure_stream(snd_pcm_t *handle, int format, unsigned int rate,
                     unsigned int *actual_rate, unsigned int *period_us,
                     snd_pcm_uframes_t *period_bytes, unsigned int *num_periods) {
  int dir, err = -1;
  snd_pcm_hw_params_t *hw_params;

  if (!handle || !actual_rate || !period_us || !num_periods) {
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
  snd_pcm_hw_params_get_period_size(hw_params, period_bytes, &dir);
  snd_pcm_hw_params_get_periods(hw_params, num_periods, &dir);

exit:
  snd_pcm_hw_params_free (hw_params);
  return err;
}
