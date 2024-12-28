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
#include "flag_file.h"

//defines
#define CORRECT_ARGC 2
#define SERVER_IP "0.0.0.0"
#define CLIENT_MAX "tlength:61;type:ERR;length:26;data:max 2 clients can connect\n"
#define INVALID_DATA "tlength:55;type:ERR;length:20;data:command not allowed\n"
#define WAIT_CLIENT "tlength:69;type:ERR;length:34;data:Wait for second client to connect\n"
#define SECOND_CLIENT_DISCONNECTED "tlength:66;type:ERR;length:31;data:\nSecond client disconnected ):\n"
#define DIR_REQUEST "tlength:41;type:FLG;length:7;data:FLG_DIR"
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
#define WIN_MSG "tlength:45;type:OUT;length:10;data:\nyou won!\n"
#define LOSE_MSG "tlength:48;type:OUT;length:13;data:\nyou lost ):\n"

//data types
struct AcceptedSocket {
    int acceptedSocketFD;
    struct sockaddr_in address;
    int error;
    int acceptedSuccessfully;
    char flag_data[32];
};

typedef struct {
    volatile sig_atomic_t stop_game; //stop a single game
    struct AcceptedSocket game_clients[MAX_CLIENTS]; //array of two client sockets
    unsigned int acceptedSocketsCount; //num of clients in a game; max 2
    pthread_mutex_t game_mutex; //mutex for a game
} Game;

//globals
volatile sig_atomic_t stop_all_games = 0;
struct AcceptedSocket acceptedSockets[MAX_CLIENTS] = {};
unsigned int acceptedSocketsCount = 0;
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

int check_message_received(char buffer[4096]);

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

void remove_client(int clientSocketFD);

int generate_message_for_clients(int clientSocketFD, char buffer[4096]);

int check_message_fields(const char *buffer);

int generate_client_flag(const char *buffer, int clientSocketFD);

bool check_winner(int clientSocketFD, char buffer[4096]);

int handle_client_flag(const char *buffer, int *flag_file_tries, int clientSocketFD, int *flag_okay_response,
                       int *flag_request_dir);

/*
 * Starts the process of accepting incoming connections on the server socket.
 * This is the main server loop that manages client connections, enforces
 * the maximum client limit, and initializes client handling threads.
 * It continues running until the server is stopped via signal.
 * Parameters: serverSocketFD - The file descriptor for the server socket.
 * Returns: None.
 */
