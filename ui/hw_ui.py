#!/usr/bin/env python3

import signal
import time
import logging
import zmq
import microdotphat
from RPi import GPIO

UI_CMD = "ipc:///tmp/nojobuck_cmd"
UI_STATUS = "ipc:///tmp/nojobuck_status"

REDRAW_PERIOD = 0.20      # seconds between redraws
DELAY_MODE_TIMEOUT = 1.5  # seconds to leave delay_setting screen up

# Rotary encoder pins
CLK_PIN = 17
DT_PIN = 27

class Run:
  def __init__(self):
    self.run = True;
    signal.signal(signal.SIGINT, self.exit_gracefully)
    signal.signal(signal.SIGTERM, self.exit_gracefully)

  def exit_gracefully(self, *args):
    print("Got terminate signal")
    self.run = False

def show_buf(buf):

    logging.debug('show_buf():');
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

def show_delay_setting(delay):

    logging.debug('show_delay():');
    microdotphat.clear()
    microdotphat.write_string('%.2f' % (delay/ 1000.0), kerning=False)
    logging.debug('  delay_setting: %.2f' % (delay/ 1000.0))
    microdotphat.show()

def main():
    server_delay = -1 # last delay value sent/recieved from UI server
    server_buf = -1  # last buf value sent/recieved from UI server
    drawn_delay = -1 # last delay value drawn (higher priority than sent_delay)
    drawn_buf = -1  # last buf value drawn
 
    r = Run();

    logging.basicConfig(level=logging.INFO,
                        format='%(asctime)s %(levelname)s %(message)s')
    logging.debug("Starting up %d" % (r.run))

    # Setup rotary encoder inputs
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(CLK_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    GPIO.setup(DT_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    clkLastState = GPIO.input(CLK_PIN)

    socket_status = zmq.Context().socket(zmq.SUB)
    socket_status.connect (UI_STATUS)
    socket_status.setsockopt_string(zmq.SUBSCRIBE, "") # subscribe to everything

    socket_cmd = zmq.Context().socket(zmq.PUSH)
    socket_cmd.connect (UI_CMD)

    # query the current delay/buf before starting
    while (server_delay == -1 or server_buf == -1):
        logging.debug("Query delay & buf")
        socket_cmd.send(b"D:");
        socket_cmd.send(b"B:");

        cmd = ['','']
        try:
            cmd = socket_status.recv(flags=zmq.NOBLOCK).decode().split(':')
        except zmq.ZMQError: 
            pass
        if (cmd[0] == "D"):
            server_delay = int(cmd[1]);
            logging.debug('got delay_setting query response: %d' % (server_delay))
        elif (cmd[0] == "B"):
            server_buf = int(cmd[1]);
            logging.debug('got buffer query response: %d' % (server_buf))
        time.sleep(0.01)

    last_drawn_buf_time = time.time()
    show_delay_setting(server_delay)
    drawn_delay = server_delay

#    while r.keep_running():
    while r.run:
        shouldSleep = True

        # Look for encoder input
        clkState = GPIO.input(CLK_PIN)
        dtState = GPIO.input(DT_PIN)
        new_delay_setting = drawn_delay
        if clkState != clkLastState:
            if dtState != clkState:
                new_delay_setting = drawn_delay + 10
            else:
                new_delay_setting = drawn_delay - 10
            clkLastState = clkState
            logging.debug('got rotation: %d -> %d\n' % (drawn_delay, new_delay_setting));

            if (new_delay_setting < 0):
                new_delay_setting = 0
            if (new_delay_setting > MAX_DELAY):
                new_delay_setting = MAX_DELAY

            last_drawn_buf_time = time.time()
            show_delay_setting(new_delay_setting)
            drawn_delay = new_delay_setting
            shouldSleep = False

        # Only send message when we're not getting rotations
        elif (new_delay_setting != server_delay):
            logging.debug('Sending delay update: %d' % (new_delay_setting))
            socket_cmd.send(b"D:%d" % (new_delay_setting))
            server_delay = new_delay_setting
            shouldSleep = False

        message = ""
        try:
            message = socket_status.recv(flags=zmq.NOBLOCK).decode()
        except zmq.ZMQError: 
            pass

        if (message != ""):
            shouldSleep = False
            cmd = message.split(':');
            if (cmd[0] == "B"):
                server_buf = int(cmd[1])
                logging.debug('Received new server buf: %d' % (server_buf))
            elif (cmd[0] == "D"):
                server_delay = int(cmd[1])
                logging.debug('Received new server delay: %d' % (server_delay))

        now = time.time()
        if (drawn_delay != server_delay):
            logging.debug('Drawing delay because new value found')
            last_drawn_buf_time = time.time()
            show_delay_setting(server_delay)
            drawn_delay = server_delay
        elif (now - last_drawn_buf_time > DELAY_MODE_TIMEOUT):
            if (abs(server_buf - drawn_buf) > 0):
                logging.debug('Drawing buf because timeout')
                show_buf(server_buf)
                drawn_buf = server_buf
            
        if (shouldSleep):
            time.sleep(0.01)

    # Clean up
    microdotphat.clear()

if __name__ == "__main__":
    # execute only if run as a script
    main()
