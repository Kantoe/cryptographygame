#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include "cryptography_game_util.h"
#define CORRECT_ARGC 2
#define SERVER_IP "0.0.0.0"
#define CLIENT_MAX "max 2 clients can connect"

struct AcceptedSocket
{
    int acceptedSocketFD;
    struct sockaddr_in address;
    int error;
    bool acceptedSuccessfully;
};

volatile sig_atomic_t stop = 0;

void handle_signal(const int signal)
{
    printf("Caught signal %d\n", signal);
    stop = 1;  // Set the stop flag when SIGINT is received
}

struct AcceptedSocket acceptIncomingConnection(int serverSocketFD);

void acceptNewConnectionAndReceiveAndPrintItsData(int serverSocketFD);

void * receiveAndPrintIncomingData(void * arg);

void startAcceptingIncomingConnections(int serverSocketFD);

void receiveAndPrintIncomingDataOnSeparateThread(const struct AcceptedSocket *clientSocketFD);

void sendReceivedMessageToTheOtherClients(const char *buffer,int socketFD);

void cleanupThreads(int count);

void cleanupClientSockets(int count);

struct AcceptedSocket acceptedSockets[2];
int acceptedSocketsCount = 0;
pthread_t clientThreads[2] = {0};

void startAcceptingIncomingConnections(const int serverSocketFD)
{
    while(!stop)
    {
        if(acceptedSocketsCount < 2)
        {
            struct AcceptedSocket clientSocket  = acceptIncomingConnection(serverSocketFD);
            if(clientSocket.acceptedSuccessfully)
            {
                acceptedSockets[acceptedSocketsCount++] = clientSocket;
                receiveAndPrintIncomingDataOnSeparateThread(&clientSocket);
            }
        }
        else
        {
            const int clientSocketFD = accept(serverSocketFD, NULL, NULL);
            send(clientSocketFD, CLIENT_MAX, strlen(CLIENT_MAX), 0);
            close(clientSocketFD);
        }
        usleep(100000);
    }
}

void receiveAndPrintIncomingDataOnSeparateThread(const struct AcceptedSocket *clientSocketFD)
{
    pthread_create(&clientThreads[acceptedSocketsCount - 1],
        NULL, receiveAndPrintIncomingData,
        (void *)(intptr_t)clientSocketFD->acceptedSocketFD);
}

void * receiveAndPrintIncomingData(void * arg)
{
    const int clientSocketFD = (intptr_t)arg;
    char buffer[1024];
    while(!stop)
    {
        const ssize_t amountReceived = recv(clientSocketFD, buffer,1024,0);
        if(amountReceived > 0)
        {
            buffer[amountReceived] = 0;
            printf("%s\n",buffer);
            sendReceivedMessageToTheOtherClients(buffer, clientSocketFD);
        }
        if(amountReceived == 0 || stop)
            break;
    }
    close(clientSocketFD);
    return NULL;
}

void sendReceivedMessageToTheOtherClients(const char *buffer, const int socketFD)
{

    for(int i = 0; i < acceptedSocketsCount; i++)
        if(acceptedSockets[i].acceptedSocketFD != socketFD)
        {
            send(acceptedSockets[i].acceptedSocketFD, buffer,strlen(buffer),0);
        }
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
    acceptedSocket.acceptedSuccessfully = clientSocketFD > 0;
    if(clientSocketFD < 0)
        acceptedSocket.error = clientSocketFD;
    else
        acceptedSocket.error = 0;
    return acceptedSocket;
}

void cleanupThreads(const int count)
{
    for (int i = 0; i < count; i++)
    {
        if(clientThreads[i] != 0)
        {
            pthread_cancel(clientThreads[i]);
            pthread_join(clientThreads[i], NULL);  // Wait for each thread to finish
        }
    }
}

void cleanupClientSockets(const int count)
{
    for (int i = 0; i < count; i++)
    {
        if(acceptedSockets[i].acceptedSocketFD > 0)
        {
            close(acceptedSockets[i].acceptedSocketFD);
        }
    }
}

int main(const int argc, char *argv[])
{
    signal(SIGINT, handle_signal);
    if(argc != CORRECT_ARGC)
    {
        printf("incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    const int serverSocketFD = createTCPIpv4Socket();
    struct sockaddr_in server_address;
    createIPv4Address(SERVER_IP, atoi(argv[1]), &server_address);
    fcntl(serverSocketFD, F_SETFL, O_NONBLOCK);
    const int result = bind(serverSocketFD, (struct sockaddr *)&server_address, sizeof(server_address));
    if(result == 0)
        printf("socket was bound successfully\n");
    else {
        printf("socket was not bound unsuccessfully\n");
        return EXIT_FAILURE;
    }
    const int listenResult = listen(serverSocketFD,1);
    if (listenResult == 0)
    {
        printf("listen successful\n");
    }
    startAcceptingIncomingConnections(serverSocketFD);
    cleanupThreads(acceptedSocketsCount);
    cleanupClientSockets(acceptedSocketsCount);
    shutdown(serverSocketFD,SHUT_RDWR);
    close(serverSocketFD);
    return 0;
}