#include <unistd.h>
#include <pthread.h>
#include "cryptography_game_util.h"
#define CORRECT_ARGC 3

void startListeningAndPrintMessagesOnNewThread(int socketFD);

void * listenAndPrint(void * arg);

void readConsoleEntriesAndSendToServer(int socketFD);

volatile int connectionClosed = 0;

int main(const int argc, char * argv[])
{
    if(argc != CORRECT_ARGC)
    {
        printf("incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    const int socketFD = createTCPIpv4Socket();
    struct sockaddr_in address;
    createIPv4Address(argv[1], atoi(argv[2]), &address);
    const int result = connect(socketFD, (struct sockaddr *)&address,sizeof(address));
    if(result == 0)
        printf("connection was successful\n");
    else {
        printf("connection to server failed\n");
        close(socketFD);
        return EXIT_FAILURE;
    }
    startListeningAndPrintMessagesOnNewThread(socketFD);
    readConsoleEntriesAndSendToServer(socketFD);
    close(socketFD);
    return 0;
}

void readConsoleEntriesAndSendToServer(const int socketFD)
{
    char *line = NULL;
    size_t lineSize = 0;
    printf("send a message\n");
    while(!connectionClosed)
    {
        char buffer[1024];
        const ssize_t charCount = getline(&line, &lineSize, stdin);
        line[charCount - 1] = 0;  // Remove newline
        sprintf(buffer, "%s", line);
        if(charCount > 0)
        {
            if (connectionClosed)
            {
                break;
            }
            if (strcmp(line, "exit") == 0)
                break;
            {
                const ssize_t send_check = send(socketFD, buffer, strlen(buffer), 0);
                if(send_check == -1)
                {
                    printf("send failed, Connection closed\n");
                    break;
                }
            }
        }
    }
    free(line);  //Free the memory allocated by getLine func
}


void startListeningAndPrintMessagesOnNewThread(const int socketFD)
{

    pthread_t id;
    pthread_create(&id, NULL, listenAndPrint, (void *)(intptr_t)socketFD);
}

void * listenAndPrint(void * arg)
{
    const int socketFD = (intptr_t)arg;
    char buffer[1024];
    while (1)
    {
        const ssize_t amountReceived = recv(socketFD ,buffer,1024,0);
        if(amountReceived > 0)
        {
            buffer[amountReceived] = 0;
            printf("Response was %s\n ",buffer);
        }
        else
        {
            printf("Connection closed, press any key to exit\n");
            connectionClosed = 1;
            break;
        }
    }
    close(socketFD);
    return NULL;
}