#pragma once
#include <stdbool.h>
#include <stdlib.h>

typedef struct m_ {
    struct m_* next;
    struct m_* children;

    size_t id;
    char* author;
    char* message;
}message;

typedef struct {
    struct {
        int capacity;
        int amount;
        int* sockets;
    }clients;
    size_t prev_id;

    message* messages;
    _Atomic bool serverShouldStop;
}serverStruct;

void serverStart();
