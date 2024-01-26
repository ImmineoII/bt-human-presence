#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <sys/syslog.h>
#include <signal.h>
#include <string.h>
#include <mosquitto.h>
#include <pthread.h>
#include "include/bt-presence.h"

#ifdef CONFIG_TARGET_MAC_ADDRESS
    #define TARGET_MAC_ADDRESS CONFIG_TARGET_MAC_ADDRESS
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_MQTT_BROKER_IP
    #define MQTT_BROKER_IP CONFIG_MQTT_BROKER_IP
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_MQTT_USERNAME
    #define MQTT_USERNAME CONFIG_MQTT_USERNAME
#else
    #error "missing configuration: "run make menuconfig"" 
#endif

#ifdef CONFIG_MQTT_PASSWORD
    #define MQTT_PASSWORD CONFIG_MQTT_PASSWORD
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_MQTT_BROKER_PORT
    #define MQTT_BROKER_PORT CONFIG_MQTT_BROKER_PORT
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_MQTT_PRESENCE_TOPIC
    #define MQTT_PRESENCE_TOPIC CONFIG_MQTT_PRESENCE_TOPIC
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_MQTT_PUBLISH_INTERVAL
    #define MQTT_PUBLISH_INTERVAL CONFIG_MQTT_PUBLISH_INTERVAL
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_PRESENCE_HYSTERESIS
    #define PRESENCE_HYSTERESIS CONFIG_PRESENCE_HYSTERESIS
#else
    #error "missing configuration: run make menuconfig" 
#endif

#ifdef CONFIG_DETECTION_INTERVAL
    #define DETECTION_INTERVAL CONFIG_DETECTION_INTERVAL
#else
    #error "missing configuration: run make menuconfig" 
#endif

int presence = 0;
int sock;
struct mosquitto *mosq = NULL;
bool keepalive = true;
struct mqtt_pub_thread_args* mosq_pub_thread_args;

void mosq_log_callback(struct mosquitto *mosq, void *userdata, int level, const char *str)
{   
    switch (level) {
        case MOSQ_LOG_DEBUG:
            syslog(LOG_DEBUG, "mosq: %s", str);
            break;
        case MOSQ_LOG_ERR: 
            syslog(LOG_ERR, "mosq: %s", str);
            break;
        default:
            syslog(LOG_DEBUG, "mosq: %s", str);
    }
}

void mqtt_setup(){

    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    if(!mosq){
            syslog(LOG_ERR, "unable to create new instance\n");
        }
    
    mosquitto_log_callback_set(mosq, mosq_log_callback);
    
    mosquitto_username_pw_set(mosq, MQTT_USERNAME, MQTT_PASSWORD);
    if(mosquitto_connect(mosq, MQTT_BROKER_IP, MQTT_BROKER_PORT, 120)){
            syslog(LOG_ERR, "unable to connect\n");
        }
    int loop = mosquitto_loop_start(mosq);
    if(loop != MOSQ_ERR_SUCCESS){
        syslog(LOG_ERR, "unable to start loop: %i\n", loop);
    }
}

static void graceful_stop(int signum){

    keepalive = false;

    syslog(LOG_DEBUG, "caught signal %d, exiting", signum);

    // Chiusura del socket HCI
    if (hci_close_dev(sock) < 0) {
        syslog(LOG_ERR, "error closing HCI socket");
    }

    pthread_join(mosq_pub_thread_args->tid,NULL);
    free(mosq_pub_thread_args);
    closelog();
    exit(EXIT_SUCCESS);
}

