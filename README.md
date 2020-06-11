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
Best run in a tmux session so you can detach. Logs only go to stdout and via the remote notification mechanism, so probably want to tee the output to a file for future inspection

## DAS 208L panel comms doco

## experimental stuff
### testing performance - c
bitbang.c
bitbanger.c
### testing performance - python
python code
### sample logs