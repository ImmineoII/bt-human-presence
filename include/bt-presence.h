#ifndef _BT_PRESENCE_H
#define _BT_PRESENCE_H

#include <pthread.h>
#include <stdbool.h>

struct mqtt_pub_thread_args{
    pthread_t tid;
    bool* keepalive;
    int* presence;
};

void compute_presence(bool detected);


#endif
