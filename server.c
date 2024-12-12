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
#define CLIENT_MAX "tlength:61;type:ERR;length:26;data:max 2 clients can connect\n"
#define INVALID_DATA "tlength:55;type:ERR;length:20;data:command not allowed\n"
#define WAIT_CLIENT "tlength:69;type:ERR;length:34;data:Wait for second client to connect\n"
#define SECOND_CLIENT_DISCONNECTED "tlength:66;type:ERR;length:31;data:\nSecond client disconnected ):\n"
#define INIT_COMMAND_DIR "tlength:42;type:CMD;length:8;data:cd /home"
#define INIT_MY_DIR "tlength:39;type:CWD;length:5;data:/home"
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
#define CMD_TYPE_LENGTH 3
#define TYPE_OFFSET 5
#define DATA_CMD_CHECK "CMD"
#define DATA_OFFSET 5

//data types
struct AcceptedSocket {
    int acceptedSocketFD;
    struct sockaddr_in address;
    int error;
    int acceptedSuccessfully;
};

//globals
volatile sig_atomic_t stop = 0;
struct AcceptedSocket acceptedSockets[MAX_CLIENTS] = {};
unsigned int acceptedSocketsCount = 0;
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
void *receiveAndPrintIncomingData(void *arg);

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
void sendReceivedMessageToTheOtherClients(const char *buffer, int socketFD);

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

/*
 * checks if data in buffer is type of CMD.
 * if it's CMD type checks for validity based upon banned words and allowed words.
 * if the message is not type CMD or valid CMD type.
 * received message is sent to other client.
 * Returns: None.
 */

void check_message_received(int clientSocketFD, char buffer[4096]);

/*
 * creates server socket and address for it.
 * returns 1 for failure 0 for success.
 */

int config_socket(int port, int *serverSocketFD, struct sockaddr_in *server_address);

/*
 * binds server socket to an address
 * listens on that server socket
 */
int bind_and_listen_on_socket(int serverSocketFD, struct sockaddr_in server_address);

/*
 * Starts the process of accepting incoming connections on the server socket.
 * This is the main server loop that manages client connections, enforces
 * the maximum client limit, and initializes client handling threads.
 * It continues running until the server is stopped via signal.
 * Parameters: serverSocketFD - The file descriptor for the server socket.
 * Returns: None.
 */
void startAcceptingIncomingConnections(const int serverSocketFD) {
    while (!stop) {
        // Lock mutex before checking client count
        pthread_mutex_lock(&globals_mutex);
        if (acceptedSocketsCount < MAX_CLIENTS) {
            // Unlock mutex before accepting new connection
            pthread_mutex_unlock(&globals_mutex);
            struct AcceptedSocket clientSocket =
                    acceptIncomingConnection(serverSocketFD);

            // If connection accepted successfully, add to active clients
            if (clientSocket.acceptedSuccessfully) {
                // Lock mutex before updating shared data
                pthread_mutex_lock(&globals_mutex);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    //choose where to add client in array
                    if (!acceptedSockets[i].acceptedSuccessfully || acceptedSockets[i].acceptedSocketFD ==
                        SOCKET_ERROR) {
                        acceptedSockets[i] = clientSocket;
                        acceptedSocketsCount++;
                        break;
                    }
                }
                receiveAndPrintIncomingDataOnSeparateThread(&clientSocket);
                pthread_mutex_unlock(&globals_mutex);
            }
        } else {
            // Server at capacity, reject new connection
            pthread_mutex_unlock(&globals_mutex);
            const int clientSocketFD = accept(serverSocketFD, NULL, NULL);
            // Send max clients error message
            s_send(clientSocketFD, CLIENT_MAX,
                   strlen(CLIENT_MAX));
            close(clientSocketFD);
        }
        // Sleep to prevent CPU overload
        usleep(SLEEP);
    }
}

/*
 * Creates a new thread to receive and print data from the connected client.
 * This function initializes the thread handling for each new client connection,
 * setting up the necessary structures for message processing.
 * Parameters: clientSocketFD - A pointer to an AcceptedSocket structure
 * containing the client socket file descriptor.
 * Returns: None.
 */
void receiveAndPrintIncomingDataOnSeparateThread(
    const struct AcceptedSocket *clientSocketFD) {
    // Create new thread and pass socket FD as argument
    pthread_create(&clientThreads[acceptedSocketsCount - PTHREAD_CREATE],
                   NULL, receiveAndPrintIncomingData,
                   (void *) (intptr_t) clientSocketFD->acceptedSocketFD);
}

