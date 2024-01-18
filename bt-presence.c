#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

int main(int argc, char **argv)
{
    int adapter_id, sock;
    bool presence = false;
    bdaddr_t bdaddr;
    char name[248] = { 0 };
    const char * mac_address_str = "C8:9F:0C:14:7D:33";

    adapter_id = hci_get_route(NULL);
    sock = hci_open_dev( adapter_id );
    if (adapter_id < 0 || sock < 0) {
        printf("error opening socket\n");
        exit(1);
    }

    // convert MAC string into bdaddr_t
    str2ba(mac_address_str, &bdaddr);

    while (1) {
        // use converted MAC to ask device name
        printf("Asking for %s name\n", mac_address_str);
        if (hci_read_remote_name(sock, &bdaddr, sizeof(name), name, 0) < 0) {
            // device not found
            strcpy(name, "[unknown]");
            presence = false;
        }else{
            // device found
            presence = true;
        }
        printf("Device name: %s\n", name);
        sleep(1);
    }

    close( sock );
    return 0;
}
