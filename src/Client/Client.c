#include "Client.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>


#include "terminalSettings.h"
#include "main.h"

#define ESC "\033"
#define CSI         ESC "["
#define clrscr()    printf(ESC "[2J")

typedef struct innerM_ {
    struct innerM_* next;
    struct innerM_* children;

    size_t id;
    char* author;
    char* message;
    bool read;
    bool collapsed;
}innerMessage;

int selected = 0;

typedef struct {
    struct sockaddr_in serverAddress;
    char* username;
    int socket;

    struct {
        int top;
        int left;
        int width;
        int height;
        size_t replyId;
        size_t selectedId;
        bool isWriting;
        int move;

        struct {
            size_t capacity;
            size_t length;
            char* buffer;
        }input;
    }ui;

    innerMessage* messages;
    bool isClientShouldStop;
}innerContext;

static int doConnection(innerContext* context){
    context->socket = socket(AF_INET, SOCK_STREAM, 0);
    return connect(context->socket, (struct sockaddr* )&context->serverAddress, sizeof(context->serverAddress));
}

static void reconnectToServer(innerContext* context){
    for (int i = 0; i < 5; i++){
        close(context->socket);

        if (doConnection(context) == 0){
            return;
        }
    }
    puts("Cannot connect to remote host");
}

static innerMessage* findMessageById(innerMessage* inputMsg, size_t id){
    for (innerMessage* msg = inputMsg; msg;msg = msg->next){
        if (msg->id == id){
            return msg;
        }

        innerMessage* child = findMessageById(msg->children, id);
        if (child){
            return child;
        }
    }

    return NULL;
}

static bool isBranchRead(innerMessage* message){
    for(innerMessage* msg = message; msg; msg = msg->next){
        if (!msg->read){
            return false;
        }

        if (!isBranchRead(msg->children)){
            return false;
        }
    }
    return true;
}

static size_t drawBufferLine(char* buffer, char* line, size_t top, size_t left, size_t width){
    size_t length = strlen(line);


    memcpy(buffer + top * width + left, line, length);

    return left + length;
}

//Отрисосываем сообщения
static int drawBufferMessages(innerContext* context, char* buffer, innerMessage* messages, int top,
                              int left, size_t width, int* selectedTop, innerMessage** bufferMessages){
    for(innerMessage* msg = messages; msg; msg= msg->next){
        for (int i = 0; i < left; i++){
            buffer[top * width + i] = ' ';
        }

        size_t localLeft = left;

        if (context->ui.selectedId == msg->id) {
            *selectedTop = top;
        }

        if (context->ui.replyId == msg->id){
            buffer[top * width + localLeft] = '>';
        } else if (!msg->read || !isBranchRead(msg->children)) {
            buffer[top * width + localLeft] = '!';
        } else {
            buffer[top * width + localLeft] = ' ';
        }

        if (msg->collapsed) {
            buffer[top * width + localLeft + 1] = '+';
        } else {
            buffer[top * width + localLeft + 1] = ' ';
        }

        localLeft = drawBufferLine(buffer, msg->author, top, localLeft + 2, width);
        localLeft = drawBufferLine(buffer, ": ", top, localLeft, width);
        localLeft = drawBufferLine(buffer, msg->message, top, localLeft, width);

        memset(buffer + top * width + localLeft, ' ', width - localLeft);

        bufferMessages[top] = msg;
        ++top;

        if (!msg->collapsed){
            top = drawBufferMessages(context, buffer, msg->children, top, left+1, width, selectedTop, bufferMessages);
        }
    }

    return top;
}

//Границы для отрисовки сообщений
static void drawBufferBounds(innerMessage* messages, size_t* width, size_t* height){
    for (innerMessage* msg = messages; msg; msg = msg->next){
        ++(*height);


        size_t messageWidth = 2 + strlen(msg->author) + 2 + strlen(msg->message);

        if (!msg->collapsed){
            size_t childrenWidth = 0, childrenHeight = 0;
            drawBufferBounds(msg->children, &childrenWidth, &childrenHeight);

            childrenWidth +=1; //may be...
            if (childrenWidth > messageWidth){
                messageWidth = childrenWidth;
            }
            *height += childrenHeight;
        }

        if (messageWidth > *width){
            *width = messageWidth;
        }
    }
}

