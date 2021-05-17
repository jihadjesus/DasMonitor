#include <stdlib.h> //system()
#include <curl/curl.h> //smtp
#include <time.h> //regular-granularity time
#include <unistd.h>  //Header file for sleep(). man 3 sleep for details. 
#include <string.h> //string copy/cat
#include <pthread.h> //threading, mutexes

#include "logging.h"
#include "dasmonitor.h"
#include "config.h"
#include "secrets.h"

//set up our array of "to" email addresses and associated length
CONFIG_EMAIL_TO_ARRAY

pthread_mutex_t muLog = PTHREAD_MUTEX_INITIALIZER;
struct MessageQueue *pmqLogFirst = NULL;
struct MessageQueue *pmqLogLast = NULL;
int fEnableComms = 0;

//pretty self-explanaory
void logdata(int level, const char * format, ...)
{
    //init
    time_t rawtime;
    struct tm * timeinfo;
    struct MessageQueue *tmpMessage = malloc(sizeof(struct MessageQueue));
    //collect the time
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    int written = sprintf(tmpMessage->message, "[%d %d %d %d:%d:%d]", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    //collect the actual input
    va_list args;
    va_start (args, format);
    vsprintf (tmpMessage->message +written, format, args);
    va_end (args);
    if(level >= LOG_LEVEL_PRINTF) {
        printf(tmpMessage->message); //send to stdout
    }
    if(fEnableComms && level >= LOG_LEVEL_EMAIL) { //add the message to the email queue.
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

//this is how we detect whether we should be sending interesting log data somewhere other than stdout
//current implementation is to detect whether their phone is still connected to the home network
void *commsEnablerFunc(void *vargp)
{
    int result;
    static int cPingFails = 0;
    while(!done) {
        int sleepTime = PING_FREQUENCY;// check roughly every minute (will drift thanks to time a failing ping takes)
        while(!done &&(sleepTime >= 0)) {
            sleep(5);
            sleepTime -=5;
        }
        result = system("arping " PING_USER_TARGET " -I wlan0 -c 1 -w 3 > /dev/null");
        logdata(LOG_INFO, "Ping result: %s \n", (result == 0)?"present":"not");
        if(result && !fEnableComms){ //need to enable comms
            result = system("arping " PING_CHECK_TARGET " -I wlan0 -c 1 -w 3 > /dev/null")? 0: 1; //check router is up and we're connected to it to avoid being triggered by network issues
            if(result){
                cPingFails++; //takes multiple fails to enable output
                if(cPingFails >= PING_MAX_FAILS){
                    fEnableComms = 1;
                    logdata(LOG_IMPORTANT, "phone disappeared, enabling notifications. Probably %s output state %s\n", (frameLen == 0)?"good":"bad", sOutputs);
                }
            }
        }else if(!result && fEnableComms){//need to disable
            logdata(LOG_IMPORTANT, "phone reappeared, disabling notifications. Probably %s output state %s\n", (frameLen == 0)?"good":"bad", sOutputs);
            fEnableComms = 0;
            cPingFails = 0;
        }
        //other pairings of these flags don't require state trasition
    }
    return 0;
}

//emailing based on https://curl.haxx.se/libcurl/c/smtp-tls.html
//email header
int email_id = 0;
int email_day = -1;
static const char *email_header_text_p1 = 
  //"Date: Mon, 29 Nov 2010 21:54:29 +1100\r\n",
#ifdef EMAIL_TO 
  "To: " EMAIL_TO "\r\n" 
#endif
#ifdef EMAIL_CC
  "CC: " EMAIL_CC "\r\n" 
#endif
  "From: " EMAIL_FROM " (SSA)\r\n" 
  "Subject: " EMAIL_SUBJECT_PREFIX "%d %d\r\n" 
//  "Message-ID: SSA-m%d-d%d-i%d@opimonitor.local\r\n"
  "In-Reply-To: " EMAIL_THREAD_MESSAGE "\r\n"
  "Thread-Topic: " EMAIL_SUBJECT_PREFIX "\r\n"
  "\r\n"; /* empty line to divide headers from body, see RFC5322 */ 

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct MessageQueue *queue = (struct MessageQueue *)userp;

    if((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
        return 0;
    }

    if(queue->message[0]){ //email header not sent
        size_t len = strlen(queue->message);
        memcpy(ptr, queue->message, len);
        queue->message[0] = 0;
        return len;
    }
    
    if(queue->next == NULL){//all log entries sent
        free(queue);
        return 0;
    }

    //more message queue entries to get send through
    size_t len = strlen(queue->next->message);
    memcpy(ptr, queue->next->message, len);
    struct MessageQueue *remove = queue->next;
    queue->next = remove->next;
    free(remove);

    return len;
}

static size_t header_sink(char* b, size_t size, size_t nmemb, void *userp) {
    size_t numbytes = size * nmemb;
    logdata(LOG_LEVEL_EMAIL, "%.*s\n", numbytes, b);
    return numbytes;
}

//one of our ways to send out interesting logs - email the owner
void *emailerFunc(void *vargp)
{
    while(!done) {
        int sleepTime = EMAIL_FREQUENCY;// check roughly every 5 minutes
        struct MessageQueue *mFirst/*, *mLast*/, *mSend;
        while(!done &&(sleepTime >= 0)) {
            sleep(5);
            sleepTime -=5;
        }
        
        if(pmqLogFirst != NULL) { //if there's messages, remove them from the list ASAP and then prepare for sending
            pthread_mutex_lock(&muLog);
            mFirst = pmqLogFirst;
            //mLast = pmqLogLast;
            pmqLogFirst = NULL;
            pmqLogLast = NULL;
            pthread_mutex_unlock(&muLog);
            
            printf("sending log... ");
            
            //we can take our time sending them
            //build the message queue to send
            //time-related stuff first (headers for threading, subject)
            time_t rawtime;
            struct tm * timeinfo;
            time ( &rawtime );
            timeinfo = localtime ( &rawtime );
            if(timeinfo->tm_mday != email_day) {
                email_day = timeinfo->tm_mday;
                email_id = 0;
            }
            mSend = malloc(sizeof(struct MessageQueue));
            //strcpy(mSend->message, email_header_text);
            int len = sprintf(mSend->message, email_header_text_p1, timeinfo->tm_mon+1, timeinfo->tm_mday/*, timeinfo->tm_mon+1, timeinfo->tm_mday, email_id*/);
            
            email_id++;
            mSend->next = mFirst;

            //emailing code from https://curl.haxx.se/libcurl/c/smtp-tls.html
            CURL *curl;
            CURLcode res = CURLE_OK;
            struct curl_slist *recipients = NULL;
            curl = curl_easy_init();
            if(curl) {
                curl_easy_setopt(curl, CURLOPT_USERNAME, EMAIL_USER);
                curl_easy_setopt(curl, CURLOPT_PASSWORD, emailPassword);
 
                curl_easy_setopt(curl, CURLOPT_URL, EMAIL_SERVER_URL);
                curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);
 
                curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
                for(int i=0; i<EMAIL_TO_ARRAY_LEN; ++i){
                    recipients = curl_slist_append(recipients, EMAIL_TO_ARRAY[i]);
                }
#ifdef EMAIL_TO 
                recipients = curl_slist_append(recipients, EMAIL_TO);
#endif
#ifdef EMAIL_CC
                recipients = curl_slist_append(recipients, EMAIL_CC);
#endif
                curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
                
                curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
                //curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_sink);
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
