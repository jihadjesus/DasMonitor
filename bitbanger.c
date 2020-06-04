#include <wiringPi.h>
#include <sys/time.h>
#include <stdio.h>
#include <stddef.h>
#include <signal.h> 


//PA14=23, PA15=19, PA16=21
#define pin_sck 23
#define pin_rx 21
#define pin_tx 19

int done = 0;

void sigintHandler(int sig_num) 
{ 
    done = 1;
    printf("dying now\n");
} 

void resynch()
{
    int vClock;
    long int tLastHigh;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    tLastHigh = tv.tv_usec;
    while (tv.tv_usec - tLastHigh < 550) { // about 2 clock cycles, shouldn't be a glitch at this point
        vClock = digitalRead (pin_sck);
        gettimeofday(&tv, NULL);
        if(vClock) {
            tLastHigh = tv.tv_usec;
        } 
    }
}

int getNextBit()
{
    int vClockLast = 1;
    int vClock = digitalRead (pin_sck) ; 
    while (!vClock || (vClock == vClockLast)) {
        vClockLast = vClock;
        vClock = digitalRead (pin_sck);
    }
    int vRx = digitalRead (pin_rx);
    return vRx;
}

int getNextByte()
{
    int byte = 0;
    for(int i = 0; i < 8; i++){
        int bit = getNextBit();
        byte = (byte << 1) | bit;
    }
    return byte;
}

int main (void)
{
    //init
    long lastTime;
    struct timeval tv;
    signal(SIGINT, sigintHandler); 
    wiringPiSetupPhys () ;
    pinMode (pin_sck, INPUT) ;
    pinMode (pin_rx, INPUT) ;
    resynch();
    printf("resynch complete \n");
    
    while (!done) {
        int packet[6];
        for(int i=0; i<6; i++){
            packet[i] = getNextByte();
        }
        gettimeofday(&tv, NULL);
        //~ printf("byte %x at us %ld after %ld \n", byte, tv.tv_usec, tv.tv_usec - lastTime);
        printf("packet: %x %x %x %x %x %x\n", packet[0], packet[1], packet[2], packet[3], packet[4], packet[5]);
        lastTime = tv.tv_usec;
        //    delay (10) ; - this is in ms, not us
    }
    return 0 ;
}