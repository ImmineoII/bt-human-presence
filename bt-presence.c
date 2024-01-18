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

int sock;

static void graceful_stop(int signum){

    syslog(LOG_DEBUG, "caught signal, exiting");

    // Chiusura del socket HCI
    if (hci_close_dev(sock) < 0) {
        syslog(LOG_ERR, "error closing HCI socket");
    }

    closelog();
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    int adapter_id;
    bool presence = false;
    bdaddr_t bdaddr;
    char name[248] = { 0 };
    const char * mac_address_str = "C8:9F:0C:14:7D:33";

    setlogmask(LOG_UPTO (LOG_DEBUG));
    openlog("bt-presence.log", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);
    syslog(LOG_DEBUG, "starting bt-presence application");

    signal(SIGINT, graceful_stop);
    signal(SIGTERM, graceful_stop);
    syslog(LOG_DEBUG, "Registered signal handler");

    adapter_id = hci_get_route(NULL);
    sock = hci_open_dev( adapter_id );
    if (adapter_id < 0 || sock < 0) {
        syslog(LOG_DEBUG, "error opening socket");
        exit(1);
    }

    // convert MAC string into bdaddr_t
    str2ba(mac_address_str, &bdaddr);

    while (1) {
        memset(name, 0, sizeof(name));
        // use converted MAC to ask device name
        syslog(LOG_DEBUG, "asking for %s name", mac_address_str);
        if (hci_read_remote_name(sock, &bdaddr, sizeof(name), name, 0) < 0) {
            // device not found
            strcpy(name, "[unknown]");
            presence = false;
        }else{
            // device found
            presence = true;
        }
        syslog(LOG_DEBUG, "device name: %s", name);
        sleep(1);
    }
}
