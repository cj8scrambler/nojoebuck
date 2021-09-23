#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <zmq.h>

#include "n2.h"
#include "audio.h"

/*
 * UI command & control interface
 *
 * 2 ZMQ endpionts are available for queries and status:
 *   - cmd:    PUSH/PULL pattern.  Commands are PUSHed by the clients
 *             to the server which PULLs them.  Any response will be
 *             sent on the status interface.
 *   - status: PUB/SUB pattern.  Any command responses or asynchronous
 *             status updates are PUBlished by the server to the UI
 *             clients.  Note that the message order is not guaranteed.
 *             An asynchronus update may arrive before a command response.
 *             The clients must SUBscribe to the messages they want to
 *             receive with zmq_setsockopt():
 *               "D" - Delay status
 *               "B" - Buffer status
 *               ""  - All status
 *
 * ASCII string message format: "[char]:[value]"
 *
 * Command       Client->Server (PUSH->PULL)   Server->Client (PUB->SUB)
 * -------       ---------------------------   -------------------------
 * "D:1540"      set delay to 1.54 seconds     report delay setting of 1.54 seconds
 * "D:"          request delay setting         N/A
 * "B:100"       N/A                           Buffer full (playing normal with delay)
 * "B:[0-99]"    N/A                           Buffer filling (playing at slower speed to fill buffer)
 * "B:[101-199]" N/A                           Buffer emptying (playing at faster speed to empty buffer)
 * "B:"          request current buffer status 
 * "C:983"       N/A                           Current delay is 983 ms
 * "C:"          request current delay 
 */

/*
 * The ZMQ interface are IPC for local (on machine) access.
 * Could be changed to TCP if a remote client needs access.
 */
#define UI_STATUS         "ipc:///tmp/nojobuck_status"
#define UI_CMD            "ipc:///tmp/nojobuck_cmd"
#define MAX_UI_CMD         16
#define UI_SLEEP_TIME_MS   50 /* sleep time for main polling loop */

/* local globals */
static void *ui_cmd = NULL;
static void *zmq_context_cmd = NULL;
static void *ui_status = NULL;
static void *zmq_context_status = NULL;

void static update_delay_setting(buffer_config_t *bc, unsigned int delay_ms) {

  if (!bc) {
    fprintf(stderr, "%s() invalid call\n", __func__);
    return;
  }

  if (delay_ms > (bc->max_delay_ms)) {
    fprintf(stderr, "Error: delay too large: %d\n", delay_ms);
    return;
  }

  if (delay_ms < (bc->min_delay_ms)) {
    fprintf(stderr, "Error: delay too small: %d/%d\n", delay_ms, bc->min_delay_ms);
    return;
  }

  pthread_mutex_lock(&bc->lock);
  bc->target_delta_p = (delay_ms * 1000) / bc->period_time;
  pthread_mutex_unlock(&bc->lock);

  if (bc->verbose) {
    printf ("Updated delay setting to %.1f sec (%d periods)\n",
            (bc->target_delta_p * bc->period_time) / 1000000.0,
            bc->target_delta_p);
  }
}

/* return negative on error, buffer value on success */
static int ui_send_buf(buffer_config_t *bc) {
  char buffer[MAX_UI_CMD+1];
  unsigned int buf_pct; /* 0 - 200 */
  int ret;

  if (!bc)
    return -1;

  buf_pct = get_buf_pct(bc);
  snprintf(buffer, MAX_UI_CMD, "B:%d", buf_pct);
  if (bc->verbose) {
    printf("UI BUFFER: %d (%s)\n", buf_pct, buffer);
  }

  if (strlen(buffer) != (ret = zmq_send(ui_status, buffer, strlen(buffer), 0))) {
    fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
            buffer, strerror(errno));
  }

  return buf_pct;
}

static int ui_send_delay_setting(buffer_config_t *bc) {

  char buffer[MAX_UI_CMD+1];
  unsigned int delay;

  if (!bc)
  return -1;

  delay = (bc->target_delta_p * bc->period_time) / 1000;

  snprintf(buffer, MAX_UI_CMD, "D:%d", delay);

  if (bc->verbose) {
    printf("UI DELAY SETTING: %d (%s)\n", delay, buffer);
  }
  
  if (strlen(buffer) != zmq_send (ui_status, buffer,
                                  strlen(buffer), 0)) {
    fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
            buffer, strerror(errno));
  }

  return delay;
}