/*
 * checks if data in buffer is type of CMD.
 * if it's CMD type checks for validity based upon banned words and allowed words.
 * if the message is not type CMD or valid CMD type.
 * received message is sent to other client.
 * Returns: None.
 */
void check_message_received(const int clientSocketFD, char buffer[4096]) {
    if (acceptedSocketsCount < MAX_CLIENTS) {
        pthread_mutex_lock(&globals_mutex);
        s_send(clientSocketFD, WAIT_CLIENT, strlen(WAIT_CLIENT));
        pthread_mutex_unlock(&globals_mutex);
        return;
    }
    int valid_data_check = 1;
    // Check if message is a command type
    if (strstr(buffer, "type:") == NULL) {
        valid_data_check = 0;
    } else if (strlen(strstr(buffer, "type:") + TYPE_OFFSET) < CMD_TYPE_LENGTH) {
        valid_data_check = 0;
    } else if (strstr(buffer, "data:") == NULL) {
        valid_data_check = 0;
    }
    if (valid_data_check && strncmp(strstr(buffer, "type:") + TYPE_OFFSET, DATA_CMD_CHECK, CMD_TYPE_LENGTH) ==
        CMP_EQUAL) {
        // Validate command data
        valid_data_check = check_command_data(strstr(buffer, "data:") + DATA_OFFSET);
    }
    if (valid_data_check) {
        // Broadcast valid message to other clients
        sendReceivedMessageToTheOtherClients(buffer, clientSocketFD);
    } else {
        // Send error for invalid command
        s_send(clientSocketFD, INVALID_DATA,
               strlen(INVALID_DATA));
    }
}

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
void *receiveAndPrintIncomingData(void *arg) {
    // Cast void pointer back to socket FD
    const int clientSocketFD = (intptr_t) arg;
    while (!stop) {
        // Initialize buffer for incoming message
        char buffer[BUFFER_SIZE] = {0};
        // Receive data from client
        const ssize_t amountReceived = s_recv(clientSocketFD, buffer, sizeof(buffer));

        if (amountReceived > CHECK_RECEIVE) {
            // Null terminate received message
            buffer[amountReceived] = NULL_CHAR;
            // Log received message
            printf("%s\n", buffer);
            check_message_received(clientSocketFD, buffer);
        }
        // Exit if connection closed or server stopping
        if (amountReceived <= CHECK_RECEIVE || stop) {
            break;
        }
    }
    if (acceptedSocketsCount > 0) {
        //send disconnect message and remove accepted socket from array
        sendReceivedMessageToTheOtherClients(SECOND_CLIENT_DISCONNECTED, clientSocketFD);
        pthread_mutex_lock(&globals_mutex);
        for (int i = 0; i < acceptedSocketsCount; i++) {
            if (acceptedSockets[i].acceptedSocketFD == clientSocketFD) {
                acceptedSockets[i].acceptedSocketFD = SOCKET_ERROR;
            }
        }
        pthread_mutex_unlock(&globals_mutex);
        pthread_mutex_lock(&globals_mutex);
        acceptedSocketsCount--;
        pthread_mutex_unlock(&globals_mutex);
    }
    // Cleanup client socket
    close(clientSocketFD);
    return NULL;
}

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
void sendReceivedMessageToTheOtherClients(const char *buffer, const int socketFD) {
    // Lock mutex before accessing shared client data
    pthread_mutex_lock(&globals_mutex);

    // Iterate through all connected clients
    for (int i = 0; i < acceptedSocketsCount; i++) {
        // Skip sender's socket
        if (acceptedSockets[i].acceptedSocketFD != socketFD) {
            // Forward message to other client
            s_send(acceptedSockets[i].acceptedSocketFD, buffer, strlen(buffer));
        }
    }

    // Release mutex after broadcasting
    pthread_mutex_unlock(&globals_mutex);
}

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
struct AcceptedSocket acceptIncomingConnection(const int serverSocketFD) {
    // Initialize socket structure
    struct AcceptedSocket acceptedSocket = {0};
    struct sockaddr_in clientAddress;
    int clientAddressSize = sizeof(struct sockaddr_in);

    // Accept new connection
    const int clientSocketFD = accept(serverSocketFD, (struct sockaddr *) &clientAddress,
                                      (socklen_t *) &clientAddressSize);

