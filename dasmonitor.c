#include <wiringPi.h> //hw access
#include <sys/time.h> //usec-granularity time
#include <stdio.h> //io
#include <stddef.h>
#include <signal.h>  //signal handling
#include <pthread.h> //threading, mutexes
//#include "secrets.h" //put passwords in a header file until we can be bothered parsing config files

#include "logging.h"
#include "dasmonitor.h"
#include "config.h"

//die nicely
void sigintHandler(int sig_num) 
{ 
    done = 1;
    logdata(LOG_IMPORTANT, "DasMonitor shutting down\n");
} 

//returns the ascii char for a button from a number
char button2toChar(int button)
{
    if(button < 1 || button >12) return 'x';
    if(button == 10) return  '*';
    if(button == 11) return  '0';
    if(button == 12) return  '#';
    return (char)(((int)'0') + button);
}

#define HIGH_BIT_INT8(x) (x & 0x80)
//returns the ascii char for a zone from the bitmask format. If more than one, returns the lowest-numbered
char zone2Char(int zone)
{
    for(int i = 1; i < 9; i++) {
        if(HIGH_BIT_INT8(zone)) {
            return (char)(((int)'0') + i);
        } 
        zone <<= 1;
    }
    return 'x';
}

//converts all zone lights to a nice bitmask
void zones2String (int zones)
{
    for(int i = 0; i < 8; i++) {
        if(HIGH_BIT_INT8(zones)) {
            sZones[i] = (char)(((int)'0') + i+1);
        } else {
            sZones[i] = '-';
        }
        zones <<= 1;
    }
    sZones[9] = '\0';
}

#define HIGH_BIT_INT16(x) (x & 0x8000)
//converts all "outputs" from the main system board (zones, lights) as a nice bitmask
void outputs2String (int outputs)
{
    int outIdx = 0;
    for(; outIdx < 4; outIdx++) { //4 bits I don't know
        if(HIGH_BIT_INT16(outputs)) {
            sOutputs[outIdx] = '?';
        } else {
            sOutputs[outIdx] = '-';
        }
        outputs <<= 1;
    }
    sOutputs[outIdx++] = HIGH_BIT_INT16(outputs) ? 'a' : '-'; //bit 4 - either armed or partial lights?
    outputs <<= 1;
    for(; outIdx < 13; outIdx++) { //bits 5-12, 8 zones
        if(HIGH_BIT_INT16(outputs)) {
            sOutputs[outIdx] = (char)(((int)'0') + outIdx-4);
        } else {
            sOutputs[outIdx] = '-';
        }
        outputs <<= 1;
    }
    sOutputs[13] = HIGH_BIT_INT16(outputs) ? 's' : '-'; //secure
    outputs <<= 1;
    sOutputs[14]= HIGH_BIT_INT16(outputs) ? 'o' : '-'; // unknown, but probably AC ON
    outputs <<= 1;
    sOutputs[15]= HIGH_BIT_INT16(outputs) ? '-' : 't'; // tone/ringer, inverted output
    sOutputs[16] = '\0';
}

//is there something different about up/down clock? Would make sense for one to be master and the other slave
//interestingly, with just recording down signal at "idle" we have interesting phenomena:
// - each time we start a logging run, we get different output from down. It's like we're consistently out of synch by the same amount since it doesn't change within captures, only between them (and there's some patterns that seem to come up multiple times)
// - the down pattern alternates between 2 different ones, kind of like there's 2 devices taking turns. especially weird as the LED state only seems to come every other down
//this is all a bit weird, since there's no doco suggesting the system gets programmed to handle additional panels or that the panels need configuration 
// for a messy log:
//triggering zones does seem to get about 1 bit flipped in the down for every one in the up. FInally noticed some changes in the pattern over time with this
//But pressing buttons on a keypad didn't seem to do anything consistent, sometimes maybe a single bit flip that stays until after the press (so I guess timing changed)
//for one tht started as all zeros on the down
//once triggering started, one would be the triggered zone or 0, sometimes at the same time as up, sometimes opposite. other message would be either 0 or same as up but with zone flipped compared to the blinking
//after some events, mostly stayed similar at idle, but sometimes picked up bit flips in the same part of the frame in both versions of the up/down messages
//interestingly, situations where frames weren't getting received correctly (either unrecognised data or button press data differing) has also no real effect on these messages
//Makes me pretty confident these are something weird about syncing frames, although maybe I'd get a different result if I atomically collected the data and clock bits
int frameInv[MAX_FRAME_BYTES];
int frameInvLen; 
// maybe I was a numpty - if I'm not grabbing the data pin simultaneously with the clock, probably better to grab it beforehand
// on inspection, it did not matter - we get the same data being read for clock returning to 0 either side (meaning it's just garbage) and get similar kinds of garbage on clock going high as we do from any time reading the down track
int framePre[MAX_FRAME_BYTES];
int frameInvPre[MAX_FRAME_BYTES];

