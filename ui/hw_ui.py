#!/usr/bin/env python3

import signal
import time
import struct
from smbus2 import SMBus, i2c_msg
import logging
import zmq
import microdotphat
#from seesaw import Seeaw
from RPi import GPIO

UI_CMD = "ipc:///tmp/nojobuck_cmd"
UI_STATUS = "ipc:///tmp/nojobuck_status"

REDRAW_PERIOD = 0.20      # seconds between redraws
DELAY_MODE_TIMEOUT = 1.5  # seconds to leave delay_setting screen up

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

    microdotphat.clear()
    microdotphat.write_string('%.2f' % (delay/ 1000.0), kerning=False)
    logging.debug('display delay_setting: %.2f' % (delay/ 1000.0))
    microdotphat.show()

def main():
    server_delay = -1 # last delay value sent/recieved from UI server
    server_buf = -1  # last buf value sent/recieved from UI server
    drawn_delay = -1 # last delay value drawn (higher priority than sent_delay)
    last_drawn_buf_val = -500
 
    r = Run();
    rotary = Seesaw();

    logging.basicConfig(level=logging.DEBUG,
                        format='%(asctime)s %(levelname)s %(message)s')
    logging.debug("Starting up %d" % (r.run))

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
    drawn_delay = new_delay_setting = server_delay
    send_pending = False

#    while r.keep_running():
    while r.run:
        shouldSleep = True

        # Look for encoder input
        delta = rotary.encoder_delta()

        # some crazy high blips come in; ignore those
        if delta and abs(delta) < 200:
            if abs(delta) >= 10:
                new_delay_setting = drawn_delay + int(delta * -500.0)
            if abs(delta) >= 5:
                new_delay_setting = drawn_delay + int(delta * -100.0)
            elif abs(delta) >= 3:
                new_delay_setting = drawn_delay + int(delta * -50.0)
            else:
                new_delay_setting = drawn_delay + int(delta * -10.0)

            logging.debug('rotary delta: {}  change delay: {} -> {}\n'.format(delta, drawn_delay, new_delay_setting));

            last_drawn_buf_time = time.time()
            show_delay_setting(new_delay_setting)
            drawn_delay = new_delay_setting
            shouldSleep = False
            send_pending = True

        # Only send message when we're not getting rotations
        elif (new_delay_setting != server_delay):
            logging.debug('Send: D:%d' % (new_delay_setting))
            socket_cmd.send(b"D:%d" % (new_delay_setting))
            server_delay = new_delay_setting
            shouldSleep = False
            send_pending = False

        message = ""
        try:
            message = socket_status.recv(flags=zmq.NOBLOCK).decode()
        except zmq.ZMQError: 
            pass

        if (message != ""):
            shouldSleep = False
            cmd = message.split(':');
            logging.debug('Received: {}'.format(cmd))
            if (cmd[0] == "B"):
                server_buf = int(cmd[1])
            elif (cmd[0] == "D"):
                server_delay = int(cmd[1])

        now = time.time()
        # Only update display with server value if we have no pending UI change to send
        if (drawn_delay != server_delay):
            if not send_pending:
                logging.debug('Drawing delay because new received from server')
                last_drawn_buf_time = time.time()
                show_delay_setting(server_delay)
                drawn_delay = new_delay_setting = server_delay
            else:
                logging.debug('Skiping display update to {} because send is pending'.format(server_delay))
        elif (now - last_drawn_buf_time > DELAY_MODE_TIMEOUT):
            logging.debug('Drawing buf because timeout')
            show_buf(server_buf)
            last_drawn_buf_val = server_buf
            # make it a big number to avoid lots of redraws
            last_drawn_buf_time = 2 * now
        elif (server_buf != last_drawn_buf_val):
            logging.debug('Drawing buf because new value')
            show_buf(server_buf)
            last_drawn_buf_val = server_buf
            
        if (shouldSleep):
            time.sleep(0.01)

    # Clean up
    microdotphat.clear()


_I2C_CH = 1
_I2C_ADDR = addr=0x36

