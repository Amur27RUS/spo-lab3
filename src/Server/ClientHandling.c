#include "ClientHandling.h"
#include <stdio.h>

#include "Client/Client.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>



void sendToClient(int socket, message* msg, size_t replyId){
    size_t authorLength = strlen(msg->author);
    size_t msgLength = strlen(msg->message);

    write(socket, &msg->id, sizeof(msg->id));
    write(socket, &replyId, sizeof(replyId));
    write(socket, &authorLength, sizeof(authorLength));
    write(socket, msg->author, authorLength);
    write(socket, &msgLength, sizeof(msgLength));
    write(socket, msg->message, msgLength);
}

void sendFirstMessage(int socket, message* messages, size_t replyId){
    for (message* msg = messages; msg;msg = msg->next){
        sendToClient(socket, msg,replyId);

        sendFirstMessage(socket, msg->children, msg->id);
    }
}

void sendMessageToAllClients(serverStruct* context, message* messageToSend, size_t replyId){
    for (int i = 0; i < context->clients.amount; i++){
        sendToClient(context->clients.sockets[i],messageToSend,replyId);
    }
}

void addNewClient(serverStruct* context, int socket){
    if (context->clients.capacity == context->clients.amount){
        context->clients.capacity*=2;
        context->clients.sockets = realloc(context->clients.sockets, sizeof(int)* context->clients.capacity);
    }
    context->clients.sockets[context->clients.amount++] = socket;
}

message* findMessageById(message* messages, size_t id){
    for(message* msg = messages; msg; msg=msg->next){
        if (msg->id == id){
            return msg;
        }
        message* child = findMessageById(msg->children, id);
        if (child){
            return child;
        }
    }
    return NULL;
}


//Добавляем сообщение
void addMessageToContext(serverStruct* context, size_t replyId, char* author, char* printedMessage){
    message* parent = NULL;

    //Если мы отвечаем на сообщение:
    if (replyId){
        parent = findMessageById(context->messages, replyId);
    }

    message **list = parent ? &parent->children : &context->messages;
    message *newMessage = malloc(sizeof(message));

    newMessage->id = ++context->prev_id;
    newMessage->author = strdup(author);
    newMessage->message = strdup(printedMessage);
    newMessage->next = *list;
    newMessage->children = NULL;

    *list = newMessage;

    sendMessageToAllClients(context, newMessage, replyId);

    //Если есть replyId, то значит, что это сообщение ответ на другое
    if (replyId){
        printf("[INFO]: Message from user: %s - reply to %zu: %s\n",author,replyId, printedMessage);
    }else {
        printf("[INFO]: Message from user: %s: %s\n",author, printedMessage);
    }

}

void* listenToClient(void* rawContext){
    clientContext* context = (clientContext*)rawContext;
    char* authorsPtr = NULL;
    while (!context->serverContext->serverShouldStop){
        size_t replyId; // first field | ID сообщения,на которое мы отвечаем
        if (read(context->socket,&replyId,sizeof(replyId)) <= 0){ // Если произошла ошибка и подключился тот же клиен, то мы уменьшаем кол-во клиентов на 1
            for (int i = 0; i < context->serverContext->clients.amount; i++){
                if (context->socket == context->serverContext->clients.sockets[i]){
                    context->serverContext->clients.sockets[i] =
                            context->serverContext->clients.sockets[--context->serverContext->clients.amount];
                    break;
                }
            }
            if (authorsPtr){
                printf("Client `%s` disconnected\n",authorsPtr);
            } else{
                puts("Client disconnected");
            }
            close(context->socket);
            break;
        }
        size_t authorLength, msgLength;
        read(context->socket, &authorLength, sizeof(authorLength)); //Длина имени автора

        char authorName[authorLength+1]; //extension
        read(context->socket, authorName,authorLength); // Имя автора
        authorName[authorLength] = '\0';
        authorsPtr = authorName;

        read(context->socket, &msgLength, sizeof(msgLength)); //Длина сообщения
        char msg[msgLength+1];
        read(context->socket, msg, msgLength);  //Сообщение
        msg[msgLength] = '\0';

        addMessageToContext(context->serverContext, replyId, authorName,msg); //Добавляем сообщение, автора и id
    }
    pthread_exit(0);
}

void handleClient(serverStruct* context, int socket){
    clientContext* cContext = malloc(sizeof(clientContext));

    cContext->serverContext = context;
    cContext->socket = socket;

    sendFirstMessage(socket, context->messages, 0);

    addNewClient(context, socket);


    pthread_t clientThread;
    pthread_create(&clientThread,NULL,listenToClient,cContext);
}