static int ui_send_current_delay(buffer_config_t *bc) {

  char buffer[MAX_UI_CMD+1];
  unsigned int delay;

  if (!bc)
  return -1;

  delay = (get_actual_delta(bc) * bc->period_time) / 1000;
  snprintf(buffer, MAX_UI_CMD, "C:%d", delay);

  if (bc->verbose) {
    printf("UI CURRENT DELAY: %d (%s)\n", delay, buffer);
  }
  
  if (strlen(buffer) != zmq_send (ui_status, buffer,
                                  strlen(buffer), 0)) {
    fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
            buffer, strerror(errno));
  }

  return delay;
}

/*
 * External Interface Functions
 */
int ui_init(buffer_config_t *bc) {

  zmq_context_cmd = zmq_ctx_new();
  if (!zmq_context_cmd) {
    fprintf(stderr, "Error creating ZMQ input context: %s\n", strerror(errno));
    return -1;
  }

  zmq_context_status = zmq_ctx_new();
  if (!zmq_context_status) {
    fprintf(stderr, "Error creating ZMQ output context: %s\n", strerror(errno));
    return -1;
  }

  ui_status = zmq_socket (zmq_context_status, ZMQ_PUB);
  if (0 != zmq_bind (ui_status, UI_STATUS)) {
    fprintf(stderr, "Could not create outgoing zmq socket\n");
    return -1;
  }

  ui_cmd = zmq_socket (zmq_context_cmd, ZMQ_PULL);
  if (0 != zmq_bind (ui_cmd, UI_CMD)) {
    fprintf(stderr, "Could not create incoming zmq socket\n");
    return -1;
  }

  return 0;
}

int ui_cleanup(void) {

  if (ui_cmd) {
    zmq_close (ui_cmd);
  }

  if (ui_status) {
    zmq_close (ui_status);
  }

  if (zmq_context_cmd) {
    zmq_ctx_destroy (zmq_context_cmd);
  }

  if (zmq_context_status) {
    zmq_ctx_destroy (zmq_context_status);
  }

  return 0;
}

void *ui_server_thread(void *data) {
  buffer_config_t *bc = (buffer_config_t *)data;
  int ret;
  char buffer[MAX_UI_CMD+1];
  unsigned int current_delay;
  unsigned int last_delay_setting = 0, last_buf = 0, last_current_delay=0;

  while (bc->state) {
    ret = zmq_recv (ui_cmd, buffer, MAX_UI_CMD, ZMQ_DONTWAIT);
    if (ret < 0 && errno != EAGAIN) {
      fprintf(stderr, "Error receiving zmq msg: %s\n", strerror(errno));
    } else if (ret >= 0) {
      char *token;
      buffer[ret] = '\0';

      if (bc->verbose)
        printf("UI received: '%s'\n", buffer);

      token = strtok(buffer, ":");
      if (token && !strcmp(token, "D")) {
        token = strtok(NULL, ":");
        if (token) {
          update_delay_setting(bc, strtol(token, NULL, 10));
        } else {
          ret = ui_send_delay_setting(bc);
          if (ret > 0) {
            last_delay_setting = ret;
          }
        }
      } else if (token && !strcmp(token, "B")) {
        ret = ui_send_buf(bc);
        if (ret > 0) {
          last_buf = ret;
        }
      } else if (token && !strcmp(token, "C")) {
        ret = ui_send_current_delay(bc);
        if (ret > 0) {
          last_current_delay = ret;
        }
      } else {
          fprintf(stderr, "Received invalid UI command: %s\n", buffer);
      }
    }

    /* check for changes in delay seting since last report */
    if (last_delay_setting != (bc->target_delta_p * bc->period_time) / 1000) {
      ret = ui_send_delay_setting(bc);
      if (ret > 0) {
        last_delay_setting = ret;
      }
    }

    /* check for changes in buff since last report */
    if (abs(last_buf - get_buf_pct(bc)) > 1) {
      ret = ui_send_buf(bc);
      if (ret > 0) {
        last_buf = ret;
      }
    }

    current_delay = (get_actual_delta(bc) * bc->period_time) / 1000;
    /* check for changes in current delay since report */
    if (abs(current_delay - last_current_delay) > 11) {
      ret = ui_send_current_delay(bc);
      if (ret > 0) {
        last_current_delay = ret;
      }
    }

    usleep(UI_SLEEP_TIME_MS * 1000);
  }

  return NULL;
}
