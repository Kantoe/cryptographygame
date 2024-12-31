/*
 * Ido Kantor
 * Client for game sends and receives messages from other clients
 * This file implements a client that connects to a game server, handles
 * message sending and receiving, and processes various types of server responses
 */

#include <pthread.h>
#include <signal.h>
#include "cryptography_game_util.h"
#include "flag_file.h"

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
#define FLAG_ERROR "tlength:39;type:FLG;length:5;data:error"
#define FLAG_OKAY "tlength:38;type:FLG;length:4;data:okay"

//globals
volatile int connectionClosed = 0;
char my_cwd[1024] = {0};
char command_cwd[1024] = {0};
pthread_mutex_t cwd_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t sync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sync_cond = PTHREAD_COND_INITIALIZER;
volatile bool ready_to_print = true;
char flag_path[512] = {0};
int socketFD = -1;

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
void *listenAndPrint(void *arg);

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
void process_received_data(int socketFD, char data[1024], char type[1024], char length[1024], bool *flag_requests);

/*
* Synchronizes console output using thread synchronization primitives.
* This function uses mutex locks and condition variables to ensure
* thread-safe console output. It waits for the ready_to_print signal,
* then prints the current working directory prompt. Used to coordinate
* output between multiple threads.
* Parameters: None
* Returns: None
*/
void wait_for_print(void);

/*
* Processes received data from the server based on message type.
* Handles four different message types:
*   - OUT: Displays standard output to console
*   - CMD: Executes command and sends results back
*   - ERR: Displays error messages to console
*   - CWD: Updates current working directory
* Uses thread-safe operations for directory and command handling.
* Parameters:
*   socketFD - Socket file descriptor for communication
*   current_data - The message data to process
*   current_type - Type identifier of the message (OUT/CMD/ERR/CWD)
*   n - Length of the current data to process
* Returns: None
*/
void process_message_type(int socketFD, char *current_data, const char *current_type, int n, bool *flag_requests);

/*
* Synchronizes console output using thread synchronization primitives.
* This function uses mutex locks and condition variables to ensure
* thread-safe console output. It waits for the ready_to_print signal,
* then prints the current working directory prompt. Used to coordinate
* output between multiple threads.
* Parameters: None
* Returns: None
*/


void wait_for_print(void) {
    // Wait until allowed to print
    pthread_mutex_lock(&sync_mutex);
    while (!ready_to_print) {
        pthread_cond_wait(&sync_cond, &sync_mutex);
    }
    ready_to_print = false; // Reset the flag
    pthread_mutex_unlock(&sync_mutex);
    pthread_mutex_lock(&cwd_mutex);
    printf("%s$ ", my_cwd);
    pthread_mutex_unlock(&cwd_mutex);
}

/*
 * Reads user input from the console and sends it to the server.
 * This function handles the main input loop, processes user commands,
 * and manages the command buffer preparation. It also handles the exit command
 * and connection closure scenarios.
 * Parameters: socketFD - The file descriptor for the socket connected to the server.
 * Returns: None.
 */
void readConsoleEntriesAndSendToServer(const int socketFD) {
    char *line = NULL;
    size_t lineSize = 0;
    // Main loop for reading and sending messages
    while (!connectionClosed) {
        wait_for_print();
        const ssize_t charCount = getline(&line, &lineSize, stdin);
        // Process input only if valid characters were read
        if (charCount > CHECK_LINE_SIZE) {
            char buffer[BUFFER_SIZE] = {0};
            // Remove trailing newline and prepare the message
            line[charCount - REMOVE_NEWLINE] = REPLACE_NEWLINE;
            if (charCount <= BUFFER_SIZE) {
                sprintf(buffer, "%s", line);
            }
            // Check for connection status and exit command
            if (connectionClosed) {
                break;
            }
            if (strcmp(line, "exit") == CHECK_EXIT) {
                break;
            }
            // Prepare and send the message to server
            prepare_buffer(buffer, sizeof(buffer), line, "CMD");
            const ssize_t send_check = s_send(socketFD, buffer, strlen(buffer));
            if (send_check == CHECK_SEND) {
                printf("send failed, Connection closed\n");
                break;
            }
        }
        usleep(50000);
    }
    free(line); //Free the memory allocated by getLine func
}

