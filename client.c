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
#include "gui_fltk.h"
#include "key_exchange.h"

//defines
#define CORRECT_ARGC 3
#define REMOVE_NEWLINE 1
#define REPLACE_NEWLINE 0
#define CHECK_RECEIVE 0
#define NULL_CHAR 0
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
#define KEY_ERROR "tlength:39;type:KEY;length:5;data:error"
#define KEY_OKAY "tlength:38;type:KEY;length:4;data:okay"
#define FLAG_PATH_SIZE 512
#define MY_CWD_SIZE 1024
#define COMMAND_CWD_SIZE 1024
#define SIGACTION_ERROR -1
#define SIGNAL_CODE 128
#define SLEEP 1000000

struct ThreadArgs {
    int socketFD;
    const unsigned char *encryption_key;
};

//globals
char my_cwd[MY_CWD_SIZE] = {NULL_CHAR};
char command_cwd[COMMAND_CWD_SIZE] = {NULL_CHAR};
pthread_mutex_t cwd_mutex = PTHREAD_MUTEX_INITIALIZER;
char flag_path[FLAG_PATH_SIZE] = {NULL_CHAR};
char key_path[512] = {NULL_CHAR};
int socketFD = -1;

//prototypes

void print_hex(const unsigned char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

/*
 * sleeps for nano_sec (nano seconds)
 */
void m_sleep(unsigned int nano_sec);

/*
 * startListeningAndPrintMessagesOnNewThread: Creates listener thread
 *
 * Args:
 * - socketFD: File descriptor for the connected socket
 *
 * Purpose: Spawns a new thread to handle incoming server messages
 *
 * Operation:
 * Creates detached thread running listenAndPrint function
 * Passes socketFD as argument after casting to void pointer
 *
 * Returns: None
 */
void startListeningAndPrintMessagesOnNewThread(int socketFD, const unsigned char *encryption_key);

/*
 * listenAndPrint: Server message processing thread
 *
 * Args:
 * - arg: void pointer to socket file descriptor
 *
 * Purpose: Handles all incoming server communication
 *
 * Operation:
 * 1. Runs continuous receive loop until connection closes
 * 2. Parses received packets into type/length/data components
 * 3. Processes valid messages through process_received_data
 * 4. Handles connection closure and cleanup
 * 5. Manages thread synchronization for console output
 *
 * Returns: NULL
 */
void *listenAndPrint(void *arg);

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
 * process_received_data: Parses and processes server messages
 *
 * Args:
 * - socketFD: Socket file descriptor
 * - data: Buffer containing message data
 * - type: Buffer containing message types
 * - length: Buffer containing message lengths
 * - flag_requests: Flag processing state pointer
 *
 * Purpose: Breaks down and processes multi-part server messages
 *
 * Operation:
 * 1. Uses strtok_r to parse semicolon-delimited message segments
 * 2. For each segment: extracts type, length, and data
 * 3. Calls process_message_type for each segment
 * 4. Advances data pointer based on segment length
 *
 * Returns: None
 */
void process_received_data(int socketFD, const unsigned char *encryption_key, char data[1024], char type[1024],
                           char length[1024], bool *flag_requests,
                           bool *key_requests);

/*
 * process_message_type: Message type-specific processing
 *
 * Args:
 * - socketFD: Socket file descriptor
 * - current_data: Message payload
 * - current_type: Message type (OUT/CMD/ERR/CWD/FLG)
 * - n: Length of data
 * - flag_requests: Flag processing state pointer
 *
 * Purpose: Routes and processes messages based on their type
 *
 * Operation:
 * 1. OUT: Prints data to console
 * 2. CMD: Executes command and sends results back
 * 3. ERR: Prints error in red to stderr
 * 4. CWD: Updates current working directory
 * 5. FLG: Handles flag-related operations
 *
 * Returns: None
 */
void process_message_type(int socketFD, const unsigned char *encryption_key, const char *current_data,
                          const char *current_type, int n, bool *flag_requests,
                          bool *key_requests);

/*
 * handle_flag_requests: Processes flag-related server commands
 *
 * Args:
 * - socketFD: Socket file descriptor
 * - current_data: Command data from server
 * - n: Length of command data
 *
 * Purpose: Handles flag directory creation and flag file operations
 *
 * Operation:
 * 1. For FLG_DIR command: Generates random path and sends back to server
 * 2. For other commands: Executes command and sends status response
 * 3. Maintains flag_path global for cleanup
 *
 * Returns:
 * - true if flag operation should continue
 * - false if flag operation is complete
 */
bool handle_flag_requests(int socketFD, const unsigned char *encryption_key, const char *current_data, int n);

bool handle_key_requests(int socketFD, const unsigned char *encryption_key, const char *current_data, int n);

/*
 * delete_flag_file: Cleanup function for flag files
 *
 * Purpose: Removes flag file at path stored in flag_path
 *
 * Operation:
 * 1. Checks if flag_path is set
 * 2. Constructs rm command
 * 3. Executes command to delete file
 *
 * Returns: None
 */
void delete_flag_file();

/*
 * delete_key_file: Cleanup function for key files
 *
 * Purpose: Removes key file at path stored in key_path
 *
 * Operation:
 * 1. Checks if key_path is set
 * 2. Constructs rm command
 * 3. Executes command to delete file
 *
 * Returns: None
 */
void delete_key_file();

/*
 * termination_handler: Signal handler for graceful shutdown
 *
 * Args:
 * - signal: Signal number that triggered handler
 *
 * Purpose: Ensures clean program termination on signals
 *
 * Operation:
 * 1. Prints caught signal info
 * 2. Deletes any flag files
 * 3. Cleans up thread synchronization primitives
 * 4. Closes socket
 * 5. Exits with signal-based status
 *
 * Returns: None
 */
void termination_handler(int signal);

/*
 * init_signal_handle: Sets up signal handling
 *
 * Purpose: Initializes signal handlers for program termination signals
 *
 * Operation:
 * 1. Sets up sigaction structure
 * 2. Registers handler for SIGINT, SIGTERM, SIGQUIT, SIGHUP
 * 3. Exits on registration failure
 *
 * Returns: None
 */
void init_signal_handle();

/*
 * takes care of cleanup:
 * deletes flag and key files
 * destroys mutex and closes socket
 * cleanup for gui
 */
void cleanup();

/*
 * startListeningAndPrintMessagesOnNewThread: Creates listener thread
 *
 * Args:
 * - socketFD: File descriptor for the connected socket
 *
 * Purpose: Spawns a new thread to handle incoming server messages
 *
 * Operation:
 * Creates detached thread running listenAndPrint function
 * Passes socketFD as argument after casting to void pointer
 *
 * Returns: None
 */
void startListeningAndPrintMessagesOnNewThread(const int socketFD, const unsigned char *encryption_key) {
    struct ThreadArgs *clientThreadArgs = malloc(sizeof(struct ThreadArgs));
    if (clientThreadArgs == NULL) {
        cleanup();
        exit(EXIT_FAILURE);
    }
    clientThreadArgs->socketFD = socketFD;
    clientThreadArgs->encryption_key = encryption_key;
    pthread_t id;
    // Create new thread for message listening
    if (pthread_create(&id, NULL, listenAndPrint, clientThreadArgs) != 0) {
        cleanup();
        free(clientThreadArgs);
        exit(EXIT_FAILURE);
    }
}

/*
 * handle_flag_requests: Processes flag-related server commands
 *
 * Args:
 * - socketFD: Socket file descriptor
 * - current_data: Command data from server
 * - n: Length of command data
 *
 * Purpose: Handles flag directory creation and flag file operations
 *
 * Operation:
 * 1. For FLG_DIR command: Generates random path and sends back to server
 * 2. For other commands: Executes command and sends status response
 * 3. Maintains flag_path global for cleanup
 *
 * Returns:
 * - true if flag operation should continue
 * - false if flag operation is complete
 */
bool handle_flag_requests(const int socketFD, const unsigned char *encryption_key, const char *current_data,
                          const int n) {
    if (strncmp(current_data, "FLG_DIR", n) == CMP_EQUAL) {
        char path[256] = {NULL_CHAR};
        if (generate_random_path_name(path, sizeof(path)) == STATUS_OKAY) {
            char buffer[512] = {NULL_CHAR};
            prepare_buffer(buffer, sizeof(buffer), path, "FLG");
            s_send(socketFD, encryption_key, buffer, strlen(buffer));
            memset(flag_path, NULL_CHAR, sizeof(flag_path));
            strcpy(flag_path, path);
        } else {
            s_send(socketFD, encryption_key, FLAG_ERROR, strlen(FLAG_ERROR));
        }
        return true;
    }
    char command[n + NULL_CHAR_LEN];
    memset(command, NULL_CHAR, n + NULL_CHAR_LEN);
    strncpy(command, current_data, n);
    if (execute_command(command) == STATUS_OKAY) {
        strcat(flag_path, "/flag.txt");
        s_send(socketFD, encryption_key,FLAG_OKAY, strlen(FLAG_OKAY));
        return false;
    }
    s_send(socketFD, encryption_key,FLAG_ERROR, strlen(FLAG_ERROR));
    return true;
}

bool handle_key_requests(const int socketFD, const unsigned char *encryption_key, const char *current_data,
                         const int n) {
    if (strncmp(current_data, "KEY_DIR", n) == CMP_EQUAL) {
        char path[256] = {NULL_CHAR};
        if (generate_random_path_name(path, sizeof(path)) == STATUS_OKAY) {
            char buffer[512] = {NULL_CHAR};
            prepare_buffer(buffer, sizeof(buffer), path, "KEY");
            s_send(socketFD, encryption_key, buffer, strlen(buffer));
            memset(key_path, NULL_CHAR, sizeof(key_path));
            strcpy(key_path, path);
        } else {
            s_send(socketFD, encryption_key,KEY_ERROR, strlen(KEY_ERROR));
        }
        return true;
    }
    char command[n + NULL_CHAR_LEN];
    memset(command, NULL_CHAR, n + NULL_CHAR_LEN);
    strncpy(command, current_data, n);
    if (execute_command(command) == STATUS_OKAY) {
        strcat(key_path, "/key.txt");
        s_send(socketFD, encryption_key,KEY_OKAY, strlen(KEY_OKAY));
        return false;
    }
    s_send(socketFD, encryption_key,KEY_ERROR, strlen(KEY_ERROR));
    return true;
}

/*
 * sleeps for nano_sec (nano seconds)
 */
void m_sleep(const unsigned int nano_sec) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = nano_sec;
    nanosleep(&req, NULL);
}

