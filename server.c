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
#define GAME_MAX "tlength:54;type:ERR;length:19;data:game limit reached\n"
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
#define MAX_GAMES 10
#define MAX_CLIENTS_TOTAL 2 * MAX_GAMES

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
    int stop_pipe[2]; // Pipe for stopping the game
} Game;

struct ThreadArgs {
    Game *game;
    int socketFD;
};

//globals
Game *games[MAX_GAMES] = {NULL};
volatile sig_atomic_t stop_all_games = 0;
unsigned int accepted_clients_count = 0;
pthread_mutex_t globals_mutex = PTHREAD_MUTEX_INITIALIZER;

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
void *handle_single_client(void *arg);

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
void handle_single_client_on_separate_thread(
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
void sendReceivedMessageToTheOtherClients(const char *buffer, int socketFD, Game *game);

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

int generate_message_for_clients(int clientSocketFD, char buffer[4096], Game *game);

int check_message_fields(const char *buffer);

int generate_client_flag(const char *buffer, int clientSocketFD, Game *game);

bool check_winner(int clientSocketFD, char buffer[4096], Game *game);

int handle_client_flag(const char *buffer, int *flag_file_tries, int clientSocketFD, int *flag_okay_response,
                       int *flag_request_dir, Game *game);

int find_active_game();

int find_inactive_game();

void add_client_to_game(const struct AcceptedSocket *clientSocketFD, int active_game_index);

bool init_new_game(const struct AcceptedSocket *clientSocketFD, int inactive_game_index);

void create_thread_args_and_thread(const struct AcceptedSocket *clientSocketFD, int active_game_index,
                                   int inactive_game_index);

void thread_exit(int clientSocketFD, Game *game);

bool handle_client_messages(int clientSocketFD, Game *game, int *flag_file_tries, int *flag_request_dir,
                            int *flag_okay_response);

void wait_for_all_threads_to_finish();

void handle_closed_games();

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
        if (accepted_clients_count < MAX_CLIENTS_TOTAL) {
            // Unlock mutex before accepting new connection
            pthread_mutex_unlock(&globals_mutex);
            struct AcceptedSocket clientSocket =
                    acceptIncomingConnection(serverSocketFD);

            // If connection accepted successfully, add to active clients
            if (clientSocket.acceptedSuccessfully) {
                // Lock mutex before updating shared data
                handle_single_client_on_separate_thread(&clientSocket);
            }
        } else {
            // Server at capacity, reject new connection
            pthread_mutex_unlock(&globals_mutex);
            const int clientSocketFD = accept(serverSocketFD, NULL, NULL);
            // Send max clients error message
            s_send(clientSocketFD, GAME_MAX,
                   strlen(GAME_MAX));
            close(clientSocketFD);
        }
        handle_closed_games();
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
void handle_single_client_on_separate_thread(
    const struct AcceptedSocket *clientSocketFD) {
    const int active_game_index = find_active_game();
    int inactive_game_index = 0;
    if (active_game_index != -1) {
        add_client_to_game(clientSocketFD, active_game_index);
    } else {
        inactive_game_index = find_inactive_game();
        if (inactive_game_index != -1) {
            if (init_new_game(clientSocketFD, inactive_game_index)) {
                return;
            }
        }
    }
    create_thread_args_and_thread(clientSocketFD, active_game_index, inactive_game_index);
}

int find_active_game() {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (games[i]) {
            pthread_mutex_lock(&games[i]->game_mutex);
            if (!games[i]->stop_game && games[i]->acceptedSocketsCount == 1) {
                pthread_mutex_unlock(&games[i]->game_mutex);
                return i;
            }
            pthread_mutex_unlock(&games[i]->game_mutex);
        }
    }
    return -1;
}

int find_inactive_game() {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!games[i]) {
            return i;
        }
    }
    return -1;
}

bool init_new_game(const struct AcceptedSocket *clientSocketFD, const int inactive_game_index) {
    //create new game with malloc
    Game *game = malloc(sizeof(Game));
    if (game == NULL) {
        perror("malloc");
        return true;
    }
    game->acceptedSocketsCount = 1;
    game->game_clients[0] = *clientSocketFD;
    pthread_mutex_init(&game->game_mutex, NULL);
    game->stop_game = false;
    // Initialize the stop_pipe
    if (pipe(game->stop_pipe) == -1) {
        perror("Failed to create pipe for Game");
        free(game);
        return true;
    }
    games[inactive_game_index] = game;
    return false;
}

