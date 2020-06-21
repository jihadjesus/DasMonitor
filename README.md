# The DasMonitor monitoring project
Because I got sick of not having remote monitoring for my DAS 208L, I've hacked together something that gives it to me.
Hooks into the data and clk pins for the control panels in order to see do all of the things it needs to.

## Hardware
Currently runs on the Orange Pi Zero. Should be pretty straightforward to adapt to any other fruit Pi. Other platforms with fast enough GPIO should be possible. May need more effort to get it to work correctly on single-core systems.

### Hardware dependencies/customisation
Relies on the WiringPi library to let us bitbang the necessary pins fast enough. To support the Orange Pi Zero, needed to modify a fork of WiringPi for the Orange Pi (ifself forked from one for Banana Pi).
**more detail to come, including patches**

## Building
Gotta satisfy dependencies, configure the thing before you can build it
### dependencies
+ wiringpi: See Hardware
+ libcurl: For email
+ libpthread

### configuration
config.h for most important details
secrets.h for anything private (a device-specific password from google so I can send email). should be of form #define emailPassword "secret" and can always remain on target/build device.

### the thing
make does the job. Some bits in there to make it easier to push to my Pi and build

## running
Best run in a tmux session so you can detach. Must run as root for GPIO access. Logs only go to stdout and via the remote notification mechanism, so probably want to tee the output to a file for future inspection

## DAS 208L panel comms doco

There's a document under logs_notes where I record my through process of what happened from the start of work until I had somewhat funcitoning code

## experimental stuff
### testing performance - c
Technically all of the c code came after the python code, but it's more interesting because it's actually sufficiently performant
#### bitbangtest.c
The first c code for testing if I can bit bang fast enough on my Orange Pi Zero using WiringPi. Needed to be able to read two pins in under 30 usec (realistically closer to 10 so we could get multiple samples per period). Worked fast enough even when printing to output (redirecting stdout to a file took us from 25 usec a sample to 5).
####bitbanger.c
First attempt at synching to frame boundaries and printing them. Not really useful for much.
### testing performance - python
python code
### sample logs
A variety of these are under logs_notes. Mostly interesting if you want to attempt to analyse them yourself
#### Bus Pirate logs
Everything prefixed with SPI capture was gathered using a Bus Pirate and performing various actions. Had no way to learn the frames when I did this so I made a guess about where they started (which wasn't correct)
#### Framebanger logs
Prefixed with Framebanger capture, these are from an early frame-aware capture implementation that didn't know much aside from what the "normal" frame looks like and where in it to look for the triggered zone