/* Starts a new thread to listen for messages from the server
 * and print them to the console. This function creates a dedicated thread
 * that handles all incoming server communications asynchronously,
 * allowing the main thread to focus on user input.
 * Parameters: socketFD - The file descriptor for the
 * socket connected to the server. */
void startListeningAndPrintMessagesOnNewThread(const int socketFD) {
    pthread_t id;
    // Create new thread for message listening
    pthread_create(&id, NULL, listenAndPrint, (void *) (intptr_t) socketFD);
}

bool handle_flag_requests(const int socketFD, const char *current_data, const int n) {
    if (strncmp(current_data, "FLG_DIR", n) == CMP_EQUAL) {
        char path[256] = {0};
        if (generate_random_path_name(path, sizeof(path)) == STATUS_OKAY) {
            char buffer[512] = {0};
            prepare_buffer(buffer, sizeof(buffer), path, "FLG");
            s_send(socketFD, buffer, strlen(buffer));
            memset(flag_path, 0, sizeof(flag_path));
            strcpy(flag_path, path);
        } else {
            s_send(socketFD, FLAG_ERROR, strlen(FLAG_ERROR));
        }
        return true;
    }
    char command[n + 1];
    memset(command, 0, n + 1);
    strncpy(command, current_data, n);
    if (create_or_delete_flag_file(command) == STATUS_OKAY) {
        strcat(flag_path, "/flag.txt");
        s_send(socketFD, FLAG_OKAY, strlen(FLAG_OKAY));
        return false;
    }
    s_send(socketFD, FLAG_ERROR, strlen(FLAG_ERROR));
    return true;
}

/*
* Processes received data from the server based on message type.
* Handles four different message types:
*   - OUT: Displays standard output to console
*   - CMD: Executes command and sends results back
*   - ERR: Displays error messages to console
*   - CWD: Updates current working directory
* Uses thread-safe operations for directory and command handling.
* Parameters:
*   socketFD - Socket file descriptor for communication
*   current_data - The message data to process
*   current_type - Type identifier of the message (OUT/CMD/ERR/CWD)
*   n - Length of the current data to process
* Returns: None
*/
void process_message_type(const int socketFD, char *current_data, const char *current_type, const int n,
                          bool *flag_requests) {
    if (strcmp(current_type, "OUT") == CMP_EQUAL) {
        printf("%.*s", n, current_data);
    } else if (strcmp(current_type, "CMD") == CMP_EQUAL) {
        // Allocate memory for command and process it
        char *command = malloc(n + NULL_CHAR_LEN);
        if (command != NULL) {
            strncpy(command, current_data, n);
            command[n] = NULL_CHAR;
        }
        pthread_mutex_lock(&cwd_mutex);
        execute_command_and_send(command, n + NULL_CHAR_LEN, socketFD,
                                 command_cwd, sizeof(command_cwd));
        pthread_mutex_unlock(&cwd_mutex);
        free(command);
    } else if (strcmp(current_type, "ERR") == CMP_EQUAL) {
        fprintf(stderr, "%.*s", n, current_data);
    } else if (strcmp(current_type, "CWD") == CMP_EQUAL) {
        pthread_mutex_lock(&cwd_mutex);
        memset(my_cwd, NULL_CHAR, sizeof(my_cwd));
        strncpy(my_cwd, current_data, n);
        pthread_mutex_unlock(&cwd_mutex);
    } else if (strcmp(current_type, "FLG") == CMP_EQUAL && flag_requests) {
        *flag_requests = handle_flag_requests(socketFD, current_data, n);
    }
}

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
void process_received_data(const int socketFD, char data[1024], char type[1024], char length[1024],
                           bool *flag_requests) {
    char *current_data = data;
    char *type_context, *length_context;
    // Initialize tokenization of message type and length
    const char *current_type = strtok_r(type, ";", &type_context);
    const char *current_length = strtok_r(length, ";", &length_context);
    // Process each message segment
    while (current_length != NULL && current_type != NULL) {
        const int n = atoi(current_length);
        // Handle different message types (OUT, CMD, ERR)
        process_message_type(socketFD, current_data, current_type, n, flag_requests);
        // Move to next message segment
        current_data += n;
        current_type = strtok_r(NULL, ";", &type_context);
        current_length = strtok_r(NULL, ";", &length_context);
    }
}

/*
 * Function executed by the listener thread that receives messages from the server.
 * This is the main message processing loop that continuously monitors for incoming
 * data and handles different types of server responses including commands and errors.
 * Parameters: arg - A pointer to the socket file descriptor cast to void*.
 * Returns: NULL upon completion.
 */