//Отрисовка места, где отрисовываем сообщения
static char* drawBuffer(innerContext* context, size_t* width, size_t* height, int* selectedTop, innerMessage*** bufferMessage){
    char* buffer;

    drawBufferBounds(context->messages, width, height);

    buffer = malloc((*width) * (*height));
    *bufferMessage = malloc(sizeof(innerMessage*) * (*height));
    drawBufferMessages(context,buffer,context->messages, 0, 0, *width, selectedTop, *bufferMessage);

    return buffer;
}

//Перерисовываем интерфейс с новыми сообщениями
static void redrawScreen(innerContext* context){
    size_t width = 0, height = 0;
    selected = 0;

    if (context->ui.selectedId == 0 && context->messages){
        context->ui.selectedId = context->messages->id;
    }

    innerMessage ** bufferMessage;
    char* buffer = drawBuffer(context,&width,&height,&selected, &bufferMessage);

    if (context->ui.move != 0){
        selected += context->ui.move;

        printf("%d, %d, %d",selected, context->ui.height, context->ui.top);
//        context->ui.height = (int)height - 1;
        if (selected >= height){
            if (height == 0){
                selected = 0;
            } else {
                selected = (int)height - 1;
                context->ui.top = (int) height - 4;
            }
        }

        //todo Возможно вот здесь стоит обрабатывать скролл вверх
        if (selected < height){
            context->ui.selectedId = bufferMessage[selected]->id;
        }
        context->ui.move = 0;
    }

    clrscr();
    size_t top;
    for (top = context->ui.top; top < height && top - context->ui.top < context->ui.height - 2; ++top) {
        printf(CSI"%zu;1H", top - context->ui.top + 1);
        fflush(stdout);

        size_t bufferLine = width - context->ui.left;

        //Выводим сообщения на экран
        write(STDOUT_FILENO, buffer + top * width + context->ui.left,
              bufferLine < context->ui.width ? bufferLine : context->ui.width);

        bufferMessage[top]->read = true;
    }

    top = context->ui.height - 1;
    printf(CSI"%zu;1H", top);
    set_display_atrib(B_CYAN);
    printf("q - exit, wa - move, r - reply, n - new message, c - hide message\n");
    resetcolor();
    ++top;
    printf("New message: ");
    fflush(stdout);
    char* inputStart = context->ui.input.buffer;
    size_t inputLength = context->ui.input.length;

    if (context->ui.input.length > context->ui.width - 15){
        inputStart += inputLength - (context->ui.width) - 15;
        inputLength = context->ui.width - 15;
    }

    write(STDOUT_FILENO, inputStart, inputLength);

    if(context->ui.isWriting){
        printf(CSI"%zu;%zuH", top, inputLength + 15);
    } else {
        printf(CSI"%d;1H", selected - context->ui.top + 1);
    }

    fflush(stdout);
    free(bufferMessage);
//    if (buffer)
//        free(buffer);
}

static void addMessageToContext(innerContext* context, size_t id, size_t replyId, char* username, char* inputMessage){
    innerMessage* parentMessage = NULL;

    if (replyId){
        parentMessage = findMessageById(context->messages,replyId);
    }

    innerMessage **list = parentMessage ? &parentMessage->children : &context->messages;
    for (innerMessage* msg = *list; msg; msg = msg->next){
        if (msg->id > id){
            break;
        }
        list = &msg->next;
    }

    innerMessage* newMessage = malloc(sizeof(message));

    newMessage->id = id;
    newMessage->next = *list;
    newMessage->children = NULL;
    newMessage->author = strdup(username);
    newMessage->message = strdup(inputMessage);
    newMessage->read = false;
    newMessage->collapsed = false;
    *list = newMessage;

    redrawScreen(context);
}

static void sendMessage(innerContext* context, char*message){
    size_t usernameLength = strlen(context->username), messageLength = strlen(message);

    write(context->socket, &context->ui.replyId,sizeof(context->ui.replyId));
    write(context->socket, &usernameLength,sizeof(usernameLength));
    write(context->socket, context->username,usernameLength);
    write(context->socket, &messageLength, sizeof(messageLength));
    write(context->socket, message,messageLength);

}

