//emailing based on https://curl.haxx.se/libcurl/c/smtp-tls.html
//details for notification emails
#define EMAIL_FROM "<aaron.frishling@gmail.com>"
//#define EMAIL_TO "<aaron.frishling+SecuritySystem@gmail.com>"
//#define EMAIL_CC "<aaron.frishling@gmail.com>"
//easiest way to use #define to set up an array. For each email adress, add them to the array and ignore the rest
#define CONFIG_EMAIL_TO_ARRAY const char *EMAIL_TO_ARRAY[] = {"<aaron.frishling+SecuritySystem@gmail.com>","<aaron.frishling@streetgeek.com.au>"}; const int EMAIL_TO_ARRAY_LEN = sizeof(EMAIL_TO_ARRAY) / sizeof(char*);
#define EMAIL_SUBJECT_PREFIX "[SSA]"
#define EMAIL_USER "aaron.frishling@gmail.com"
#define EMAIL_SERVER_URL "smtp://smtp.gmail.com:587"
//how often we check if we need to send emails and do so, in seconds
#define EMAIL_FREQUENCY 300
//the below is needed for threading to work right - send yourself an email with the subject prefix you're using, look at the email source, grab the Message-ID from there
#define EMAIL_THREAD_MESSAGE "<CADkgYnwTR65RHFx9AOm09eNieARKn=T3RLb=thei1zoBf1Jg3Q@mail.gmail.com>"

//details for detecting owner presence and network state
#define PING_MAX_FAILS 3
#define PING_USER_TARGET "192.168.1.4"
#define PING_CHECK_TARGET "192.168.1.1"
//how often we check for owner presence in seconds
#define PING_FREQUENCY 60
