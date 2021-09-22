#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <zmq.h>

#include "n2.h"
#include "audio.h"

/*
 * UI Command control format
 *
 * 2 ZMQ endpionts are available for control/status:
 *   - cmd:    PUSH/PULL pattern.  Commands are PUSHed by the clients
 *             to the server which PULLs them.  Any response will be
 *             sent on the status interface.
 *   - status: PUB/SUB pattern.  Any command responses or asynchronous
 *             status updates are PUBlished by the server to the UI
 *             clients.  The clients must SUBscribe to the messages
 *             they want to receive with zmq_setsockopt():
 *               "D" - Delay status
 *               "B" - Buffer status
 *               ""  - All status
 *
 * ASCII string message format: "[char]:[value]"
 *
 * Command       Client->Server (REQ)          Server->Client (REP or SUB)
 * -------       --------------------          --------------
 * "D:1540"      set delay to 1.54 seconds     report current delay of 1.54 seconds
 * "D:"          request current delay         N/A
 * "B:100"       N/A                           Buffer full (playing normal with delay)
 * "B:[0-99]"    N/A                           Buffer filling (playing at 1/2 speed to fill buffer)
 * "B:[101-199]" N/A                           Buffer emptying (playing at 2x speed to empty buffer)
 * "B:"          request current buffer status 
 */

/*
 * default ZMQ interface are IPC for local (on machine) access.
 * Can be changed to TCP if a remote client needs access
 */
#define UI_STATUS         "ipc:///tmp/nojobuck_status"
#define UI_CMD            "ipc:///tmp/nojobuck_cmd"
#define MAX_UI_CMD         16
#define UI_SLEEP_TIME_MS  50

/* static globals */
void *ui_cmd = NULL;
void *zmq_context_cmd = NULL;
void *ui_status = NULL;
void *zmq_context_status = NULL;

void static update_delay(buffer_config_t *bc, unsigned int delay_ms) {

    if (!bc) {
        fprintf(stderr, "%s() invalid call\n", __func__);
        return;
    }

    pthread_mutex_lock(&bc->lock);
    bc->target_delta_p = (delay_ms * 1000) / bc->period_time;
    pthread_mutex_unlock(&bc->lock);

    if (bc->verbose) {
      printf ("Updated delay to %.1f sec (%d periods)\n",
              (bc->target_delta_p * bc->period_time) / 1000000.0,
              bc->target_delta_p);
    }
}

static int ui_get_buf_pct(buffer_config_t *bc) {
    int buf_pct = 0;
    if (bc) {
        buf_pct = (int)((get_actual_delta(bc) * 100) /
                        bc->target_delta_p + 0.5);
    }

    if (buf_pct > 200)
        buf_pct = 200;

    return buf_pct;
}

/* return negative on error, buffer value on success */
static int ui_send_buf(buffer_config_t *bc) {
    char buffer[MAX_UI_CMD+1];
    unsigned int buf_pct; /* 0 - 200 */
    int ret;

    if (!bc)
        return -1;

#if 0
    switch(bc->state) {
    case PLAY:
        buf_pct = 100;
        break;
    case BUFFER:
        buf_pct = (int)((act_delay * 100) / bc->target_delta_p + 0.5));
        break;
    case DOUBLE:
        if (bc->target_delta_p > act_delay) {
            buf_pct = 100;
        } else {
            buf_pct = (int)(((act_delay - bc->target_delta_p) * 100) / bc->target_delta_p + 0.5);
            buf_p = (buf_p > 200) ? 200 : buf_p;
            snprintf(buffer, MAX_UI_CMD, "B:%d", buf_p);
        }
        break;
    default:
        fprintf(stderr, "Received B: in inalid state: %d\n", bc->state);
        buffer[0] = '\0';
        break;
    }
#endif

    buf_pct = ui_get_buf_pct(bc);
    snprintf(buffer, MAX_UI_CMD, "B:%d", buf_pct);
    if (bc->verbose) {
        printf("UI BUFFER: %d (%s)\n", buf_pct, buffer);
    }

    if (strlen(buffer) != (ret = zmq_send(ui_status, buffer, strlen(buffer), 0))) {
//        if (errno == EAGAIN) {
//            /* ignore missing client */
//            ret = 0;
//            if (bc->verbose) {
//                printf("UI NO CLIENT\n");
//            }
//        } else {
            fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
                    buffer, strerror(errno));
//        }
    }

    return buf_pct;
}

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

static int ui_send_delay(buffer_config_t *bc) {

  char buffer[MAX_UI_CMD+1];
  unsigned int delay;

  if (!bc)
    return -1;

  delay = (bc->target_delta_p * bc->period_time) / 1000;

  snprintf(buffer, MAX_UI_CMD, "D:%d", delay);

  if (bc->verbose) {
    printf("UI DELAY: %d (%s)\n", delay, buffer);
  }
  
  if (strlen(buffer) != zmq_send (ui_status, buffer,
      strlen(buffer), 0)) {
    fprintf(stderr, "Error sending zmq msg [%s]: %s\n",
            buffer, strerror(errno));
  }

  return delay;
}

void *ui_server_thread(void *data) {
    buffer_config_t *bc = (buffer_config_t *)data;
    int ret;
    char buffer[MAX_UI_CMD+1];
    unsigned int last_delay = 0, last_buf = 0;

    while (bc->state) {
	ret = zmq_recv (ui_cmd, buffer, MAX_UI_CMD, ZMQ_DONTWAIT);
        if (ret < 0 && errno != EAGAIN) {
            fprintf(stderr, "Error receiving zmq msg: %s\n", strerror(errno));
        } else if (ret >= 0) {
            char *token;
	    buffer[ret] = '\0';

            if (bc->verbose)
              printf("UI got cmd: '%s'\n", buffer);

	    token = strtok(buffer, ":");
	    if (token && !strcmp(token, "D")) {
	        token = strtok(NULL, ":");
		if (token) {
		    unsigned long val = strtol(token, NULL, 10);
                    if (val <= (bc->max_delay_ms)) {
                        update_delay(bc, val);
                    }
                } else {
                    ret = ui_send_delay(bc);
                    if (ret > 0) {
                        last_delay = ret;
                    }
                }
            } else if (token && !strcmp(token, "B")) {
                ret = ui_send_buf(bc);
                if (ret > 0) {
                    last_buf = ret;
                }
            } else {
                    fprintf(stderr, "Received invalid UI command: %s\n", buffer);
            }
        }

        /* check for changes in delay */
        if (last_delay != (bc->target_delta_p * bc->period_time) / 1000) {
printf("found change in delay; sending new val\n");
            ret = ui_send_delay(bc);
            if (ret > 0) {
                last_delay = ret;
            }
        }

        /* check for changes in buff */
        if (abs(last_buf - ui_get_buf_pct(bc)) > 1) {
printf("found change in buf; sending new val\n");
            ret = ui_send_buf(bc);
            if (ret > 0) {
                last_buf = ret;
            }
        }

        usleep(UI_SLEEP_TIME_MS * 1000);
    }

    return NULL;
}