// wait until new frame starts, grab it, return amount of bits collected. Frames are delimited by time since there aren't any stop/start bits and this way we can handle random delays
int getNextFrame()
{
    //init
    int vClock, vClockLast=0, vData, vDataPre, cBits=-1;
    long int tLastHigh, tDiff;
    struct timeval tv;
    for(int i=0; i<MAX_FRAME_BYTES; i++){
        frame[i]=0;
    }
    gettimeofday(&tv, NULL);
    tLastHigh = tv.tv_usec;
    frameLen = 0;
    frameInvLen = 0;
    //main work
    while(1) {
        //loop-relevant updates
        vDataPre = digitalRead(pin_rx);
        vClock = digitalRead(pin_sck);
        vData = digitalRead(pin_rx);
        gettimeofday(&tv, NULL);
        //time-based things
        tDiff = tv.tv_usec - tLastHigh;
        while(tDiff < 0) {tDiff += 1000000;} //handle wrap-around and unexpected delays
        if(tDiff > FRAME_GUARD_US) {
            if(cBits > 0) { //a whole frame has passed time-wise. Exit whether or not we have a whole frame
                break;
            } else {
                cBits=0; // initial guard interval complete
            }
        }
        //we're in receive mode, do the thing
        if(cBits >= 0) { 
            //interesting things happen on clock-transition
            if(vClock != vClockLast) {
                if(vClock) { //rising edge, relevant for receiving
                    int iBit = 7 - (cBits & 7); //MSB first
                    int iByte = cBits >> 3;
                    frame[iByte]^= vData << iBit;
                    framePre[iByte]^= vDataPre << iBit;
                    cBits++;
                    if(cBits == 8 * (MAX_FRAME_BYTES)) { //frame buffer size reached
                        break;
                    } 
                } else { //falling edge, currently means nothing (maybe for transmit?)
                    //record downwards bit, let's see if they're different. Down comes after up...
                    int iBit = 7 - (frameInvLen & 7); //MSB first
                    int iByte = frameInvLen >> 3;
                    frameInv[iByte]^= vData << iBit;
                    frameInvPre[iByte]^= vDataPre << iBit;
                    frameInvLen++;
                }
            }
        }
        //update cycle-relevant data
        vClockLast = vClock;
        if(vClock) {
            tLastHigh = tv.tv_usec;
        }
    }
    //should probably do more verification, but for now confirm we've received correct number of bits
    //~ printf("cbits %d, delay %ld, ", cBits, tLastHigh - tv.tv_usec);
    //~ return cBits == 8 * BYTES_PER_FRAME;
    frameLen = cBits;
    return cBits;
}

//copy the contents of a data frame
void copyFrame(int dst[], int src[])
{
    for(int i=0;i<MAX_FRAME_BYTES;i++){
        dst[i]=src[i];
    }
}

//ocasionally useful utility func - compare prefixes of received data frames
int framesEqual(int left[], int right[], int length)
{
    if(length > MAX_FRAME_BYTES) length = MAX_FRAME_BYTES;
    for(int i=0;i<length;i++){
        if(left[i]!=right[i]) {
            return 0;
        }
    }
    return 1;
}


