/*
 * Ido Kantor
 * Client for game sends and receives messages from other clients
 * This file implements a client that connects to a game server, handles
 * message sending and receiving, and processes various types of server responses
 */

#include <pthread.h>
#include "cryptography_game_util.h"

//defines
#define CORRECT_ARGC 3
#define REMOVE_NEWLINE 1
#define REPLACE_NEWLINE 0
#define CHECK_RECEIVE 0
#define NULL_CHAR 0
#define TRUE 1
#define BUFFER_SIZE 4096
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
 * and print them to the console. This function creates a dedicated thread
 * that handles all incoming server communications asynchronously,
 * allowing the main thread to focus on user input.
 * Parameters: socketFD - The file descriptor for the
 * socket connected to the server. */
void startListeningAndPrintMessagesOnNewThread(int socketFD);

/*
 * Function executed by the listener thread that receives messages from the server.
 * This is the main message processing loop that continuously monitors for incoming
 * data and handles different types of server responses including commands and errors.
 * Parameters: arg - A pointer to the socket file descriptor cast to void*.
 * Returns: NULL upon completion.
 */
void * listenAndPrint(void * arg);

/*
 * Reads user input from the console and sends it to the server.
 * This function handles the main input loop, processes user commands,
 * and manages the command buffer preparation. It also handles the exit command
 * and connection closure scenarios.
 * Parameters: socketFD - The file descriptor for the socket connected to the server.
 * Returns: None.
 */
void readConsoleEntriesAndSendToServer(int socketFD);

/*
 * Initializes the client socket, connects to the server,
 * and returns the socket file descriptor. This function handles all network
 * setup including IPv4 address creation and connection establishment.
 * It includes error handling for various network setup failures.
 * Parameters: ip - The server's IP address as a string.
 * port - The server's port number as a string.
 * Returns: A valid socket file descriptor if successful;
 * EXIT_FAILURE if there is an error (such as socket creation
 * failure or connection failure).
 */
int initClientSocket(const char *ip, const char *port);

/*
 * Processes received data from the server based on message type.
 * This function handles three different message types: OUT (output),
 * CMD (commands), and ERR (errors). It parses incoming messages and
 * processes each segment according to its type and length.
 * Parameters:
 *   socketFD - The socket file descriptor
 *   data - Buffer containing the message data
 *   type - Buffer containing message type information
 *   length - Buffer containing message length information
 * Returns: None
 */
void process_received_data(int socketFD, char data[1024], char type[1024], char length[1024]);


void readConsoleEntriesAndSendToServer(const int socketFD) {
    char *line = NULL;
    size_t lineSize = 0;
    // Main loop for reading and sending messages
    while(!connectionClosed) {
        const ssize_t charCount = getline(&line, &lineSize, stdin);
        // Process input only if valid characters were read
        if(charCount > CHECK_LINE_SIZE) {
            char buffer[BUFFER_SIZE] = {0};
            // Remove trailing newline and prepare the message
            line[charCount - REMOVE_NEWLINE] = REPLACE_NEWLINE;
            if(charCount <= BUFFER_SIZE) {
                sprintf(buffer, "%s", line);
            }
            // Check for connection status and exit command
            if(connectionClosed) {
                break;
            }
            if(strcmp(line, "exit") == CHECK_EXIT) {
                break;
            }
            // Prepare and send the message to server
            prepare_buffer(buffer, sizeof(buffer), line, "CMD");
            const ssize_t send_check = s_send(socketFD, buffer, strlen(buffer));
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
    // Create new thread for message listening
    pthread_create(&id, NULL, listenAndPrint, (void *)(intptr_t)socketFD);
}

void process_received_data(const int socketFD, char data[1024], char type[1024], char length[1024]) {
    char *current_data = data;
    char *type_context, *length_context;
    // Initialize tokenization of message type and length
    const char *current_type = strtok_r(type, ";", &type_context);
    const char *current_length = strtok_r(length, ";", &length_context);
    // Process each message segment
    while (current_length != NULL && current_type != NULL) {
        const int n = atoi(current_length);
        // Handle different message types (OUT, CMD, ERR)
        if (strcmp(current_type, "OUT") == 0) {
            printf("%.*s", n, current_data);
        }
        else if (strcmp(current_type, "CMD") == 0) {
            // Allocate memory for command and process it
            char *command = malloc(n + 1);
            if (command != NULL) {
                strncpy(command, current_data, n);
                command[n] = 0;
            }
            execute_command_and_send(command, sizeof(command), socketFD);
            free(command);
        }
        else if (strcmp(current_type, "ERR") == 0) {
            printf("%.*s", n, current_data);
        }
        // Move to next message segment
        current_data += n;
        current_type = strtok_r(NULL, ";", &type_context);
        current_length = strtok_r(NULL, ";", &length_context);
    }
}

void * listenAndPrint(void * arg) {
    const int socketFD = (intptr_t)arg;
    // Continuous listening loop for server messages
    while(TRUE) {
        char buffer[BUFFER_SIZE] = {0};
        const ssize_t amountReceived = s_recv(socketFD ,buffer, sizeof(buffer));
        // Initialize buffers for message parsing
        char data [1024] = {0};
        char type [1024] = {0};
        char length [1024] = {0};
        // Process received data if valid
        if(amountReceived > CHECK_RECEIVE) {
            buffer[amountReceived] = NULL_CHAR;
            // Parse and process the received packet
            const int check = parse_received_packets(buffer, data, type,
                    length, strlen(buffer), sizeof(length),
                    sizeof(data), sizeof(type));
            if(check) {
                process_received_data(socketFD, data, type, length);
            }
        }
        else {
            // Handle connection closure
            printf("Connection closed, press any key to exit\n");
            connectionClosed = 1;
            break;
        }
    }
    close(socketFD);
    return NULL;
}

int initClientSocket(const char *ip, const char *port) {
    // Create TCP/IPv4 socket
    const int socketFD = createTCPIpv4Socket();
    if(socketFD == SOCKET_ERROR) {
        printf("failed to create socket\n");
        return EXIT_FAILURE;
    }
    // Set up server address structure
    struct sockaddr_in address;
    createIPv4Address(ip, atoi(port), &address);
    if(createIPv4Address(ip, atoi(port), &address) == SOCKET_INIT_ERROR) {
        printf("Incorrect IP or port\n");
        close(socketFD);
        return EXIT_FAILURE;
    }
    // Attempt connection to server
    if(connect(socketFD, (struct sockaddr *)&address,sizeof(address))
        == SOCKET_INIT_ERROR) {
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
 * Main entry point for the client program.
 * Expects command line arguments for server IP and port.
 * This function initializes the client, establishes the server connection,
 * and manages the main program flow including thread creation and cleanup.
 * Parameters:
 *   argc - Number of command line arguments
 *   argv - Array of command line argument strings
 * Returns:
 *   0 on successful execution
 *   EXIT_FAILURE if incorrect arguments or connection fails
 */

int main(const int argc, char * argv[])
{
    // Validate command line arguments
    if(argc != CORRECT_ARGC) {
        printf("incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    // Initialize socket and start client
    const int socketFD = initClientSocket(argv[IP_ARGV], argv[PORT_ARGV]);
    if(socketFD == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    // Start message listening thread and handle user input
    startListeningAndPrintMessagesOnNewThread(socketFD);
    readConsoleEntriesAndSendToServer(socketFD);
    close(socketFD);
    return 0;
}