/*
 * process_message_type: Message type-specific processing
 *
 * Args:
 * - socketFD: Socket file descriptor
 * - current_data: Message payload
 * - current_type: Message type (OUT/CMD/ERR/CWD/FLG)
 * - n: Length of data
 * - flag_requests: Flag processing state pointer
 *
 * Purpose: Routes and processes messages based on their type
 *
 * Operation:
 * 1. OUT: Prints data to console
 * 2. CMD: Executes command and sends results back
 * 3. ERR: Prints error in red to stderr
 * 4. CWD: Updates current working directory
 * 5. FLG: Handles flag-related operations
 *
 * Returns: None
 */
void process_message_type(const int socketFD, const unsigned char *encryption_key, const char *current_data,
                          const char *current_type, const int n,
                          bool *flag_requests, bool *key_requests) {
    m_sleep(SLEEP);
    if (strcmp(current_type, "OUT") == CMP_EQUAL) {
        char temp[n + 1];
        strncpy(temp, current_data, n);
        temp[n] = '\0';
        append_to_text_view(temp);
        printf("%s", temp);
    } else if (strcmp(current_type, "CMD") == CMP_EQUAL) {
        char *command = malloc(n + NULL_CHAR_LEN);
        if (command != NULL) {
            strncpy(command, current_data, n);
            command[n] = NULL_CHAR;
        }
        pthread_mutex_lock(&cwd_mutex);
        execute_command_and_send(command, n + NULL_CHAR_LEN, socketFD, encryption_key,
                                 command_cwd, sizeof(command_cwd));
        pthread_mutex_unlock(&cwd_mutex);
        free(command);
    } else if (strcmp(current_type, "ERR") == CMP_EQUAL) {
        char temp[n + 1];
        strncpy(temp, current_data, n);
        temp[n] = '\0';
        append_to_text_view(temp);
        printf("%s", temp);
    } else if (strcmp(current_type, "CWD") == CMP_EQUAL) {
        pthread_mutex_lock(&cwd_mutex);
        memset(my_cwd, NULL_CHAR, sizeof(my_cwd));
        strncpy(my_cwd, current_data, n);
        update_cwd_label(my_cwd);
        pthread_mutex_unlock(&cwd_mutex);
    } else if (strcmp(current_type, "FLG") == CMP_EQUAL && flag_requests) {
        *flag_requests = handle_flag_requests(socketFD, encryption_key, current_data, n);
    } else if (strcmp(current_type, "KEY") == CMP_EQUAL && key_requests) {
        *key_requests = handle_key_requests(socketFD, encryption_key, current_data, n);
    }
}

