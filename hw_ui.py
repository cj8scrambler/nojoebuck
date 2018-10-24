import zmq
import microdotphat
import threading
import logging
import time
from RPi import GPIO

UI_CMD = "ipc:///tmp/nojobuck_cmd"
UI_STATUS = "ipc:///tmp/nojobuck_status"
MAX_DELAY = 120000
REDRAW_PERIOD = 0.20      # seconds between redraws
DELAY_MODE_TIMEOUT = 1.5  # seconds to leave delay screen up
CLK_PIN = 17
DT_PIN = 18

delay = -1
buf = -1
draw_state = 0
last_redraw = 0.0

def show_buf():
    global buf

    microdotphat.clear()
    if (buf == 100):
        microdotphat.write_string('PLAY', offset_x = 8, kerning=False)
        logging.debug('  draw PLAY');
    else:
        percent = buf
        if (percent > 100):
            percent = percent - 100

        microdotphat.write_string('%d' % (percent), kerning=False)
        cols = int(((percent / 100.0) * 29) + 0.5)
        left = 16
        logging.debug('  pct: %d  buf: %d  fill: %d/%d' % (percent, buf, cols, microdotphat.WIDTH - left))
        for x in range( left, left + cols):
            for y in range(microdotphat.HEIGHT):
                #logging.debug('    (%d,%d) = 1' % (x, y))
                microdotphat.set_pixel(x, y, 1)

    microdotphat.show()

def show_delay():
    global delay

    microdotphat.clear()
    microdotphat.write_string('%.2f' % (delay / 1000.0), kerning=False)
    logging.debug('  delay: %.2f' % (delay / 1000.0))
    microdotphat.show()

def redraw():
    global delay
    global draw_state
    global last_redraw

    now = time.time()
    if ((buf != 100) and ((now - last_redraw) < REDRAW_PERIOD)):
        return

    last_redraw = now

    if (draw_state == 0):
        show_buf()
    else:
        show_delay()

def buf_mode():
    global draw_state
    draw_state = 0

def main():
    global delay
    global buf

    logging.basicConfig(level=logging.ERROR,
                        format='%(asctime)s %(levelname)s %(message)s',
                        filename='hw_ui.log',
                        filemode='w')
    logging.debug("Starting up")

    timer=threading.Timer(DELAY_MODE_TIMEOUT, buf_mode)
 
    # Setup rotary encoder inputs
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(CLK_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    GPIO.setup(DT_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    clkLastState = GPIO.input(CLK_PIN)

    socket_cmd = zmq.Context().socket(zmq.PUSH)
    socket_cmd.connect (UI_CMD)
    socket_status = zmq.Context().socket(zmq.PULL)
    socket_status.connect (UI_STATUS)

    # There might be a bunch of other messages coming in so wait for the right one
    while (delay == -1):
        socket_cmd.send("D:");
        cmd = ['','']
        try:
            cmd = socket_status.recv(flags=zmq.NOBLOCK).split(':')
        except zmq.ZMQError: 
            pass
        if (cmd[0] == "D"):
            delay = int(cmd[1]);
            logging.debug('got delay query response: %d' % (delay))
            new_delay = delay
        else:
            logging.debug('BAD delay response: %s ' % (cmd))

    while (buf == -1):
        socket_cmd.send("B:");
        cmd = ['','']
        try:
            cmd = socket_status.recv(flags=zmq.NOBLOCK).split(':')
        except zmq.ZMQError: 
            pass
        if (cmd[0] == "B"):
            buf = int(cmd[1]);
            logging.debug('got buffer query response: %d' % (buf))
        else:
            logging.debug('BAD buffer response: %s ' % (cmd))
        time.sleep(0.01)

    redraw();

    while True:
        shouldSleep = True
        clkState = GPIO.input(CLK_PIN)
        dtState = GPIO.input(DT_PIN)
        if clkState != clkLastState:
            if dtState != clkState:
                new_delay = delay + 10
            else:
                new_delay = delay - 10
            clkLastState = clkState
            logging.debug('got rotation: %d -> %d\n' % (delay, new_delay));

            if (new_delay < 0):
                new_delay = 0
            if (new_delay > MAX_DELAY):
                new_delay = MAX_DELAY
            draw_state=1
            redraw()
            timer.cancel()
            timer.start() #switch back to draw_state=0 later

        # Send message when we're not getting rotations
        else:
            if (new_delay != delay):
                delay = new_delay
                logging.debug('Updated delay: %d' % (delay))
                socket_cmd.send("D:%d" % (delay))
                redraw()
                shouldSleep = False

        message = ""
        try:
            message = socket_status.recv(flags=zmq.NOBLOCK)
        except zmq.ZMQError: 
            pass

        if (message != ""):
            shouldSleep = False
            cmd = message.split(':');
            if (cmd[0] == "B"):
                new_buf = int(cmd[1])
                if (new_buf != buf):
                    buf = new_buf
                    redraw()

        if (shouldSleep):
            time.sleep(0.01)

if __name__ == "__main__":
    # execute only if run as a script
    main()
