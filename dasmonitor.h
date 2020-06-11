

//externally-relevant
int done; //signal to die nicely

//using static allocation for some things, because easier
char sOutputs[17]; //string representing current state of keypad display. May suffer from "tearing" if read when processing thread updates it, but pretty unlikely
char sZones[9]; //string representing current state of zones. same caveat as above




//internally-relevant
//the data pins we're using on the OPi, PA14=23, PA15=19, PA16=21
#define pin_sck 23
#define pin_rx 21
#define pin_tx 19

#define BYTES_PER_FRAME 6
#define MAX_FRAME_BYTES BYTES_PER_FRAME +4
//frame guard could probably be 150-200us, as long as it's a bit over 1/2 a cycle
#define FRAME_GUARD_US 300
int frame[MAX_FRAME_BYTES];
int frameLen;
