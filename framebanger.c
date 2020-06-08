#include <wiringPi.h> //hw access
#include <sys/time.h> //usec-granularity time
#include <time.h> //regular-granularity time
#include <stdio.h> //io
#include <stddef.h>
#include <signal.h>  //signal handling
#include <stdarg.h>
#include <unistd.h>  //Header file for sleep(). man 3 sleep for details. 
#include <pthread.h> //threading
#include <stdlib.h> //system()
#include <curl/curl.h> //smtp   
#include <string.h> //string copy/cat


//PA14=23, PA15=19, PA16=21
#define pin_sck 23
#define pin_rx 21
#define pin_tx 19

struct MessageQueue
{
    struct MessageQueue *next;
    char message[500]; //longer than a message will ever be
};
pthread_mutex_t muLog = PTHREAD_MUTEX_INITIALIZER;
struct MessageQueue *pmqLogFirst = NULL;
struct MessageQueue *pmqLogLast = NULL;
int fEnableComms = 0;

void logdata(const char * format, ...)
{
    //print the time
    time_t rawtime;
    struct tm * timeinfo;
    struct MessageQueue *tmpMessage = malloc(sizeof(struct MessageQueue));
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    int written = sprintf(tmpMessage->message, "[%d %d %d %d:%d:%d]", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    //printf the actual input
    va_list args;
    va_start (args, format);
    vsprintf (tmpMessage->message +written, format, args);
    va_end (args);
    printf(tmpMessage->message);
    if(fEnableComms) { //add the message to the print queue.
        pthread_mutex_lock(&muLog);
        if(pmqLogFirst == NULL) {
            pmqLogFirst = tmpMessage;
        }else{
            pmqLogLast->next = tmpMessage;
        }
        pmqLogLast = tmpMessage;
        tmpMessage->next = NULL;
        pthread_mutex_unlock(&muLog);
    }else{
        free(tmpMessage);
    }
}

//die nicely
int done = 0;
void sigintHandler(int sig_num) 
{ 
    done = 1;
    logdata("dying now\n");
} 

//this is how we detect whether we should be sending interesting log data somewhere other than stdout
//current implementation is to detect whether their phone is still connected to the home network
void *commsEnablerFunc(void *vargp)
{
    int result;
    while(!done) {
        int sleepTime = 60;// check roughly every minute (will drift thanks to time a failing ping takes
        while(((sleepTime = sleep(sleepTime)) != 0) && !done) {}
        result = system("ping 192.168.1.4 -c 1");
        if(result && !fEnableComms){ //need to enable comms
            result = system("ping 192.168.1.1 -c 1")? 0: 1; //check router is up and we're connected to it
            if(result){
                fEnableComms = 1;
                logdata("phone disappeared, enabling notifications\n");
            }
        }else if(!result && fEnableComms){
            logdata("phone reappeared, disabling notifications\n");
            fEnableComms = 0;
        }
    }
    return 0;
}

//emailing based on https://curl.haxx.se/libcurl/c/smtp-tls.html
#define FROM    "<aaron.frishling@gmail.com>"
#define TO      "<aaron.frishling+SecuritySystem@gmail.com>"
 
static const char *payload_text = 
  //"Date: Mon, 29 Nov 2010 21:54:29 +1100\r\n",
  "To: " TO "\r\n" 
  "From: " FROM " (Example User)\r\n" 
  //~ "Message-ID: <dcd7cb36-11db-487a-9f3a-e652a9458efd@"
  //~ "rfcpedant.example.org>\r\n",
  "Subject: Security system alert\r\n" 
  "\r\n"; /* empty line to divide headers from body, see RFC5322 */ 

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct MessageQueue *queue = (struct MessageQueue *)userp;

    if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
        return 0;
    }

    if(queue->message[0]){ //header not sent
        size_t len = strlen(queue->message);
        memcpy(ptr, queue->message, len);
        queue->message[0] = 0;
        return len;
    }
    
    if(queue->next == NULL){//all entries sent
        free(queue);
        return 0;
    }

    //more message queue entries to get through
    size_t len = strlen(queue->next->message);
    memcpy(ptr, queue->next->message, len);
    struct MessageQueue *remove = queue->next;
    queue->next = remove->next;
    free(remove);

    return len;
}
 

