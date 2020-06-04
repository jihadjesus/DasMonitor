#include <wiringPi.h>
#include <sys/time.h>
#include <stdio.h>
#include <stddef.h>

//PA14=23, PA15=19, PA16=21
#define pin_sck 23
#define pin_rx 21
#define pin_tx 19

int main (void)
{
  int vClock, vRx;
  long int lastUS;
  struct timeval tv;
  wiringPiSetupPhys () ;
  pinMode (pin_sck, INPUT) ;
  pinMode (pin_rx, INPUT) ;
  for (int i=0; i < 20; i++)
  {
    vClock = digitalRead (pin_sck) ;
    vRx = digitalRead (pin_rx) ;
    gettimeofday(&tv, NULL);
    printf("clock %d data %d at us %ld after us %ld\n", vClock, vRx, tv.tv_usec, tv.tv_usec - lastUS);
    lastUS = tv.tv_usec;
//    delay (10) ;

    //digitalWrite (0,  LOW) ; delay (500) ;
  }
  return 0 ;
}