    // Set socket information
    acceptedSocket.address = clientAddress;
    acceptedSocket.acceptedSocketFD = clientSocketFD;
    acceptedSocket.acceptedSuccessfully = clientSocketFD > ACCEPTED_SUCCESSFULLY;
    acceptedSocket.error = clientSocketFD < ACCEPTED_SUCCESSFULLY ? clientSocketFD : ACCEPTED_SUCCESSFULLY;

    return acceptedSocket;
}

/*
 * Cleans up and cancels client threads based on the count provided.
 * This function ensures proper thread termination and resource cleanup,
 * preventing memory leaks and zombie threads.
 * Parameters: count - The number of client threads to clean up.
 * Returns: None.
 */
void cleanupThreads(const int count) {
    // Iterate through all client threads
    for (int i = 0; i < count; i++) {
        // Cancel and join active threads
        if (clientThreads[i] != CLIENT_THREAD_EXISTS) {
            pthread_cancel(clientThreads[i]);
            pthread_join(clientThreads[i], NULL);
        }
    }
}

/*
 * Closes all accepted client sockets based on the count provided.
 * This function handles the cleanup of network resources,
 * ensuring all connections are properly terminated.
 * Parameters: count - The number of accepted client sockets to close.
 * Returns: None.
 */
void cleanupClientSockets(const int count) {
    // Iterate through all client sockets
    for (int i = 0; i < count; i++) {
        // Close active sockets
        if (acceptedSockets[i].acceptedSocketFD > CLIENT_SOCKET_EXISTS) {
            close(acceptedSockets[i].acceptedSocketFD);
        }
    }
}

/*
 * Handles signals, specifically SIGINT (press ctrl+c)
 * (interrupt signal) to safely terminate the server.
 * This function ensures proper cleanup of resources when
 * the server is interrupted, preventing memory leaks and
 * hanging connections.
 * Parameters: signal - The signal number received (e.g., SIGINT).
 */
void handle_signal(const int signal) {
    // Print a message indicating the signal received
    printf("Caught signal %d\n", signal);
    // Set the `stop` flag to trigger cleanup
    stop = 1;
}

/*
 * creates server socket and address for it.
 * returns 1 for failure 0 for success.
 */
int config_socket(const int port, int *serverSocketFD, struct sockaddr_in *server_address) {
    *serverSocketFD = createTCPIpv4Socket();
    if (*serverSocketFD == SOCKET_ERROR) {
        printf("Error creating server socket\n");
        return EXIT_FAILURE;
    }

    if (createIPv4Address(SERVER_IP, port, server_address) == SOCKET_INIT_ERROR) {
        printf("Incorrect IP or port\n");
        close(*serverSocketFD);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/*
 * binds server socket to an address
 * listens on that server socket
 */
int bind_and_listen_on_socket(const int serverSocketFD, struct sockaddr_in server_address) {
    if (bind(serverSocketFD, (struct sockaddr *) &server_address, sizeof(server_address)) == SOCKET_INIT_ERROR) {
        printf("Socket bound successfully\n");
    } else {
        printf("Socket binding failed\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }

    // Start listening for connections
    if (listen(serverSocketFD, LISTEN) != SOCKET_INIT_ERROR) {
        printf("Listening failed\n");
        close(serverSocketFD);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

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
int initServerSocket(const int port) {
    int serverSocketFD;
    struct sockaddr_in server_address;
    if (config_socket(port, &serverSocketFD, &server_address)) {
        return EXIT_FAILURE;
    }
    // Set socket to non-blocking mode
    fcntl(serverSocketFD, F_SETFL, O_NONBLOCK);
    // Bind socket to address and port
    if (bind_and_listen_on_socket(serverSocketFD, server_address)) {
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
    // Set up signal handler
    signal(SIGINT, handle_signal);

    // Validate command line arguments
    if (argc != CORRECT_ARGC) {
        printf("Incorrect number of arguments\n");
        return EXIT_FAILURE;
    }

    // Initialize server
    const int serverSocketFD = initServerSocket(atoi(argv[PORT_ARGV]));
    if (serverSocketFD == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    // Start server main loop
    startAcceptingIncomingConnections(serverSocketFD);

    // Cleanup resources
    cleanupThreads(acceptedSocketsCount);
    cleanupClientSockets(acceptedSocketsCount);
    shutdown(serverSocketFD, SHUT_RDWR);
    close(serverSocketFD);
    pthread_mutex_destroy(&globals_mutex);

    return 0;
}