void *listenAndPrint(void *arg) {
    pthread_detach(pthread_self());
    const int socketFD = (intptr_t) arg;
    bool flag_requests = true;
    // Continuous listening loop for server messages
    while (TRUE) {
        char buffer[BUFFER_SIZE] = {0};
        const ssize_t amountReceived = s_recv(socketFD, buffer, sizeof(buffer));
        // Initialize buffers for message parsing
        char data[1024] = {0};
        char type[1024] = {0};
        char length[1024] = {0};
        // Process received data if valid
        if (amountReceived > CHECK_RECEIVE) {
            buffer[amountReceived] = NULL_CHAR;
            // Parse and process the received packet
            const int check = parse_received_packets(buffer, data, type,
                                                     length, strlen(buffer), sizeof(length),
                                                     sizeof(data), sizeof(type));
            if (check) {
                process_received_data(socketFD, data, type, length, &flag_requests);
            }
        } else {
            // Handle connection closure
            pthread_mutex_lock(&sync_mutex);
            ready_to_print = true;
            pthread_cond_signal(&sync_cond);
            pthread_mutex_unlock(&sync_mutex);
            printf("\nConnection closed, press any key to exit\n");
            connectionClosed = 1;
            break;
        }
        pthread_mutex_lock(&sync_mutex);
        ready_to_print = true;
        pthread_cond_signal(&sync_cond);
        pthread_mutex_unlock(&sync_mutex);
    }
    close(socketFD);
    return NULL;
}

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
int initClientSocket(const char *ip, const char *port) {
    // Create TCP/IPv4 socket
    const int socketFD = createTCPIpv4Socket();
    if (socketFD == SOCKET_ERROR) {
        printf("failed to create socket\n");
        return EXIT_FAILURE;
    }
    // Set up server address structure
    struct sockaddr_in address;
    createIPv4Address(ip, atoi(port), &address);
    if (createIPv4Address(ip, atoi(port), &address) == SOCKET_INIT_ERROR) {
        printf("Incorrect IP or port\n");
        close(socketFD);
        return EXIT_FAILURE;
    }
    // Attempt connection to server
    if (connect(socketFD, (struct sockaddr *) &address, sizeof(address))
        == SOCKET_INIT_ERROR) {
        printf("connection was successful\n");
    } else {
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

void delete_flag_file() {
    if (strlen(flag_path) <= 0) {
        return;
    }
    char command[515] = {0};
    snprintf(command, sizeof(command), "rm %s", flag_path);
    create_or_delete_flag_file(command);
}

void termination_handler(const int signal) {
    printf("\nCaught signal %d (%s)\n", signal, strsignal(signal));
    delete_flag_file();
    pthread_mutex_destroy(&cwd_mutex);
    pthread_mutex_destroy(&sync_mutex);
    pthread_cond_destroy(&sync_cond);
    close(socketFD);
    exit(EXIT_FAILURE);
}

void init_signal_handle() {
    struct sigaction sa;
    // Set up the signal handler
    sa.sa_handler = termination_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    // List of termination signals to handle
    const int signals[] = {SIGINT, SIGTERM, SIGQUIT, SIGHUP};
    const size_t num_signals = sizeof(signals) / sizeof(signals[0]);
    for (size_t i = 0; i < num_signals; i++) {
        if (sigaction(signals[i], &sa, NULL) == -1) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }
}

int main(const int argc, char *argv[]) {
    // Validate command line arguments
    if (argc != CORRECT_ARGC) {
        printf("incorrect number of arguments\n");
        return EXIT_FAILURE;
    }
    // Initialize socket and start client
    socketFD = initClientSocket(argv[IP_ARGV], argv[PORT_ARGV]);
    if (socketFD == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }
    strcpy(my_cwd, "/home");
    strcpy(command_cwd, "/home");
    // Start message listening thread and handle user input
    startListeningAndPrintMessagesOnNewThread(socketFD);
    //initiate signal handler
    init_signal_handle();
    //start reading console entries
    readConsoleEntriesAndSendToServer(socketFD);
    delete_flag_file();
    pthread_mutex_destroy(&cwd_mutex);
    pthread_mutex_destroy(&sync_mutex);
    pthread_cond_destroy(&sync_cond);
    close(socketFD);
    return 0;
}
