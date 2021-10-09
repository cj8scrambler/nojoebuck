#import time
#import struct
#from smbus2 import SMBus, i2c_msg

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

        print("DZ: Set neopixel pin")
        self.write(_NEOPIXEL_BASE, _NEOPIXEL_PIN, [self._pin])
        cmd = struct.pack(">H", 1 * self._bpp)
        print("DZ: Set neopixel len")
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
        print("DZ:  write to 0x{:x}: {:x} {}".format(self._addr, base, buf))
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
        print("DZ: encoder pos returned: {}".format(buf))
        return struct.unpack(">i", buf)[0]

    def encoder_delta(self):
        """The change in encoder position since it was last read"""
        buf = self.read(_ENCODER_BASE, _ENCODER_DELTA, 4)
        print("DZ: encoder delta returned: {}".format(buf))
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