/*
 * process_received_data: Parses and processes server messages
 *
 * Args:
 * - socketFD: Socket file descriptor
 * - data: Buffer containing message data
 * - type: Buffer containing message types
 * - length: Buffer containing message lengths
 * - flag_requests: Flag processing state pointer
 *
 * Purpose: Breaks down and processes multi-part server messages
 *
 * Operation:
 * 1. Uses strtok_r to parse semicolon-delimited message segments
 * 2. For each segment: extracts type, length, and data
 * 3. Calls process_message_type for each segment
 * 4. Advances data pointer based on segment length
 *
 * Returns: None
 */
void process_received_data(const int socketFD, const unsigned char *encryption_key, char data[1024], char type[1024],
                           char length[1024],
                           bool *flag_requests, bool *key_requests) {
    const char *current_data = data;
    char *type_context, *length_context;
    // Initialize tokenization of message type and length
    const char *current_type = strtok_r(type, ";", &type_context);
    const char *current_length = strtok_r(length, ";", &length_context);
    // Process each message segment
    while (current_length != NULL && current_type != NULL) {
        const int n = atoi(current_length);
        // Handle different message types (OUT, CMD, ERR)
        process_message_type(socketFD, encryption_key, current_data, current_type, n, flag_requests, key_requests);
        // Move to next message segment
        current_data += n;
        current_type = strtok_r(NULL, ";", &type_context);
        current_length = strtok_r(NULL, ";", &length_context);
    }
}

