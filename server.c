/*
 * Ido Kantor
 * Server for the game handles two clients at the same time
 * This file implements a multithreaded server that manages client connections,
 * handles message routing between clients, and maintains thread synchronization
 * using mutex locks for thread-safe operations
 */

#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

#include "cryptography_game_util.h"

//defines
#define CORRECT_ARGC 2
#define SERVER_IP "0.0.0.0"
#define CLIENT_MAX "tlength:60;type:ERR;length:25;data:max 2 clients can connect"
#define SOCKET_ERROR -1
#define SOCKET_INIT_ERROR 0
#define PORT_ARGV 1
#define MAX_CLIENTS 2
#define SEND_FLAG 0
#define SLEEP 100000
#define PTHREAD_CREATE 1
#define BUFFER_SIZE 4096
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
 * This function ensures proper cleanup of resources when
 * the server is interrupted, preventing memory leaks and
 * hanging connections.
 * Parameters: signal - The signal number received (e.g., SIGINT).
 */
void handle_signal(int signal);

/*
 * Accepts an incoming connection on the server socket.
 * This function handles the initial connection setup for new clients,
 * creating the necessary socket structures and performing error checking.
 * It is called for each new client attempting to connect.
 * Parameters: serverSocketFD - The file descriptor for the server socket.
 * Returns: An AcceptedSocket structure containing the
 * client socket file descriptor, address,
 * success flag, and error code (if any).
 */
struct AcceptedSocket acceptIncomingConnection(int serverSocketFD);

/*
 * Function executed by each client thread to receive data
 * from the connected client and print it to the console.
 * This is the main message processing loop for each connected client,
 * handling incoming messages and routing them to other clients.
 * It runs until the client disconnects or the server stops.
 * Parameters: arg - A pointer to the client socket
 * file descriptor cast to void*.
 * Returns: NULL upon completion.
 */
void * receiveAndPrintIncomingData(void * arg);

/*
 * Starts the process of accepting incoming connections on the server socket.
 * This is the main server loop that manages client connections, enforces
 * the maximum client limit, and initializes client handling threads.
 * It continues running until the server is stopped via signal.
 * Parameters: serverSocketFD - The file descriptor for the server socket.
 * Returns: None.
 */
void startAcceptingIncomingConnections(int serverSocketFD);

/*
 * Creates a new thread to receive and print data from the connected client.
 * This function initializes the thread handling for each new client connection,
 * setting up the necessary structures for message processing.
 * Parameters: clientSocketFD - A pointer to an AcceptedSocket structure
 * containing the client socket file descriptor.
 * Returns: None.
 */
void receiveAndPrintIncomingDataOnSeparateThread(
    const struct AcceptedSocket *clientSocketFD);

/*
 * Sends received messages from one client to all other connected clients.
 * This function handles message broadcasting, ensuring that messages
 * are properly routed to all connected clients except the sender.
 * It maintains thread safety when accessing shared resources.
 * Parameters: buffer - The message to be sent.
 * socketFD - The file descriptor of the sender
 * (to exclude it from receiving its own message).
 * Returns: None.
 */
void sendReceivedMessageToTheOtherClients(const char *buffer,int socketFD);

/*
 * Cleans up and cancels client threads based on the count provided.
 * This function ensures proper thread termination and resource cleanup,
 * preventing memory leaks and zombie threads.
 * Parameters: count - The number of client threads to clean up.
 * Returns: None.
 */
void cleanupThreads(int count);

/*
 * Closes all accepted client sockets based on the count provided.
 * This function handles the cleanup of network resources,
 * ensuring all connections are properly terminated.
 * Parameters: count - The number of accepted client sockets to close.
 * Returns: None.
 */
void cleanupClientSockets(int count);

/*
 * Initializes the server socket, binds it to the specified port,
 * and prepares it to listen for incoming connections.
 * This function handles all server setup including address binding,
 * socket creation, and listen queue initialization.
 * Parameters: port - The port number on which the
 * server will listen for incoming connections.
 * Returns: A valid server socket file descriptor if successful;
 * EXIT_FAILURE if there is an error.
 */
int initServerSocket(int port);

void startAcceptingIncomingConnections(const int serverSocketFD) {
    // Main server loop for accepting connections
    while(!stop)
    {
        // Lock mutex to safely check client count
        pthread_mutex_lock(&globals_mutex);
        if(acceptedSocketsCount < MAX_CLIENTS)
        {
            pthread_mutex_unlock(&globals_mutex);
            // Accept new client connection
            struct AcceptedSocket clientSocket =
                acceptIncomingConnection(serverSocketFD);

            // If connection was successful, add to active clients
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
            // Handle connection when server is at capacity
            const int clientSocketFD = accept(serverSocketFD, NULL, NULL);
            s_send(clientSocketFD, CLIENT_MAX,
                strlen(CLIENT_MAX));
            close(clientSocketFD);
        }
        usleep(SLEEP);
    }
}

void receiveAndPrintIncomingDataOnSeparateThread(
    const struct AcceptedSocket *clientSocketFD) {
    // Create new thread for handling client communication
    pthread_create(&clientThreads[acceptedSocketsCount - PTHREAD_CREATE],
        NULL, receiveAndPrintIncomingData,
        (void *)(intptr_t)clientSocketFD->acceptedSocketFD);
}

