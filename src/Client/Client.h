#pragma once

#include "Server/Server.h"
#define B_CYAN		46
#define resetcolor() printf(ESC "[0m")
#define set_display_atrib(color) 	printf(ESC "[%dm",color)

typedef struct {
    serverStruct* serverContext;
    int socket;
} clientContext;

void clientStart(char* username, char* address);
