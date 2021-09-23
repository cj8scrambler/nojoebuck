# nojoebuck

nojoebuck is an audio buffering program intended for use on Raspberry Pi
hardware.  It allows you to add delay to an audio source such as a radio
broadcast of a sporting event.  This makes it possible to sync the audio
from a radio broadcast to a streamed video of the same broadcast.

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
  1. Install dependencies:  `sudo apt-get install build-essential git alsa-utils python3-microdotphat python3-zmq libzmq3-dev`
  1. Clone the repository: `git clone https://github.com/cj8scrambler/nojoebuck.git`
  1. Build: `cd nojoebuck; make`
  1. Install: `sudo make install`
  1. Enable nojoebuck at boot: `sudo systemctl enable nojoebuck`
  1. Optional: Enable HW based UI at boot: `sudo systemctl enable njb-hw-ui`

## Usage

nojoebuck can be started as a systemd service.  Options can be configured in
`/etc/default/nojoebuck`.  The default values are listed there.
```
# Bit depth to capture at
#BITS=16 

# Sampling Rate
#RATE=48000 

# MB of Memory to reserve for buffer.  The determines the maximum delay
# possible.  With the default sampling depth/rate, the default of 32MB
# allows for up to 174.8 seconds of delay
#MEMORY=32 

# ALSA compatible playback interface name.  To see which interfaces are
available on your system run: aplay -L
#CAPTURE="default"

# ALSA compatible capture interface name.  To see which interfaces are
available on your system run: arecord -L
#CAPTURE="default"
```

## Background

In the fall of 2016 the Cubs were making a strong run through the playoffs.
Since it was post-seaons, the games were no longer handled by the local
broadcasters who knew the team and provided insightful commentary.  Instead
the games were on national broadcast where the commentators had nothing useful
to say.

In the old days, you could simply turn off the TV sound and turn on the radio
broadcast to solve this.  However now with streaming video, the delay between
radio and video is too great.  This is where nojoebuck comes in.  Put it inline
with the radio audio source and you can tune the delay to get it to match the
streaming video source.

## Todo
  * Use a radio tuner (or maybe SDR) as the audio source
  * Verify mixer settings at runtime
  * Move from discrete stretching steps to continuous
