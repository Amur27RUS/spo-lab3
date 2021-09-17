#include "Server.h"
#include <sys/socket.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "Parser/Parser.h"
#include "main.h"
#include "ClientHandling.h"

#include "Client/Client.h"


void* serverMainThreadWorker(void *serverContextTemplate){
    serverStruct* context = ((serverStruct*)serverContextTemplate);

    // Запускаем сокет
    int serverSocket;
    if ((serverSocket = (socket(AF_INET, SOCK_STREAM , 0))) == -1){
        perror("Server couldn't start");
        context->serverShouldStop = true;
        pthread_exit(NULL);
    }

    struct sockaddr_in sockAddr;
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr =INADDR_ANY;
    sockAddr.sin_port = htons(PORT);

    //Биндим сокет, чтобы был доступен для клиентов
    if ((bind(serverSocket, (struct sockaddr*)&sockAddr, sizeof(sockAddr))) < 0){
        perror("Error binding socket");
        context->serverShouldStop = true;
        pthread_exit(NULL);
    }

    listen(serverSocket, 255);

    //Сервер работает, пока не прописали exit
    while (!context->serverShouldStop){
        int ret = accept(serverSocket, NULL, NULL);

        //Если присоединился клиент:
        if (ret >0){
            handleClient(context,ret);
        }

        //Освобождаем процессор
        sched_yield();
    }

    puts("Closing...");

    for (int i=0; i<context->clients.amount; i++){
        close(context->clients.sockets[i]);
    }
    close(serverSocket);
    pthread_exit(NULL);
}

//аргументы сервера
serverStruct* initServerContext(){
    serverStruct* context = malloc(sizeof(serverStruct));
    context->serverShouldStop = false;
    context->clients.amount = 0;
    context->clients.capacity = 2;
    context->clients.sockets = malloc(sizeof(int)*2);
    context->prev_id = 0;
    context->messages = NULL;

    return context;
}

void serverStart(){
    serverStruct* context = initServerContext(); //Аргументы потока


    pthread_t serverThread; // Идентификатор потока
    pthread_create(&serverThread,NULL,&serverMainThreadWorker,(void*)context);

    char* line = NULL;
    size_t bufferLength = 0;

    //Проверяем то, что вводят серверу
    while (!context->serverShouldStop){
        printf("> ");
        size_t charsNum = getline(&line,&bufferLength, stdin);
        if (charsNum == (size_t)-1){
            free(line);
            puts("EOF...");
            return;
        }
        char** args = NULL;
        //Парсим команду
        int amountOfWords = parseCommand(line, &args);

        if (amountOfWords == 0){
            if (args != NULL){
                free(args);
            }
            continue;
        }
        if (strcmp("exit",args[0]) == 0){
            context->serverShouldStop = true;
        }
    }
    puts("Сервер был остановлен.");
    pthread_join(serverThread,NULL);
}