static void daemonize(){
    pid_t pid;

    pid = fork();

    //exit on error
    if(pid < 0){
        syslog(LOG_DEBUG, "fork #1 failed");
        exit(EXIT_FAILURE);
    }
    syslog(LOG_DEBUG, "fork #1 done");

    //stop parent
    if(pid > 0){
        syslog(LOG_DEBUG, "stop parent #1");
        exit(EXIT_SUCCESS);
    }

    //change session id exit on failure
    if(setsid() < 0){
        syslog(LOG_DEBUG, "set SID failed");
        exit(EXIT_FAILURE);
    }
    syslog(LOG_DEBUG, "set SID done");

    //fork again to deamonize
    pid = fork();

    //exit on error
    if(pid < 0){
        syslog(LOG_DEBUG, "fork #2 failed");
        exit(EXIT_FAILURE);
    }
    syslog(LOG_DEBUG, "fork #2 done");

    //stop parent
    if(pid > 0){
        syslog(LOG_DEBUG, "stop parent #2");
        exit(EXIT_SUCCESS);
    }

    //add signal handlers again
    signal(SIGINT, graceful_stop);
    signal(SIGTERM, graceful_stop);
    syslog(LOG_DEBUG, "registered daemon signal handler");
}

void compute_presence(bool detected){
    if(detected){

        if(presence < 0){
            presence = 0;
        }else {
            presence = ++presence > PRESENCE_HYSTERESIS ? PRESENCE_HYSTERESIS : presence;
        }

    }else{

        presence = --presence < -PRESENCE_HYSTERESIS ? -PRESENCE_HYSTERESIS : presence;

    }
        syslog(LOG_DEBUG, "presence is now %d", presence);
}

static void* mqtt_publisher_thread(void* args){

    syslog(LOG_DEBUG, "start publisher thread");
    
    struct mqtt_pub_thread_args* t_args = (struct mqtt_pub_thread_args *) args;
    char msg[256] = {0};
    int * presence_p = t_args->presence;
    while(*t_args->keepalive){

        syslog(LOG_DEBUG, "publisher thread running");
        memset(msg, 0, sizeof(msg));
        syslog(LOG_DEBUG, "presence in thread is %d", *presence_p);
        sprintf(msg, "{\"presence\":%d, \"mac\":%s}", *presence_p > 0 ? 1:0,TARGET_MAC_ADDRESS);
        mosquitto_publish(mosq, NULL, MQTT_PRESENCE_TOPIC, strlen(msg), msg, 1, 1);
        sleep(MQTT_PUBLISH_INTERVAL);
    }
    pthread_exit(t_args);
}

int main(int argc, char **argv)
{
    int adapter_id;
    bool detected = false;
    bdaddr_t bdaddr;
    char name[248] = { 0 };
        
    setlogmask(LOG_UPTO (LOG_DEBUG));
    openlog("bt-presence.log", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);
    syslog(LOG_DEBUG, "starting bt-presence application");

    signal(SIGINT, graceful_stop);
    signal(SIGTERM, graceful_stop);
    syslog(LOG_DEBUG, "registered signal handler");

    //check if program should be deamonized
    if(argc==2){
        if(strcmp(argv[1], "-d") == 0){
            syslog(LOG_DEBUG, "turning into a daemon");
            daemonize();
        }
    }

    mqtt_setup();
    mosq_pub_thread_args = (struct mqtt_pub_thread_args * )malloc(sizeof(struct mqtt_pub_thread_args));
    mosq_pub_thread_args->presence = &presence;
    mosq_pub_thread_args->keepalive = &keepalive;
    pthread_create(&mosq_pub_thread_args->tid,NULL,&mqtt_publisher_thread, mosq_pub_thread_args);

    // create hci socket
    adapter_id = hci_get_route(NULL);
    sock = hci_open_dev( adapter_id );
    if (adapter_id < 0 || sock < 0) {
        syslog(LOG_DEBUG, "error opening socket");
        exit(1);
    }

    // convert MAC string into bdaddr_t
    str2ba(TARGET_MAC_ADDRESS, &bdaddr);

    while (keepalive) {
        memset(name, 0, sizeof(name));
        // use converted MAC to ask device name
        syslog(LOG_DEBUG, "asking for %s name", TARGET_MAC_ADDRESS);
        if (hci_read_remote_name(sock, &bdaddr, sizeof(name), name, 0) < 0) {
            // device not found
            strcpy(name, "[unknown]");
            detected = false;
        }else{
            // device found
            detected = true;
        }
        syslog(LOG_DEBUG, "device name: %s", name);
        compute_presence(detected);
        sleep(DETECTION_INTERVAL);
    }
}