void create_thread_args_and_thread(const struct AcceptedSocket *clientSocketFD, const int active_game_index,
                                   const int inactive_game_index) {
    // Dynamically allocate memory for thread arguments
    struct ThreadArgs *clientThreadArgs = malloc(sizeof(struct ThreadArgs));
    if (!clientThreadArgs) {
        if (inactive_game_index != -1) {
            //if failed to allocate memory for args that have a new game then free game and set to NULL
            free(games[inactive_game_index]);
            games[inactive_game_index] = NULL;
        }
        perror("Failed to allocate memory for ThreadArgs");
        return;
    }
    // Create new thread and pass socket FD as argument
    clientThreadArgs->socketFD = clientSocketFD->acceptedSocketFD;
    clientThreadArgs->game = active_game_index != -1
                                 ? games[active_game_index]
                                 : inactive_game_index != -1
                                       ? games[inactive_game_index]
                                       : NULL;
    if (clientThreadArgs->game == NULL) {
        perror("No valid game found for the client");
        free(clientThreadArgs);
        return;
    }
    pthread_t clientThread;
    if (pthread_create(&clientThread, NULL, handle_single_client, clientThreadArgs) != 0) {
        if (inactive_game_index != -1) {
            //if failed to allocate memory for args that have a new game then free game and set to NULL
            free(games[inactive_game_index]);
            games[inactive_game_index] = NULL;
        }
        perror("Failed to create thread");
        free(clientThreadArgs); // Free allocated memory on failure
    }
}

void add_client_to_game(const struct AcceptedSocket *clientSocketFD, const int active_game_index) {
    pthread_mutex_lock(&games[active_game_index]->game_mutex);
    games[active_game_index]->acceptedSocketsCount = 2;
    games[active_game_index]->game_clients[1] = *clientSocketFD;
    pthread_mutex_unlock(&games[active_game_index]->game_mutex);
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
void *handle_single_client(void *arg) {
    pthread_detach(pthread_self());
    pthread_mutex_lock(&globals_mutex);
    accepted_clients_count++;
    pthread_mutex_unlock(&globals_mutex);
    const int clientSocketFD = ((struct ThreadArgs *) arg)->socketFD;
    Game *game = ((struct ThreadArgs *) arg)->game;
    free(arg);
    int flag_file_tries = 0;
    int flag_request_dir = 0;
    int flag_okay_response = 0;
    const int max_fd = clientSocketFD > game->stop_pipe[0] ? clientSocketFD : game->stop_pipe[0];
    s_send(clientSocketFD, DIR_REQUEST, strlen(DIR_REQUEST));
    while (!stop_all_games && !game->stop_game) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(clientSocketFD, &readfds);
        FD_SET(game->stop_pipe[0], &readfds);
        // Use select with a timeout
        struct timeval timeout = {1, 0}; // 1-second timeout
        const int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
        if (ret < 0) {
            perror("select");
            break;
        }
        // Check if the pipe was written to
        if (FD_ISSET(game->stop_pipe[0], &readfds)) {
            char buf[1];
            read(game->stop_pipe[0], buf, sizeof(buf)); // Clear the pipe
            break;
        }
        if (FD_ISSET(clientSocketFD, &readfds)) {
            if (handle_client_messages(clientSocketFD, game, &flag_file_tries, &flag_request_dir, &flag_okay_response))
                break;
        }
    }
    thread_exit(clientSocketFD, game);
    return NULL;
}

void thread_exit(const int clientSocketFD, Game *game) {
    //send disconnect message and remove accepted socket from array
    if (!stop_all_games) {
        sendReceivedMessageToTheOtherClients(SECOND_CLIENT_DISCONNECTED, clientSocketFD, game);
    }
    pthread_mutex_lock(&game->game_mutex);
    if (game->acceptedSocketsCount > 0) {
        game->acceptedSocketsCount--;
    }
    // Write to the pipe to signal that the game should stop
    const char signal = 'N';
    write(game->stop_pipe[1], &signal, sizeof(signal)); // Writing to stop the game
    game->stop_game = true;
    pthread_mutex_unlock(&game->game_mutex);
    close(clientSocketFD);
    printf("\033[1;31;47mThread %lu has successfully exited.\033[0m\n", pthread_self());
    pthread_mutex_lock(&globals_mutex);
    accepted_clients_count--;
    pthread_mutex_unlock(&globals_mutex);
}

