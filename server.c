/*
* Ido Kantor
 * Server for the game handles two clients at the same time
 */
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

#include "cryptography_game_util.h"

//defines
#define CORRECT_ARGC 2
#define SERVER_IP "0.0.0.0"
#define CLIENT_MAX "max 2 clients can connect"
#define SOCKET_ERROR -1
#define SOCKET_INIT_ERROR 0
#define PORT_ARGV 1
#define MAX_CLIENTS 2
#define SEND_FLAG 0
#define SLEEP 100000
#define PTHREAD_CREATE 1
#define BUFFER_SIZE 1024
#define RECEIVE_FLAG 0
#define CHECK_RECEIVE 0
#define NULL_CHAR 0
#define ACCEPTED_SUCCESSFULLY 0
#define CLIENT_THREAD_EXISTS 0
#define CLIENT_SOCKET_EXISTS 0
#define LISTEN 1

//data types
struct AcceptedSocket {
    int acceptedSocketFD;
    struct sockaddr_in address;
    int error;
    int acceptedSuccessfully;
};

//globals
volatile sig_atomic_t stop = 0;
struct AcceptedSocket acceptedSockets[MAX_CLIENTS];
int acceptedSocketsCount = 0;
pthread_t clientThreads[MAX_CLIENTS] = {0};
pthread_mutex_t globals_mutex = PTHREAD_MUTEX_INITIALIZER;
//Mutex for globals

//prototypes
/*
 * Handles signals, specifically SIGINT (press ctrl+c)
 * (interrupt signal) to safely terminate the server.
 * Parameters: signal - The signal number received (e.g., SIGINT).
 */

void handle_signal(int signal);

/*
 * Accepts an incoming connection on the server socket.
 * Parameters: serverSocketFD - The file descriptor for the server socket.
 * Returns: An AcceptedSocket structure containing the
 * client socket file descriptor, address,
 * success flag, and error code (if any).
 */

struct AcceptedSocket acceptIncomingConnection(int serverSocketFD);

/*
 * Accepts new incoming connections and starts threads to receive
 * and print their data.
 * Parameters: serverSocketFD - The file descriptor for the server socket.
 * Returns: None.
 */

/*
* Function executed by each client thread to receive data
* from the connected client
* and print it to the console.
* Parameters: arg - A pointer to the client socket
* file descriptor cast to void*.
* Returns: NULL upon completion.
 */

void * receiveAndPrintIncomingData(void * arg);

/*
* Starts the process of accepting incoming connections on the server socket.
* Continuously checks for new connections until the stop flag is set.
* Parameters: serverSocketFD - The file descriptor for the server socket.
* Returns: None.
 */

void startAcceptingIncomingConnections(int serverSocketFD);

/*
* Creates a new thread to receive and print data from the connected client.
* Parameters: clientSocketFD - A pointer to an AcceptedSocket structure
* containing the client socket file descriptor.
* Returns: None.
 */

void receiveAndPrintIncomingDataOnSeparateThread(
    const struct AcceptedSocket *clientSocketFD);

/*
* Sends received messages from one client to all other connected clients.
* Parameters: buffer - The message to be sent.
* socketFD - The file descriptor of the sender
* (to exclude it from receiving its own message).
* Returns: None.
 */

void sendReceivedMessageToTheOtherClients(const char *buffer,int socketFD);

/*
* Cleans up and cancels client threads based on the count provided.
* Parameters: count - The number of client threads to clean up.
* Returns: None.
 */

void cleanupThreads(int count);

/*
* Closes all accepted client sockets based on the count provided.
* Parameters: count - The number of accepted client sockets to close.
* Returns: None.
 */

void cleanupClientSockets(int count);

/*
* Initializes the server socket, binds it to the specified port,
* and prepares it to listen for incoming connections.
* Parameters: port - The port number on which the
* server will listen for incoming connections.
* Returns: A valid server socket file descriptor if successful;
* EXIT_FAILURE if there is an error.
 */

int initServerSocket(int port);

void startAcceptingIncomingConnections(const int serverSocketFD)
{
    while(!stop)
    {
        pthread_mutex_lock(&globals_mutex);
        if(acceptedSocketsCount < MAX_CLIENTS)
        {
            pthread_mutex_unlock(&globals_mutex);
            struct AcceptedSocket clientSocket =
                acceptIncomingConnection(serverSocketFD);
            if(clientSocket.acceptedSuccessfully) {
                pthread_mutex_lock(&globals_mutex);
                acceptedSockets[acceptedSocketsCount++] = clientSocket;
                receiveAndPrintIncomingDataOnSeparateThread(&clientSocket);
                pthread_mutex_unlock(&globals_mutex);
            }
        }
        else
        {
            pthread_mutex_unlock(&globals_mutex);
            const int clientSocketFD = accept(serverSocketFD, NULL, NULL);
            send(clientSocketFD, CLIENT_MAX,
                strlen(CLIENT_MAX), SEND_FLAG);
            close(clientSocketFD);
        }
        usleep(SLEEP);
    }
}

