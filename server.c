#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include "cryptography_game_util.h"
#define CORRECT_ARGC 2
#define SERVER_IP "0.0.0.0"

struct AcceptedSocket
{
    int acceptedSocketFD;
    struct sockaddr_in address;
    int error;
    bool acceptedSuccessfully;
};

struct AcceptedSocket acceptIncomingConnection(int serverSocketFD);

void acceptNewConnectionAndReceiveAndPrintItsData(int serverSocketFD);

void * receiveAndPrintIncomingData(void * arg);

void startAcceptingIncomingConnections(int serverSocketFD);

void receiveAndPrintIncomingDataOnSeparateThread(const struct AcceptedSocket *clientSocketFD);

void sendReceivedMessageToTheOtherClients(const char *buffer,int socketFD);

struct AcceptedSocket acceptedSockets[2] ;
int acceptedSocketsCount = 0;

void startAcceptingIncomingConnections(const int serverSocketFD)
{
    while(1)
    {
        struct AcceptedSocket clientSocket  = acceptIncomingConnection(serverSocketFD);
        acceptedSockets[acceptedSocketsCount++] = clientSocket;
        receiveAndPrintIncomingDataOnSeparateThread(&clientSocket);
    }
}

void receiveAndPrintIncomingDataOnSeparateThread(const struct AcceptedSocket *clientSocketFD)
{
    pthread_t id;
    pthread_create(&id, NULL, receiveAndPrintIncomingData,
        (void *)(intptr_t)clientSocketFD->acceptedSocketFD);
}

void * receiveAndPrintIncomingData(void * arg)
{
    const int clientSocketFD = (intptr_t)arg;
    char buffer[1024];
    while(1)
    {
        const ssize_t amountReceived = recv(clientSocketFD, buffer,1024,0);
        if(amountReceived > 0)
        {
            buffer[amountReceived] = 0;
            printf("%s\n",buffer);
            sendReceivedMessageToTheOtherClients(buffer, clientSocketFD);
        }
        if(amountReceived==0)
            break;
    }
    close(clientSocketFD);
    return NULL;
}

void sendReceivedMessageToTheOtherClients(const char *buffer, const int socketFD)
{

    for(int i = 0; i<acceptedSocketsCount; i++)
        if(acceptedSockets[i].acceptedSocketFD !=socketFD)
        {
            send(acceptedSockets[i].acceptedSocketFD,buffer, strlen(buffer),0);
        }
}

struct AcceptedSocket acceptIncomingConnection(const int serverSocketFD)
{
    struct sockaddr_in clientAddress ;
    int clientAddressSize = sizeof(struct sockaddr_in);
    const int clientSocketFD = accept(serverSocketFD,
        (struct sockaddr*)&clientAddress, (socklen_t *)&clientAddressSize);
    struct AcceptedSocket acceptedSocket;
    acceptedSocket.address = clientAddress;
    acceptedSocket.acceptedSocketFD = clientSocketFD;
    acceptedSocket.acceptedSuccessfully = clientSocketFD > 0;
    if(clientSocketFD < 0)
        acceptedSocket.error = clientSocketFD;
    else
        acceptedSocket.error = 0;
    return acceptedSocket;
}


int main(const int argc, char *argv[])
{
    if(argc != CORRECT_ARGC)
    {
        return EXIT_FAILURE;
    }
    const int serverSocketFD = createTCPIpv4Socket();
    struct sockaddr_in server_address;
    createIPv4Address(SERVER_IP, atoi(argv[1]), &server_address);
    const int result = bind(serverSocketFD, (struct sockaddr *)&server_address, sizeof(server_address));
    if(result == 0)
        printf("socket was bound successfully\n");

    const int listenResult = listen(serverSocketFD,1);
    if (listenResult == 0)
    {
        printf("listen successful\n");
    }
    startAcceptingIncomingConnections(serverSocketFD);
    shutdown(serverSocketFD,SHUT_RDWR);
    return 0;
}