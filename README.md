# nojoebuck

nojoebuck is an audio buffering program intended for use on Raspberry Pi
hardware.  It allows you to add delay to an audio such as a radio
broadcast of a sporting event which Joe Buck is calling on TV.

## Background

In the fall of 2016 the Cubs were making a strong run through the playoffs.
Since it was post-seaons, the games were no longer handled by the local
broadcasters who knew the team and provided insightful commentary.  Instead
the games were on national broadcast where the commentators had nothing useful
to say.

In the old days, you could simply turn off the TV sound and turn on the radio
broadcast to solve this.  However now with streaming video, the delay between
radio and TV is too great.  This is where nojoebuck comes in.  Put it inline
with the audio source and you can tune the audio delay to get it to match
with the streaming video source.

## Hardware

This can run on any Linux platform, but the Raspberry Pi Zero was the intended
platform.  Here are the basics:
  * Raspberry Pi Zero - Any Linux machine will work, but this is cheap and small
  * ALSA compatible capture and playback devices
    * Full size Raspberry Pis have playback interfaces but will need a capture device
    * Pi Zero has no audio interfaces, so I used the [Zero Soundcard][https://www.audioinjector.net/rpi-zero]
  * Optional hardware based UI
    * [Microdot PHAT][https://shop.pimoroni.com/products/microdot-phat?variant=25454635591]
    * [Rotary Encoder][https://www.adafruit.com/product/377]

## Setup

The easiest way to setup is to clone the github repo onto the machine, build and install:
  1. Install dependencies:  `sudo apt-get install build-essential git python3-microdotphat python3-zmq libzmq3-dev`
  1. Clone the repository: `git clone https://github.com/cj8scrambler/nojoebuck.git`
  1. Build: `cd nojoebuck; make`
  1. Install: `sudo make install`
  1. Enable nojoebuck at boot: `sudo systemctl enable nojoebuck`
  1. Optional: Enable HW based UI at boot: `sudo systemctl enable njb-hw-ui`

## Usage

nojoebuck can be started as a systemd service.

nojoebuck [options]...
  -b, --bits=[16|24|32]  Bit depth.  Default: 16
  -c, --capture=NAME     Name of capture interface (list with aplay -L).  Default: default
  -h, --help             This usage message
  -m, --memory=SIZE      Memory buffer to reserve in MB.  Default: 32.0
  -p, --playback=NAME    Name of playback interface (list with aplay -L).  Default: default
  -r, --rate=RATE        Sample rate.  Default: 48000