_STATUS_BASE = 0x00
_STATUS_HW_ID = 0x01
_STATUS_VERSION = 0x02
_STATUS_OPTIONS = 0x03
_STATUS_TEMP = 0x04
_STATUS_SWRST = 0x7F

_HW_ID_CODE = 0x55

_ENCODER_BASE = 0x11

_ENCODER_STATUS = 0x00
_ENCODER_INTENSET = 0x10
_ENCODER_INTENCLR = 0x20
_ENCODER_POSITION = 0x30
_ENCODER_DELTA = 0x40

_NEOPIXEL_BASE = 0x0E

_NEOPIXEL_STATUS = 0x00
_NEOPIXEL_PIN = 0x01
_NEOPIXEL_SPEED = 0x02
_NEOPIXEL_BUF_LENGTH = 0x03
_NEOPIXEL_BUF = 0x04
_NEOPIXEL_SHOW = 0x05

# Pixel color order constants
RGB = (0, 1, 2)
"""Red Green Blue"""
GRB = (1, 0, 2)
"""Green Red Blue"""
RGBW = (0, 1, 2, 3)
"""Red Green Blue White"""
GRBW = (1, 0, 2, 3)
"""Green Red Blue White"""

class Seesaw:

    def __init__(
        self,
        i2c_ch=_I2C_CH,
        i2c_addr=_I2C_ADDR,
        pixel_order=None
    ):
        self._addr = i2c_addr
        self._bus = SMBus(i2c_ch)
        self._pixel_order=GRB
        self._pin = 6
        self._bpp = 3
        self._pixel_order = GRB if pixel_order is None else pixel_order

        print("DZ: reset")
        self._reset()

        self.write(_NEOPIXEL_BASE, _NEOPIXEL_PIN, [self._pin])
        cmd = struct.pack(">H", 1 * self._bpp)
        self.write(_NEOPIXEL_BASE, _NEOPIXEL_BUF_LENGTH, cmd)

    def read(self, base, reg, size):
        self._bus.write_i2c_block_data(self._addr, base, [reg])
        time.sleep(0.01)
        read = i2c_msg.read(self._addr, size)
        self._bus.i2c_rdwr(read)
        return bytes(read)

    def write(self, base, reg, data=None):
        if data:
            buf = bytearray(1+len(data))
        else:
            buf = bytearray(1)
        buf[0] = reg
        if data:
            buf[1:] = data
        self._bus.write_i2c_block_data(self._addr, base, buf)

    def _reset(self):
        self.write(_STATUS_BASE, _STATUS_SWRST, [0xFF])
        time.sleep(0.500)
        hwid = self.read(_STATUS_BASE, _STATUS_HW_ID, 1)
        if (hwid[0] != _HW_ID_CODE):
            raise RuntimeError(
                "Seesaw hardware ID returned (0x{:x}) is not "
                "correct! Expected 0x{:x}. Please check your wiring.".format(
                hwid[0], _HW_ID_CODE))

    def encoder_position(self):
        buf = self.read(_ENCODER_BASE, _ENCODER_POSITION, 4)
        return struct.unpack(">i", buf)[0]

    def encoder_delta(self):
        """The change in encoder position since it was last read"""
        buf = self.read(_ENCODER_BASE, _ENCODER_DELTA, 4)
        return struct.unpack(">i", buf)[0]

    def set_pixel(self, color):

        cmd = bytearray(2 + self._bpp)
        struct.pack_into(">H", cmd, 0, 0)

        if isinstance(color, int):
            w = color >> 24
            r = (color >> 16) & 0xFF
            g = (color >> 8) & 0xFF
            b = color & 0xFF
        else:
            r, g, b = color

        # Store colors in correct slots
        cmd[2 + self._pixel_order[0]] = r
        cmd[2 + self._pixel_order[1]] = g
        cmd[2 + self._pixel_order[2]] = b

        self.write(_NEOPIXEL_BASE, _NEOPIXEL_BUF, cmd)
        self.write(_NEOPIXEL_BASE, _NEOPIXEL_SHOW)

if __name__ == "__main__":
    # execute only if run as a script
    main()
