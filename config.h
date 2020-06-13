//emailing based on https://curl.haxx.se/libcurl/c/smtp-tls.html
//details for notification emails
#define EMAIL_FROM "<aaron.frishling@gmail.com>"
#define EMAIL_TO "<aaron.frishling+SecuritySystem@gmail.com>"
#define EMAIL_SUBJECT "RE: [SSA]"
#define EMAIL_USER "aaron.frishling@gmail.com"
#define EMAIL_SERVER_URL "smtp://smtp.gmail.com:587"

//details for detecting owner presence and network state
#define PING_MAX_FAILS 3
#define PING_USER_TARGET "192.168.1.4"
#define PING_CHECK_TARGET "192.168.1.1"