static void* serverDispatcher(void* param){
    innerContext* context = (innerContext*) param;

    while (!context->isClientShouldStop){
        size_t id, replyId;

        if (read(context->socket, &id, sizeof(id)) < 0){
            if (!context->isClientShouldStop){
                reconnectToServer(context);
            }
            continue;
        }

        read(context->socket, &replyId, sizeof(replyId));

        size_t usernameLength, messageLength;
        read(context->socket,&usernameLength, sizeof(usernameLength));

        char username[usernameLength + 1];
        read(context->socket,username,usernameLength);
        username[usernameLength] = '\0';

        read(context->socket, &messageLength, sizeof(messageLength));

        char message[messageLength + 1];
        read(context->socket, message, messageLength);
        message[messageLength] = '\0';

        addMessageToContext(context,id,replyId,username,message);
    }

    pthread_exit(0);
}

static void runServerDispatcher(innerContext* context){
    pthread_t dispatcher;
    pthread_create(&dispatcher, NULL,serverDispatcher, (void*)context);
}

static void handleConsole(innerContext* context){
    while (!context->isClientShouldStop){
        int c = getc(stdin);

        //Если пользователь вводит сообщение:
        if(context->ui.isWriting){
            //Отправляем сообщение по завершению
            if (c == '\n') {
                if (context->ui.input.length == 0) {
                    context->ui.isWriting = false;
                    context->ui.replyId = 0;
                    redrawScreen(context);
                    continue;
                }

                char* buff = malloc(context->ui.input.length+1);
                memcpy(buff, context->ui.input.buffer, context->ui.input.length);
                buff[context->ui.input.length] = '\0';

                context->ui.input.length = 0;
                context->ui.isWriting = false;
                redrawScreen(context);

                sendMessage(context, buff);
                free(buff);
                context->ui.replyId = 0;
                continue;
            }

            //При удалении
            if (c == '\177'){ // deleting is working..
                if (context->ui.input.length > 0){
                    --context->ui.input.length;
                    redrawScreen(context);
                }
                continue;
            }


            if (isprint(c) || c == ' '){
                if (context->ui.input.capacity == context->ui.input.length){
                    context->ui.input.capacity *= 2;
                    context->ui.input.buffer = realloc(context->ui.input.buffer, context->ui.input.capacity);
                }
                context->ui.input.buffer[context->ui.input.length++] = (char) c;
                redrawScreen(context);
            }
            continue;
        }

        //Отслеживаем нажатие клавиш
        switch (c){
            case 'q':
                context->isClientShouldStop = true;
                break;
            case 'r':
                context->ui.replyId = context->ui.selectedId;
                context->ui.isWriting = true;
                redrawScreen(context);
                break;
            case 'n':
                context->ui.replyId = 0;
                context->ui.isWriting = true;
                redrawScreen(context);
                break;

            case 'c':{
                innerMessage* msg = findMessageById(context->messages, context->ui.selectedId);
                if (msg){
                    msg->collapsed = !msg->collapsed;
                    redrawScreen(context);
                }
                break;
            }

            case 'w':
                context->ui.move = -1;
                if(context->ui.top > 0) {
                    --context->ui.top;
                }
                redrawScreen(context);
                break;

            case 's':
                context->ui.move = 1;
                int msgs = sizeof *context->messages;
                if(selected > context->ui.height -5){
                    ++context->ui.top;
                }
//                if(context->ui.top < msgs -3) {
//                    ++context->ui.top;
//                }
                redrawScreen(context);
                break;

            case 'a':
                if (context->ui.left > 0){
                    --context->ui.left;
                    redrawScreen(context);
                }
                break;

            case 'd':
                ++context->ui.left;
                redrawScreen(context);
                break;

        }
    }
}

void clientStart(char* username, char* address){
    innerContext* context = malloc(sizeof(innerContext));
    context->serverAddress.sin_family = AF_INET;
    context->serverAddress.sin_port = htons(PORT); //port

    if (inet_aton(address, &context->serverAddress.sin_addr) == 0){
        puts("bad hostname format");
        return;
    }

    if (doConnection(context)){
        puts("There was an error while trying to make connection");
        return;
    }

    struct winsize window;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window);

    context->username = strdup(username);
    context->ui.top = 0;
    context->ui.left = 0;
    context->ui.width = window.ws_col;
    context->ui.height = window.ws_row;
    context->ui.replyId = 0;
    context->ui.selectedId = 0;
    context->ui.isWriting = false;
    context->ui.move = 0;

    context->ui.input.capacity = 256;
    context->ui.input.length = 0;
    context->ui.input.buffer = malloc(256);

    context->messages = NULL;
    context->isClientShouldStop = false;

    struct termios originalSettings = set_keypress();

    runServerDispatcher(context);
    handleConsole(context);

    close(context->socket);

    reset_keypress(originalSettings);
}