int main (void)
{
    //init everything
    int lastFrame[] = {0xff, 0xfe, 0xff, 0xff, 0x0, 0x7, 0x0, 0x0, 0x0, 0x0};
    int triggeredZones = 0, fTriggered = 0, fArmed=0;
    pthread_t tCommsEnabler, tEmailer; 
    done = 0;
    signal(SIGINT, sigintHandler); 
    pthread_create(&tCommsEnabler, NULL, commsEnablerFunc, NULL); 
    pthread_create(&tEmailer, NULL, emailerFunc, NULL); 
    wiringPiSetupPhys () ;
    pinMode (pin_sck, INPUT) ;
    pinMode (pin_rx, INPUT) ;
    logdata(LOG_IMPORTANT, "DasMonitor starting up\n");
    //main processing loop - all of our frame-related code is here
    while (!done) {
        int result = getNextFrame();
        int fBadFrame = 0;
        if(result == 8 * BYTES_PER_FRAME) { // currently known good length
            int buttons1 = (frame[0] << 8) | frame[1]; 
            int buttons2 = (frame[2] << 8) | frame[3];
            int outputs = (frame[4] << 8) | frame[5]; //seem to be the parts with data going to the keypads
            outputs2String(outputs);
            if(!(buttons1 == 0xfffe && buttons2 == 0xffff)) { // buttons being pressed
                if(((buttons1 & 0x003f) !=0x003e) || ((buttons2 & 0x8007) != 0x8007)){// check unknown bits are in their common state
                    fBadFrame = 1;
                }  else { //parse out which ones are pressed, check for consistency
                    int buttons2tmp = ~buttons2;
                    for(int digit = 1; digit < 13; digit++){
                        buttons2tmp <<= 1;
                        if(buttons2tmp & 0x8000){
                            int shift = digit < 4 ? 0 : digit -3;
                            int buttons1tmp = (0x7fff >> shift) & 0xfffe;
                            fBadFrame = buttons1 != buttons1tmp;
                            logdata(LOG_IMPORTANT, "button '%c' pressed according to field 2, %s with field 1\n", button2toChar(digit), fBadFrame? "disagrees" : "agrees");
                        }
                    }
                }
            } else { //only check zone state when buttons aren't being pressed
                if((outputs & 0xf000) != 0x0000) {
                    fBadFrame = 1;
                } else {
                    if(outputs & 0x800 & !fArmed) {
                        logdata(LOG_IMPORTANT, "system has been armed\n");
                        fArmed = 1;
                    } else {
                        if(fArmed) {
                            logdata(LOG_IMPORTANT, "returning from armed\n");
                            fArmed = 0;
                        }
                        if(outputs & 4) { //secure
                            if(fTriggered){
                                zones2String(triggeredZones);
                                logdata(LOG_IMPORTANT, "returned to secure, all zones triggered %s\n", sZones);
                                triggeredZones = 0;
                                fTriggered = 0;
                            }
                        } else { //somethign happening
                            if(!fTriggered) {
                                logdata(LOG_IMPORTANT, "zones are getting triggered\n");
                                fTriggered = 1;
                            }
                            int newTriggered = 0xff & (outputs >> 3);
                            if((newTriggered & triggeredZones) != newTriggered) {
                                logdata(LOG_IMPORTANT, " triggered new zone %c\n", zone2Char((newTriggered & triggeredZones) ^ newTriggered));
                                triggeredZones|=newTriggered;
                            }
                        }
                    }
                }
            }
        } else {
            fBadFrame = 1;
        }
        
        //various experimental bits - trying to figure out if I should sample/transmit with slightly different timing relative to clk (thanks to data and clk not readint at same time)
        //~ if(!framesEqual(frame, frameInv, result >> 3)) {
            //~ printf("%d bits, %d bits, up: %x %x %x %x %x %x %x %x %x %x down: %x %x %x %x %x %x %x %x %x %x\n", frameLen, frameInvLen, frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9], frameInv[0], frameInv[1], frameInv[2], frameInv[3], frameInv[4], frameInv[5], frameInv[6], frameInv[7], frameInv[8], frameInv[9]);
        //~ }
        //~ if(!framesEqual(frame, framePre, result >> 3)) {
            //~ printf("up: %d bits, post: %x %x %x %x %x %x %x %x %x %x pre: %x %x %x %x %x %x %x %x %x %x\n", frameLen, frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9], framePre[0], framePre[1], framePre[2], framePre[3], framePre[4], framePre[5], framePre[6], framePre[7], framePre[8], framePre[9]);
        //~ }
        
        //~ if(!framesEqual(frameInv, frameInvPre, result >> 3)) {
            //~ printf("down: %d bits, post: %x %x %x %x %x %x %x %x %x %x pre: %x %x %x %x %x %x %x %x %x %x\n", frameInvLen, frameInv[0], frameInv[1], frameInv[2], frameInv[3], frameInv[4], frameInv[5], frameInv[6], frameInv[7], frameInv[8], frameInv[9], frameInvPre[0], frameInvPre[1], frameInvPre[2], frameInvPre[3], frameInvPre[4], frameInvPre[5], frameInvPre[6], frameInvPre[7], frameInvPre[8], frameInvPre[9]);
        //~ }
        
        //and log if we had any issues with the frame for future reference
        if(fBadFrame) {
            logdata(LOG_DEBUG, "%d bits, unrecognised data in frame: %x %x %x %x %x %x %x %x %x %x\n", result, frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9]);
            copyFrame(lastFrame, frame);
        }
        
        //    delay (10) ; - this is in ms, not us
    }
    
    pthread_join(tCommsEnabler, NULL); 
    pthread_join(tEmailer, NULL); 
    return 0 ;
}