bool handle_client_messages(const int clientSocketFD, Game *game, int *flag_file_tries, int *flag_request_dir,
                            int *flag_okay_response) {
    // Initialize buffer for incoming message
    char buffer[BUFFER_SIZE] = {0};
    // Receive data from client
    const ssize_t amountReceived = s_recv(clientSocketFD, buffer, sizeof(buffer));
    if (amountReceived > CHECK_RECEIVE) {
        // Null terminate received message
        buffer[amountReceived] = NULL_CHAR;
        // Log received message
        printf("%s\n", buffer);
        if (!(*flag_okay_response && *flag_request_dir)) {
            if (!handle_client_flag(buffer, flag_file_tries, clientSocketFD, flag_okay_response,
                                    flag_request_dir, game)) {
                return true;
            }
        } else {
            //deal with client message and make an ideal response
            game->stop_game = generate_message_for_clients(clientSocketFD, buffer, game);
        }
    }
    // Exit if connection closed or server stopping
    if (amountReceived <= CHECK_RECEIVE || stop_all_games || game->stop_game) {
        return true;
    }
    return false;
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
void sendReceivedMessageToTheOtherClients(const char *buffer, const int socketFD, Game *game) {
    // Lock mutex before accessing shared client data
    pthread_mutex_lock(&game->game_mutex);

    // Iterate through all connected clients
    for (int i = 0; i < game->acceptedSocketsCount; i++) {
        // Skip sender's socket
        if (game->game_clients[i].acceptedSocketFD != socketFD) {
            // Forward message to other client
            s_send(game->game_clients[i].acceptedSocketFD, buffer, strlen(buffer));
        }
    }

    // Release mutex after broadcasting
    pthread_mutex_unlock(&game->game_mutex);
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

bool check_winner(const int clientSocketFD, char buffer[4096], Game *game) {
    pthread_mutex_lock(&game->game_mutex);
    for (int i = 0; i < game->acceptedSocketsCount; i++) {
        if (game->game_clients[i].acceptedSocketFD != clientSocketFD) {
            if (strcmp(strstr(buffer, "data:") + DATA_OFFSET, game->game_clients[i].flag_data) == CMP_EQUAL) {
                pthread_mutex_unlock(&game->game_mutex);
                return true;
            }
        }
    }
    pthread_mutex_unlock(&game->game_mutex);
    return false;
}

int generate_message_for_clients(const int clientSocketFD, char buffer[4096], Game *game) {
    pthread_mutex_lock(&game->game_mutex);
    if (game->acceptedSocketsCount < MAX_CLIENTS) {
        pthread_mutex_unlock(&game->game_mutex);
        //are there not 2 clients connected?
        s_send(clientSocketFD, WAIT_CLIENT, strlen(WAIT_CLIENT));
    } else {
        pthread_mutex_unlock(&game->game_mutex);
        if (check_winner(clientSocketFD, buffer, game)) {
            s_send(clientSocketFD, WIN_MSG, strlen(WIN_MSG));
            sendReceivedMessageToTheOtherClients(LOSE_MSG, clientSocketFD, game);
            return true;
        }
        if (check_message_received(buffer)) {
            sendReceivedMessageToTheOtherClients(buffer, clientSocketFD, game);
        } else {
            s_send(clientSocketFD, INVALID_DATA,
                   strlen(INVALID_DATA));
        }
    }
    return false;
}

int generate_client_flag(const char *buffer, const int clientSocketFD, Game *game) {
    char flag_command[256] = {0};
    char random_str[32] = {0};
    generate_random_string(random_str, 31);
    if (snprintf(flag_command, sizeof(flag_command), "echo '%s' > %s/flag.txt", random_str, buffer) < sizeof(
            flag_command)) {
        char flag_command_buffer[512] = {0};
        if (prepare_buffer(flag_command_buffer, sizeof(flag_command_buffer), flag_command, "FLG")) {
            s_send(clientSocketFD, flag_command_buffer, strlen(flag_command_buffer));
            for (int i = 0; i < game->acceptedSocketsCount; i++) {
                if (game->game_clients[i].acceptedSocketFD == clientSocketFD) {
                    strcpy(game->game_clients[i].flag_data, random_str);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int handle_client_flag(const char *buffer, int *flag_file_tries, const int clientSocketFD, int *flag_okay_response,
                       int *flag_request_dir, Game *game) {
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
            *flag_request_dir = generate_client_flag(strstr(buffer, "data:") + DATA_OFFSET, clientSocketFD, game);
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
 * Handles signal, specifically SIGINT (press ctrl+c)
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

void handle_closed_games() {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (games[i]) {
            pthread_mutex_lock(&games[i]->game_mutex);
            if (games[i]->stop_game && games[i]->acceptedSocketsCount == 0) {
                close(games[i]->stop_pipe[0]);
                close(games[i]->stop_pipe[1]);
                pthread_mutex_unlock(&games[i]->game_mutex);
                pthread_mutex_destroy(&games[i]->game_mutex);
                free(games[i]);
                games[i] = NULL;
                printf("\033[1;30;42mGame %d resources have been released.\033[0m\n", i);
            } else {
                pthread_mutex_unlock(&games[i]->game_mutex);
            }
        }
    }
}

void wait_for_all_threads_to_finish() {
    unsigned int local_count = 0;
    while (true) {
        pthread_mutex_lock(&globals_mutex);
        local_count = accepted_clients_count; // Copy the value under lock
        pthread_mutex_unlock(&globals_mutex);
        if (local_count == 0) {
            break; // Exit the loop when no threads are active
        }
        usleep(1000); // Sleep for 1ms to avoid busy-waiting
    }
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
    wait_for_all_threads_to_finish(); // Cleanup resources
    handle_closed_games();
    shutdown(serverSocketFD, SHUT_RDWR);
    close(serverSocketFD);
    return 0;
}