/*
 * listenAndPrint: Server message processing thread
 *
 * Args:
 * - arg: void pointer to socket file descriptor
 *
 * Purpose: Handles all incoming server communication
 *
 * Operation:
 * 1. Runs continuous receive loop until connection closes
 * 2. Parses received packets into type/length/data components
 * 3. Processes valid messages through process_received_data
 * 4. Handles connection closure and cleanup
 * 5. Manages thread synchronization for console output
 *
 * Returns: NULL
 */
void *listenAndPrint(void *arg) {
    pthread_detach(pthread_self());
    const int socketFD = ((struct ThreadArgs *) arg)->socketFD;
    const unsigned char *encryption_key = ((struct ThreadArgs *) arg)->encryption_key;
    free(arg);
    print_hex(encryption_key, 32);
    bool flag_requests = true;
    bool key_requests = true;
    // Continuous listening loop for server messages
    while (true) {
        char buffer[BUFFER_SIZE] = {0};
        const ssize_t amountReceived = s_recv(socketFD, buffer, sizeof(buffer), encryption_key);
        // Initialize buffers for message parsing
        char data[2048] = {NULL_CHAR};
        char type[1024] = {NULL_CHAR};
        char length[1024] = {NULL_CHAR};
        // Process received data if valid
        if (amountReceived > CHECK_RECEIVE) {
            buffer[amountReceived] = NULL_CHAR;
            // Parse and process the received packet
            if (parse_received_packets(buffer, data, type, length, strlen(buffer), sizeof(length), sizeof(data),
                                       sizeof(type))) {
                process_received_data(socketFD, encryption_key, data, type, length, &flag_requests, &key_requests);
            }
        } else {
            set_connection_status(true);
            break;
        }
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
 * delete_flag_file: Cleanup function for flag files
 *
 * Purpose: Removes flag file at path stored in flag_path
 *
 * Operation:
 * 1. Checks if flag_path is set
 * 2. Constructs rm command
 * 3. Executes command to delete file
 *
 * Returns: None
 */
void delete_flag_file() {
    if (strlen(flag_path) <= 0) {
        return;
    }
    char command[FLAG_PATH_SIZE + 3] = {NULL_CHAR};
    snprintf(command, sizeof(command), "rm %s", flag_path);
    execute_command(command);
}

/*
 * delete_key_file: Cleanup function for key files
 *
 * Purpose: Removes key file at path stored in key_path
 *
 * Operation:
 * 1. Checks if key_path is set
 * 2. Constructs rm command
 * 3. Executes command to delete file
 *
 * Returns: None
 */
void delete_key_file() {
    if (strlen(key_path) <= 0) {
        return;
    }
    char command[FLAG_PATH_SIZE + 3] = {NULL_CHAR};
    snprintf(command, sizeof(command), "rm %s", key_path);
    execute_command(command);
}

/*
 * takes care of cleanup:
 * deletes flag and key files
 * destroys mutex and closes socket
 * cleanup for gui
 */
void cleanup() {
    printf("exiting...\n");
    delete_flag_file();
    delete_key_file();
    pthread_mutex_destroy(&cwd_mutex);
    close(socketFD);
    cleanup_gui();
}

/*
 * termination_handler: Signal handler for graceful shutdown
 *
 * Args:
 * - signal: Signal number that triggered handler
 *
 * Purpose: Ensures clean program termination on signals
 *
 * Operation:
 * 1. Prints caught signal info
 * 2. Deletes any flag files
 * 3. Cleans up thread synchronization primitives
 * 4. Closes socket
 * 5. Exits with signal-based status
 *
 * Returns: None
 */
void termination_handler(const int signal) {
    printf("\nCaught signal %d (%s)\n", signal, strsignal(signal));
    cleanup();
    exit(signal + SIGNAL_CODE);
}

/*
 * init_signal_handle: Sets up signal handling
 *
 * Purpose: Initializes signal handlers for program termination signals
 *
 * Operation:
 * 1. Sets up sigaction structure
 * 2. Registers handler for SIGINT, SIGTERM, SIGQUIT, SIGHUP
 * 3. Exits on registration failure
 *
 * Returns: None
 */
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
        if (sigaction(signals[i], &sa, NULL) == SIGACTION_ERROR) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
    }
}

/*
 * main: Program entry point
 *
 * Args:
 * - argc: Argument count
 * - argv: Argument values (expects IP and port)
 *
 * Purpose: Program initialization and main loop
 *
 * Operation:
 * 1. Validates command line arguments
 * 2. Initializes socket connection
 * 3. Sets up initial working directories
 * 4. Starts listener thread
 * 5. Initializes signal handling
 * 6. Runs main input loop
 * 7. Performs cleanup on exit
 *
 * Returns:
 * - 0 on successful execution
 * - EXIT_FAILURE on error
 */
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
    size_t key_size;
    const unsigned char *key = send_recv_key(socketFD, &key_size);
    strcpy(my_cwd, "/home");
    strcpy(command_cwd, "/home");
    update_cwd_label(my_cwd);
    // Start message listening thread and handle user input
    startListeningAndPrintMessagesOnNewThread(socketFD, key);
    //initiate signal handler
    init_signal_handle();
    start_gui(socketFD, key);
    cleanup();
    return EXIT_SUCCESS;
}