void *receiveAndPrintIncomingData(void *arg) {
    // Get the client socket FD from the argument
    const int clientSocketFD = (intptr_t)arg;
    // Continuously receive and process messages until the `stop` flag is set
    while (!stop) {
        char buffer[BUFFER_SIZE] = {0};
        // Receive a message from the client
        const ssize_t amountReceived = s_recv(clientSocketFD, buffer, sizeof(buffer));
        // If a message was received successfully
        if (amountReceived > CHECK_RECEIVE) {
            // Null-terminate the received message
            buffer[amountReceived] = NULL_CHAR;
            // Print the message to the console
            printf("%s\n", buffer);
            // Broadcast the message to all other connected clients
            sendReceivedMessageToTheOtherClients(buffer, clientSocketFD);
        }
        // If the connection is closed or the `stop` flag is set, exit the loop
        if (amountReceived == CHECK_RECEIVE || stop) {
            break;
        }
    }
    // Close the client socket
    close(clientSocketFD);
    return NULL;
}

void sendReceivedMessageToTheOtherClients(const char *buffer, const int socketFD) {
    // Acquire the global mutex to ensure thread safety
    pthread_mutex_lock(&globals_mutex);
    // Iterate through all connected clients
    for (int i = 0; i < acceptedSocketsCount; i++) {
        // Send the message to all clients except the sender
        if (acceptedSockets[i].acceptedSocketFD != socketFD) {
            s_send(acceptedSockets[i].acceptedSocketFD, buffer, strlen(buffer));
        }
    }
    // Release the global mutex
    pthread_mutex_unlock(&globals_mutex);
}

struct AcceptedSocket acceptIncomingConnection(const int serverSocketFD) {
    // Initialize a new `AcceptedSocket` structure
    struct AcceptedSocket acceptedSocket = {0};
    // Accept an incoming connection
    struct sockaddr_in clientAddress;
    int clientAddressSize = sizeof(struct sockaddr_in);
    const int clientSocketFD = accept(serverSocketFD, (struct sockaddr*)&clientAddress, (socklen_t *)&clientAddressSize);
    // Populate the `AcceptedSocket` structure with information about the new connection
    acceptedSocket.address = clientAddress;
    acceptedSocket.acceptedSocketFD = clientSocketFD;
    acceptedSocket.acceptedSuccessfully = clientSocketFD > ACCEPTED_SUCCESSFULLY;
    acceptedSocket.error = clientSocketFD < ACCEPTED_SUCCESSFULLY ? clientSocketFD : ACCEPTED_SUCCESSFULLY;
    return acceptedSocket;
}

void cleanupThreads(const int count) {
    // Iterate through all client threads
    for (int i = 0; i < count; i++) {
        // If the thread is active, cancel and join it
        if (clientThreads[i] != CLIENT_THREAD_EXISTS) {
            pthread_cancel(clientThreads[i]);
            pthread_join(clientThreads[i], NULL);
        }
    }
}

void cleanupClientSockets(const int count) {
    // Iterate through all accepted sockets
    for (int i = 0; i < count; i++) {
        // If the socket is valid, close it
        if (acceptedSockets[i].acceptedSocketFD > CLIENT_SOCKET_EXISTS) {
            close(acceptedSockets[i].acceptedSocketFD);
        }
    }
}

void handle_signal(const int signal) {
    // Print a message indicating the signal received
    printf("Caught signal %d\n", signal);
    // Set the `stop` flag to trigger cleanup
    stop = 1;
}

int initServerSocket(const int port) {
    // Create a TCP/IP socket
    const int serverSocketFD = createTCPIpv4Socket();
    if (serverSocketFD == SOCKET_ERROR) {
        printf("Error creating server socket\n");
        return EXIT_FAILURE;
    }
    // Set up the server address structure
    struct sockaddr_in server_address;
    if (createIPv4Address(SERVER_IP, port, &server_address) == SOCKET_INIT_ERROR) {
        printf("Incorrect IP or port\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    // Set the socket to non-blocking mode
    fcntl(serverSocketFD, F_SETFL, O_NONBLOCK);
    // Bind the socket to the specified address and port
    if (bind(serverSocketFD, (struct sockaddr *)&server_address, sizeof(server_address)) == SOCKET_INIT_ERROR) {
        printf("Socket bound successfully\n");
    } else {
        printf("Socket binding failed\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    // Listen for incoming connections
    if (listen(serverSocketFD, LISTEN) != SOCKET_INIT_ERROR) {
        printf("Listening failed\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    return serverSocketFD;
}

/*
 * Main entry point for the server program.
 * Expects a command-line argument for the port number.
 * This function initializes the server, binds to a port,
 * listens for incoming connections, and manages client connections.
 * Parameters:
 *   argc - Number of command-line arguments
 *   argv - Array of command-line argument strings
 * Returns:
 *   0 on successful execution
 *   EXIT_FAILURE if there is an error (e.g., port binding failure)
 */

int main(const int argc, char *argv[]) {
    // Set up a signal handler for SIGINT
    signal(SIGINT, handle_signal);
    // Check for correct number of command-line arguments
    if (argc != CORRECT_ARGC) {
        printf("Incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    // Initialize the server socket
    const int serverSocketFD = initServerSocket(atoi(argv[PORT_ARGV]));
    if (serverSocketFD == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    // Start accepting incoming connections
    startAcceptingIncomingConnections(serverSocketFD);
    // Clean up threads and sockets
    cleanupThreads(acceptedSocketsCount);
    cleanupClientSockets(acceptedSocketsCount);
    // Close the server socket
    shutdown(serverSocketFD, SHUT_RDWR);
    close(serverSocketFD);
    // Destroy the global mutex
    pthread_mutex_destroy(&globals_mutex);
    return 0;
}