//one of our ways to send out interesting logs - email the owner
void *emailerFunc(void *vargp)
{
    while(!done) {
        int sleepTime = 60;// check roughly every minute
        struct MessageQueue *mFirst, *mLast, *mSend;
        
        while(((sleepTime = sleep(sleepTime)) != 0) && !done) {}
        
        if(pmqLogFirst != NULL) { //if there's messages, remove them from the list ASAP
            pthread_mutex_lock(&muLog);
            mFirst = pmqLogFirst;
            mLast = pmqLogLast;
            pmqLogFirst = NULL;
            pmqLogLast = NULL;
            pthread_mutex_unlock(&muLog);
            
            printf("sending log... ");
            
            //and then we can take our time sending them
            //build the message queue to send
            mSend = malloc(sizeof(struct MessageQueue));
            strcpy(mSend->message, payload_text);
            mSend->next = mFirst;
            
            //to be implemented

            CURL *curl;
            CURLcode res = CURLE_OK;
            struct curl_slist *recipients = NULL;
            curl = curl_easy_init();
            if(curl) {
                curl_easy_setopt(curl, CURLOPT_USERNAME, "aaron.frishling@gmail.com");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, "");
 
                curl_easy_setopt(curl, CURLOPT_URL, "smtp://smtp.gmail.com:587");
                //~ curl_easy_setopt(curl, CURLOPT_URL, "smtp://aspmx.l.google.com:587");
                curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
 
                //~ curl_easy_setopt(curl, CURLOPT_CAINFO, "/home/adf/DasMonitor/GTSR2.crt");
                curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
 
                recipients = curl_slist_append(recipients, TO);
                curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
                
                curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
                curl_easy_setopt(curl, CURLOPT_READDATA, mSend);
                curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
                //~ curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
     
                res = curl_easy_perform(curl);
                if(res != CURLE_OK) { fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res)); }
                curl_slist_free_all(recipients);
                curl_easy_cleanup(curl);
            }
            printf("... complete/n");
        }
    }
    return 0;
}

#define BYTES_PER_FRAME 6
#define MAX_FRAME_BYTES BYTES_PER_FRAME +4
//frame guard could probably be 150-200us, as long as it's a bit over 1/2 a cycle
#define FRAME_GUARD_US 300
int frame[MAX_FRAME_BYTES];
int frameLen;
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

// wait until new frame starts, grab it, return if apparently received OK
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

void copyFrame(int dst[], int src[])
{
    for(int i=0;i<MAX_FRAME_BYTES;i++){
        dst[i]=src[i];
    }
}

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

char button2toChar(int button)
{
    if(button < 1 || button >12) return 'x';
    if(button == 10) return  '*';
    if(button == 11) return  '0';
    if(button == 12) return  '#';
    return (char)(((int)'0') + button);
}

char sZones[9]; 

char zone2Char(int zone)
{
    for(int i = 1; i < 9; i++) {
        if(zone & 0x80) {
            return (char)(((int)'0') + i);
        } 
        zone <<= 1;
    }
    return 'x';
}

void zones2String (int zones)
{
    for(int i = 0; i < 8; i++) {
        if(zones & 0x80) {
            sZones[i] = (char)(((int)'0') + i+1);
        } else {
            sZones[i] = '-';
        }
        zones <<= 1;
    }
    sZones[9] = '\0';
}

char sOutputs[17];

void outputs2String (int outputs)
{
    int outIdx = 0;
    for(; outIdx < 4; outIdx++) { //4 bits I don't know
        if(outputs & 0x80) {
            sOutputs[outIdx] = '?';
        } else {
            sOutputs[outIdx] = '-';
        }
        outputs <<= 1;
    }
    sOutputs[14] = outputs & 0x80 ? 'a' : '-'; //either armed or partial lights?
    outputs <<= 1;
    for(; outIdx < 13; outIdx++) { //8 zones
        if(outputs & 0x80) {
            sOutputs[outIdx] = (char)(((int)'0') + outIdx-4);
        } else {
            sOutputs[outIdx] = '-';
        }
        outputs <<= 1;
    }
    sOutputs[14] = outputs & 0x80 ? 's' : '-'; //secure
    outputs <<= 1;
    sOutputs[15]= outputs & 0x80 ? 'o' : '-'; // unknown, but probably AC ON
    outputs <<= 1;
    sOutputs[16]= outputs & 0x80 ? '-' : 't'; // tone/ringer, inverted output
    sOutputs[17] = '\0';
}


int unknownBytes[] = {0xff, 0xfe, 0xff, 0xff};