void startAcceptingIncomingConnections(const int serverSocketFD) {
    while (!stop_all_games) {
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
    if (acceptedSocketsCount == 1) {
        //add to the last game with less than 2 accepted sockets
    } else if (acceptedSocketsCount == 0) {
        //create new game with malloc
    } else {
        //initialize to be 0 again
    }
    // Create new thread and pass socket FD as argument
    //deal with thread array
    pthread_t clientThread;
    /*Game *game = malloc(sizeof(Game));
    game->acceptedSocketFD = clientSocketFD->acceptedSocketFD;
    pthread_create(&clientThread,
                   NULL, receiveAndPrintIncomingData,
                   game);
    free(game);*/
    pthread_create(&clientThread,
                   NULL, receiveAndPrintIncomingData,
                   (void *) (intptr_t) clientSocketFD->acceptedSocketFD);
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
    pthread_detach(pthread_self());
    // Cast void pointer back to socket FD
    const int clientSocketFD = (intptr_t) arg;
    //const int clientSocketFD = ((Game *) arg)->acceptedSocketFD;
    int flag_file_tries = 0;
    int flag_request_dir = 0;
    int flag_okay_response = 0;
    s_send(clientSocketFD, DIR_REQUEST, strlen(DIR_REQUEST));
    while (!stop_all_games) {
        // Initialize buffer for incoming message
        char buffer[BUFFER_SIZE] = {0};
        // Receive data from client
        const ssize_t amountReceived = s_recv(clientSocketFD, buffer, sizeof(buffer));
        if (amountReceived > CHECK_RECEIVE) {
            // Null terminate received message
            buffer[amountReceived] = NULL_CHAR;
            // Log received message
            printf("%s\n", buffer);
            if (!(flag_okay_response && flag_request_dir)) {
                if (!handle_client_flag(buffer, &flag_file_tries, clientSocketFD, &flag_okay_response,
                                        &flag_request_dir)) {
                    break;
                }
            } else {
                //deal with client message and make an ideal response
                stop_all_games = generate_message_for_clients(clientSocketFD, buffer);
            }
        }
        // Exit if connection closed or server stopping
        if (amountReceived <= CHECK_RECEIVE || stop_all_games) {
            break;
        }
    }
    pthread_mutex_lock(&globals_mutex);
    if (acceptedSocketsCount > 0) {
        pthread_mutex_unlock(&globals_mutex);
        //send disconnect message and remove accepted socket from array
        sendReceivedMessageToTheOtherClients(SECOND_CLIENT_DISCONNECTED, clientSocketFD);
        remove_client(clientSocketFD);
        stop_all_games = 1;
    }
    pthread_mutex_unlock(&globals_mutex);
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
    memset(acceptedSocket.flag_data, NULL_CHAR, sizeof(acceptedSocket.flag_data));

    return acceptedSocket;
}

int check_message_fields(const char *buffer) {
    if (strstr(buffer, "type:") == NULL) {
        return false;
    }
    if (strlen(strstr(buffer, "type:") + TYPE_OFFSET) < 3) {
        return false;
    }
    if (strstr(buffer, "data:") == NULL) {
        return false;
    }
    return true;
}

/*
 * checks if data in buffer is type of CMD.
 * if it's CMD type checks for validity based upon banned words and allowed words.
 * if the message is not type CMD or valid CMD type.
 * received message is sent to other client.
 * Returns: None.
 */
int check_message_received(char buffer[4096]) {
    if (!check_message_fields(buffer)) {
        return false;
    }
    if (strncmp(strstr(buffer, "type:") + TYPE_OFFSET, DATA_CMD_CHECK, CMD_TYPE_LENGTH) ==
        CMP_EQUAL) {
        // Validate command data
        return check_command_data(strstr(buffer, "data:") + DATA_OFFSET);
    }
    return strncmp(strstr(buffer, "type:") + TYPE_OFFSET, "FLG", 3) !=
           CMP_EQUAL;
}

void remove_client(const int clientSocketFD) {
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

bool check_winner(const int clientSocketFD, char buffer[4096]) {
    pthread_mutex_lock(&globals_mutex);
    for (int i = 0; i < acceptedSocketsCount; i++) {
        if (acceptedSockets[i].acceptedSocketFD != clientSocketFD) {
            if (strcmp(strstr(buffer, "data:") + DATA_OFFSET, acceptedSockets[i].flag_data) == CMP_EQUAL) {
                pthread_mutex_unlock(&globals_mutex);
                return true;
            }
        }
    }
    pthread_mutex_unlock(&globals_mutex);
    return false;
}

int generate_message_for_clients(const int clientSocketFD, char buffer[4096]) {
    if (acceptedSocketsCount < MAX_CLIENTS) {
        //are there not 2 clients connected?
        pthread_mutex_lock(&globals_mutex);
        s_send(clientSocketFD, WAIT_CLIENT, strlen(WAIT_CLIENT));
        pthread_mutex_unlock(&globals_mutex);
    } else {
        if (check_winner(clientSocketFD, buffer)) {
            s_send(clientSocketFD, WIN_MSG, strlen(WIN_MSG));
            sendReceivedMessageToTheOtherClients(LOSE_MSG, clientSocketFD);
            return true;
        }
        if (check_message_received(buffer)) {
            sendReceivedMessageToTheOtherClients(buffer, clientSocketFD);
        } else {
            s_send(clientSocketFD, INVALID_DATA,
                   strlen(INVALID_DATA));
        }
    }
    return false;
}

int generate_client_flag(const char *buffer, const int clientSocketFD) {
    char flag_command[256] = {0};
    char random_str[32] = {0};
    generate_random_string(random_str, 31);
    if (snprintf(flag_command, sizeof(flag_command), "echo '%s' > %s/flag.txt", random_str, buffer) < sizeof(
            flag_command)) {
        char flag_command_buffer[512] = {0};
        if (prepare_buffer(flag_command_buffer, sizeof(flag_command_buffer), flag_command, "FLG")) {
            s_send(clientSocketFD, flag_command_buffer, strlen(flag_command_buffer));
            for (int i = 0; i < acceptedSocketsCount; i++) {
                if (acceptedSockets[i].acceptedSocketFD == clientSocketFD) {
                    strcpy(acceptedSockets[i].flag_data, random_str);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int handle_client_flag(const char *buffer, int *flag_file_tries, const int clientSocketFD, int *flag_okay_response,
                       int *flag_request_dir) {
    if (*flag_file_tries >= 5) {
        return false;
    }
    if (!check_message_fields(buffer)) {
        return false;
    }
    if (strncmp(strstr(buffer, "type:") + TYPE_OFFSET, "FLG", 3) != CMP_EQUAL) {
        return true;
    }
    if (strcmp(strstr(buffer, "data:") + DATA_OFFSET, "error") == CMP_EQUAL) {
        *flag_okay_response = 0;
        *flag_request_dir = 0;
    } else {
        if (!contains_banned_word(strstr(buffer, "type:") + TYPE_OFFSET) && !*flag_request_dir) {
            *flag_request_dir = generate_client_flag(strstr(buffer, "data:") + DATA_OFFSET, clientSocketFD);
            return true;
        }
        if (*flag_request_dir) {
            if (strcmp(strstr(buffer, "data:") + DATA_OFFSET, "okay") == CMP_EQUAL) {
                *flag_okay_response = 1;
                return true;
            }
        }
    }
    if (*flag_request_dir == 0) {
        s_send(clientSocketFD, DIR_REQUEST, strlen(DIR_REQUEST));
        *flag_file_tries += 1;
    }
    return true;
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
    stop_all_games = 1;
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
    cleanupClientSockets(acceptedSocketsCount);
    shutdown(serverSocketFD, SHUT_RDWR);
    close(serverSocketFD);
    pthread_mutex_destroy(&globals_mutex);

    return 0;
}
