#include <stdarg.h> //varargs

//externally-relevnt
#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_IMPORTANT 2
#define LOG_LEVEL_EMAIL 2
#define LOG_LEVEL_PRINTF 0

void logdata(int level, const char * format, ...);
void *commsEnablerFunc(void *vargp);
void *emailerFunc(void *vargp);



//internally relevant
//only need to travers the list in one direction, but need to be able to append. ALso need to add and clear list atomically
struct MessageQueue
{
    struct MessageQueue *next;
    char message[500]; //longer than a single message line will ever be
};