void receiveAndPrintIncomingDataOnSeparateThread(
    const struct AcceptedSocket *clientSocketFD) {
    pthread_create(&clientThreads[acceptedSocketsCount - PTHREAD_CREATE],
        NULL, receiveAndPrintIncomingData,
        (void *)(intptr_t)clientSocketFD->acceptedSocketFD);
}

void * receiveAndPrintIncomingData(void * arg)
{
    const int clientSocketFD = (intptr_t)arg;
    char buffer[BUFFER_SIZE];
    while(!stop) {
        const ssize_t amountReceived = recv(clientSocketFD,
            buffer,sizeof(buffer),RECEIVE_FLAG);
        if(amountReceived > CHECK_RECEIVE) {
            buffer[amountReceived] = NULL_CHAR;
            printf("%s\n",buffer);

            sendReceivedMessageToTheOtherClients(buffer, clientSocketFD);
        }
        if(amountReceived == CHECK_RECEIVE || stop) {
            break;
        }
    }
    close(clientSocketFD);
    return NULL;
}

void sendReceivedMessageToTheOtherClients(const char *buffer,
    const int socketFD) {
    pthread_mutex_lock(&globals_mutex);
    for(int i = 0; i < acceptedSocketsCount; i++)
        if(acceptedSockets[i].acceptedSocketFD != socketFD)
        {
            send(acceptedSockets[i].acceptedSocketFD, buffer,
                strlen(buffer), SEND_FLAG);
        }
    pthread_mutex_unlock(&globals_mutex);
}

struct AcceptedSocket acceptIncomingConnection(const int serverSocketFD)
{
    struct sockaddr_in clientAddress ;
    int clientAddressSize = sizeof(struct sockaddr_in);
    struct AcceptedSocket acceptedSocket = {0};
    const int clientSocketFD = accept(serverSocketFD,
        (struct sockaddr*)&clientAddress, (socklen_t *)&clientAddressSize);
    acceptedSocket.address = clientAddress;
    acceptedSocket.acceptedSocketFD = clientSocketFD;
    acceptedSocket.acceptedSuccessfully = clientSocketFD > ACCEPTED_SUCCESSFULLY;
    if(clientSocketFD < ACCEPTED_SUCCESSFULLY)
        acceptedSocket.error = clientSocketFD;
    else
        acceptedSocket.error = ACCEPTED_SUCCESSFULLY;
    return acceptedSocket;
}

void cleanupThreads(const int count)
{
    for (int i = 0; i < count; i++)
    {
        if(clientThreads[i] != CLIENT_THREAD_EXISTS)
        {
            pthread_cancel(clientThreads[i]);
            pthread_join(clientThreads[i], NULL);
            // Wait for each thread to finish
        }
    }
}

void cleanupClientSockets(const int count)
{
    for (int i = 0; i < count; i++)
    {
        if(acceptedSockets[i].acceptedSocketFD > CLIENT_SOCKET_EXISTS)
        {
            close(acceptedSockets[i].acceptedSocketFD);
        }
    }
}

void handle_signal(const int signal)
{
    printf("Caught signal %d\n", signal);
    stop = 1;  // Set the stop flag when SIGINT is received
}

int initServerSocket(const int port) {
    const int serverSocketFD = createTCPIpv4Socket();
    if(serverSocketFD == SOCKET_ERROR) {
        printf("Error creating server socket\n");
        return EXIT_FAILURE;
    }
    struct sockaddr_in server_address;
    const int correct_ip = createIPv4Address(SERVER_IP,
        port, &server_address);
    if(correct_ip == SOCKET_INIT_ERROR) {
        printf("Incorrect IP or port\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    fcntl(serverSocketFD, F_SETFL, O_NONBLOCK);
    const int result = bind(serverSocketFD,
        (struct sockaddr *)&server_address, sizeof(server_address));
    if(result == SOCKET_INIT_ERROR) {
        printf("socket was bound successfully\n");
    }
    else {
        printf("socket was not bound unsuccessfully\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    const int listenResult = listen(serverSocketFD,LISTEN);
    if (listenResult == SOCKET_INIT_ERROR) {
        printf("listen successful\n");
    }
    else {
        printf("listen failed\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    return serverSocketFD;
}

/*
 * main runs server and other functions
 */

int main(const int argc, char *argv[]) {
    signal(SIGINT, handle_signal);
    if(argc != CORRECT_ARGC)
    {
        printf("incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    const int serverSocketFD = initServerSocket(atoi(argv[PORT_ARGV]));
    if(serverSocketFD == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    startAcceptingIncomingConnections(serverSocketFD);
    cleanupThreads(acceptedSocketsCount);
    cleanupClientSockets(acceptedSocketsCount);
    shutdown(serverSocketFD,SHUT_RDWR);
    close(serverSocketFD);
    pthread_mutex_destroy(&globals_mutex);
    return 0;
}