int main (void)
{
    //init
    //~ long lastTime;
    //~ struct timeval tv;
    int lastFrame[] = {0xff, 0xfe, 0xff, 0xff, 0x0, 0x7, 0x0, 0x0, 0x0, 0x0};
    int triggeredZones = 0, fTriggered = 0, fArmed=0;
    pthread_t tCommsEnabler, tEmailer; 
    signal(SIGINT, sigintHandler); 
    pthread_create(&tCommsEnabler, NULL, commsEnablerFunc, NULL); 
    pthread_create(&tEmailer, NULL, emailerFunc, NULL); 
    wiringPiSetupPhys () ;
    pinMode (pin_sck, INPUT) ;
    pinMode (pin_rx, INPUT) ;
    logdata("let's go?\n");
    while (!done) {
        int result = getNextFrame();
        int fBadFrame = 0;
        if(result == 8 * BYTES_PER_FRAME) { // currently known good length
            int buttons1 = (frame[0] << 8) | frame[1]; 
            int buttons2 = (frame[2] << 8) | frame[3];
            int outputs = (frame[4] << 8) | frame[5]; //seem to be the parts with data going to the keypads
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
                            logdata("button '%c' pressed according to field 2, %s with field 1\n", button2toChar(digit), fBadFrame? "disagrees" : "agrees");
                        }
                    }
                }
            } else { //only check zone state when buttons aren't being pressed
                if((outputs & 0xf000) != 0x0000) {
                    fBadFrame = 1;
                } else {
                    if(outputs & 0x800 & !fArmed) {
                        logdata("system has been armed\n");
                        fArmed = 1;
                    } else {
                        if(fArmed) {
                            logdata("returning from armed\n");
                            fArmed = 0;
                        }
                        if(outputs & 4) { //secure
                            if(fTriggered){
                                zones2String(triggeredZones);
                                logdata("returned to secure, all zones triggered %s\n", sZones);
                                triggeredZones = 0;
                                fTriggered = 0;
                            }
                        } else { //somethign happening
                            if(!fTriggered) {
                                logdata("zones are getting triggered\n");
                                fTriggered = 1;
                            }
                            int newTriggered = 0xff & (outputs >> 3);
                            if((newTriggered & triggeredZones) != newTriggered) {
                                logdata(" triggered new zone %c\n", zone2Char((newTriggered & triggeredZones) ^ newTriggered));
                                triggeredZones|=newTriggered;
                            }
                        }
                    }
                }
            }
        } else {
            fBadFrame = 1;
        }
        
        //~ if(!framesEqual(frame, frameInv, result >> 3)) {
            //~ printf("%d bits, %d bits, up: %x %x %x %x %x %x %x %x %x %x down: %x %x %x %x %x %x %x %x %x %x\n", frameLen, frameInvLen, frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9], frameInv[0], frameInv[1], frameInv[2], frameInv[3], frameInv[4], frameInv[5], frameInv[6], frameInv[7], frameInv[8], frameInv[9]);
        //~ }
        //~ if(!framesEqual(frame, framePre, result >> 3)) {
            //~ printf("up: %d bits, post: %x %x %x %x %x %x %x %x %x %x pre: %x %x %x %x %x %x %x %x %x %x\n", frameLen, frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9], framePre[0], framePre[1], framePre[2], framePre[3], framePre[4], framePre[5], framePre[6], framePre[7], framePre[8], framePre[9]);
        //~ }
        
        //~ if(!framesEqual(frameInv, frameInvPre, result >> 3)) {
            //~ printf("down: %d bits, post: %x %x %x %x %x %x %x %x %x %x pre: %x %x %x %x %x %x %x %x %x %x\n", frameInvLen, frameInv[0], frameInv[1], frameInv[2], frameInv[3], frameInv[4], frameInv[5], frameInv[6], frameInv[7], frameInv[8], frameInv[9], frameInvPre[0], frameInvPre[1], frameInvPre[2], frameInvPre[3], frameInvPre[4], frameInvPre[5], frameInvPre[6], frameInvPre[7], frameInvPre[8], frameInvPre[9]);
        //~ }
        
        if(fBadFrame) {
            logdata("%d bits, unrecognised data in frame: %x %x %x %x %x %x %x %x %x %x\n", result, frame[0], frame[1], frame[2], frame[3], frame[4], frame[5], frame[6], frame[7], frame[8], frame[9]);
            copyFrame(lastFrame, frame);
        }
        
        //    delay (10) ; - this is in ms, not us
    }
    
    pthread_join(tCommsEnabler, NULL); 
    pthread_join(tEmailer, NULL); 
    return 0 ;
}