#include <stdio.h>
#include <string.h>
#include "Server/Server.h"
#include "Client/Client.h"

int main(int argc, char *argv[]) {
    if(argc < 2) {
        puts("lab: insufficient arguments. Try lab --help");
    }
    if (strcmp(argv[1], "--help") == 0){
        puts("How to start a lab:\n"
             "--server: server mode\n"
             "--client: client mode. pass address and ur name, like Andrey 127.0.0.1");
    }
    if (strcmp(argv[1],"--server") == 0){
        serverStart();
    } else if (strcmp(argv[1], "--client") == 0) {
        if (argc < 3){
            puts("Pass the address and ur name, like - Andrey 127.0.0.1");
            return 0;
        } else {
            clientStart(argv[2],argv[3]);
        }

    }
}
