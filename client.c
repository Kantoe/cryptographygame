#include <unistd.h>
#include <pthread.h>
#include "cryptography_game_util.h"

//defines
#define CORRECT_ARGC 3
#define REMOVE_NEWLINE 1
#define REPLACE_NEWLINE 0
#define CHECK_RECEIVE 0
#define NULL_CHAR 0
#define TRUE 1
#define BUFFER_SIZE 1024
#define CHECK_LINE_SIZE 0
#define CHECK_EXIT 0
#define CHECK_SEND -1
#define SEND_FLAG 0
#define RECEIVE_FLAG 0
#define SOCKET_ERROR -1
#define SOCKET_INIT_ERROR 0
#define IP_ARGV 1
#define PORT_ARGV 2

//globals
volatile int connectionClosed = 0;

//prototypes

/* Starts a new thread to listen for messages from the server
 * and print them to the console.
 * Parameters: socketFD - The file descriptor for the
 * socket connected to the server. */

void startListeningAndPrintMessagesOnNewThread(int socketFD);

/*
 * Function executed by the listener
 * thread that receives messages from the server.
 * Parameters: arg - A pointer to the socket file descriptor cast to void*.
 * Returns: NULL upon completion.
 */

void * listenAndPrint(void * arg);

/*
* Reads user input from the console and sends it to the server.
* Parameters: socketFD - The file descriptor for
* the socket connected to the server.
* Returns: None.
 */

void readConsoleEntriesAndSendToServer(int socketFD);

/*
* Initializes the client socket, connects to the server,
* and returns the socket file descriptor.
* Parameters: ip - The server's IP address as a string.
* port - The server's port number as a string.
* Returns: A valid socket file descriptor if successful;
* EXIT_FAILURE if there is an error (such as socket creation
* failure or connection failure).
 */

int initClientSocket(const char *ip, const char *port);

void readConsoleEntriesAndSendToServer(const int socketFD) {
    char *line = NULL;
    size_t lineSize = 0;
    printf("send a message\n");
    while(!connectionClosed) {
        char buffer[BUFFER_SIZE];
        const ssize_t charCount = getline(&line, &lineSize, stdin);
        line[charCount - REMOVE_NEWLINE] = REPLACE_NEWLINE;  // Remove newline
        sprintf(buffer, "%s", line);
        if(charCount > CHECK_LINE_SIZE) {
            if(connectionClosed) {
                break;
            }
            if(strcmp(line, "exit") == CHECK_EXIT) {
                break;
            }
            const ssize_t send_check = send(socketFD, buffer, strlen(buffer), SEND_FLAG);
            if(send_check == CHECK_SEND) {
                printf("send failed, Connection closed\n");
                break;
            }

        }
    }
    free(line);  //Free the memory allocated by getLine func
}

void startListeningAndPrintMessagesOnNewThread(const int socketFD) {
    pthread_t id;
    pthread_create(&id, NULL, listenAndPrint, (void *)(intptr_t)socketFD);
}

void * listenAndPrint(void * arg) {
    const int socketFD = (intptr_t)arg;
    char buffer[BUFFER_SIZE];
    while(TRUE) {
        const ssize_t amountReceived = recv(socketFD ,buffer, sizeof(buffer),RECEIVE_FLAG);
        if(amountReceived > CHECK_RECEIVE) {
            buffer[amountReceived] = NULL_CHAR;
            printf("Response was: %s\n",buffer);
        }
        else {
            printf("Connection closed, press any key to exit\n");
            connectionClosed = 1;
            break;
        }
    }
    close(socketFD);
    return NULL;
}

int initClientSocket(const char *ip, const char *port) {
    const int socketFD = createTCPIpv4Socket();
    if(socketFD == SOCKET_ERROR) {
        printf("failed to create socket\n");
        return EXIT_FAILURE;
    }
    struct sockaddr_in address;
    const int correct_ip = createIPv4Address(ip, atoi(port), &address);
    if(correct_ip == SOCKET_INIT_ERROR) {
        printf("Incorrect IP or port\n");
        close(socketFD);
        return EXIT_FAILURE;
    }
    const int result = connect(socketFD,
        (struct sockaddr *)&address,sizeof(address));
    if(result == SOCKET_INIT_ERROR) {
        printf("connection was successful\n");
    }
    else {
        printf("connection to server failed\n");
        close(socketFD);
        return EXIT_FAILURE;
    }
    return socketFD;
}

/*
 * main func runs the client and other functions
 */

int main(const int argc, char * argv[])
{
    if(argc != CORRECT_ARGC) { // Check if the number of arguments is correct
        printf("incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    const int socketFD = initClientSocket(argv[IP_ARGV], argv[PORT_ARGV]);
    if(socketFD == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    startListeningAndPrintMessagesOnNewThread(socketFD);
    readConsoleEntriesAndSendToServer(socketFD);
    close(socketFD);
    return 0;
}