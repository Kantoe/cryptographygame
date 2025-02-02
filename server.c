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
#define SLEEP 100000
#define BUFFER_SIZE 4096
#define RECEIVE_FLAG 0
#define CHECK_RECEIVE 0
#define NULL_CHAR 0
#define ACCEPTED_SUCCESSFULLY 0
#define LISTEN 1
#define TYPE_LENGTH 3
#define TYPE_OFFSET 5
#define DATA_CMD_CHECK "CMD"
#define DATA_OFFSET 5
#define WIN_MSG "tlength:45;type:OUT;length:10;data:\nyou won!\n"
#define LOSE_MSG "tlength:48;type:OUT;length:13;data:\nyou lost ):\n"
#define MAX_GAMES 10
#define MAX_CLIENTS_TOTAL 2 * MAX_GAMES
#define GAME_NOT_FOUND -1
#define FIRST_CLIENT_INDEX 0
#define SECOND_CLIENT_INDEX 1
#define PTHREAD_CREATE_SUCCESS 0
#define PIPE_READ 0
#define PIPE_WRITE 1
#define TIMEOUT_SECONDS 1
#define TIMEOUT_USECONDS 0
#define SELECT_ERROR_CHECK 0
#define MAX_FD_FOR_SELECT 1
#define PIPE_READ_BUF_SIZE 1
#define FLAG_DATA_SIZE 32
#define RANDOM_KEY_SIZE 9
#define FLAG_COMMAND_SIZE 1024
#define FLAG_COMMAND_BUFFER_SIZE 2048
#define MAX_FLAG_FILE_TRIES 5

//data types
struct AcceptedSocket {
    int acceptedSocketFD;
    struct sockaddr_in address;
    int error;
    int acceptedSuccessfully;
    char flag_data[FLAG_DATA_SIZE];
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
/**
 * Signal handler for graceful server shutdown
 * Args:
 *   signal: The signal number received (typically SIGINT)
 * Operation:
 *   Sets global stop flag and prints signal info
 * Returns: void
 */
void handle_signal(int signal);

/**
 * Accepts new client connections on server socket
 * Args:
 *   serverSocketFD: File descriptor for the server socket
 * Operation:
 *   Accepts connection and populates client address info
 * Returns:
 *   AcceptedSocket struct with client details and status
 */
struct AcceptedSocket acceptIncomingConnection(int serverSocketFD);

/**
 * Main client message handling thread function
 * Args:
 *   arg: Pointer to ThreadArgs containing socket FD and game info
 * Operation:
 *   - Handles client messaging in a loop
 *   - Processes commands and flags
 *   - Routes messages between clients
 * Returns: NULL on completion
 */
void *handle_single_client(void *arg);

/**
 * Main server accept loop for incoming connections
 * Args:
 *   serverSocketFD: File descriptor for the server socket
 * Operation:
 *   - Accepts new clients up to MAX_CLIENTS_TOTAL
 *   - Creates handler thread for each client
 *   - Manages game instances
 * Returns: void
 */
void startAcceptingIncomingConnections(int serverSocketFD);

/**
 * Creates and starts a new client handler thread
 * Args:
 *   clientSocketFD: Pointer to AcceptedSocket for the client
 * Operation:
 *   - Finds/creates game instance for client
 *   - Initializes thread arguments
 *   - Spawns handler thread
 * Returns: void
 */
void handle_single_client_on_separate_thread(
    const struct AcceptedSocket *clientSocketFD);


/**
 * Routes messages between connected clients in a game
 * Args:
 *   buffer: Message to send
 *   socketFD: Sender's socket FD (excluded from receiving)
 *   game: Pointer to Game struct for message routing
 * Operation:
 *   Thread-safe message broadcasting to other game clients
 * Returns: void
 */
void sendReceivedMessageToTheOtherClients(const char *buffer, int socketFD, Game *game);

/**
 * Initializes the server socket with specified configuration
 * Args:
 *   port: Port number to bind server to
 * Operation:
 *   - Creates TCP/IPv4 socket
 *   - Binds to address/port
 *   - Sets non-blocking mode
 *   - Starts listening
 * Returns:
 *   Valid server socket FD or EXIT_FAILURE
 */
int initServerSocket(int port);

/**
 * Validates incoming client messages
 * Args:
 *   buffer: Message buffer to check
 * Operation:
 *   - Checks message type and format
 *   - Validates command data if CMD type
 * Returns:
 *   Boolean indicating message validity
 */
int check_message_received(char buffer[4096]);

/**
 * Socket creation and address binding wrapper
 * Args:
 *   port: Port to bind to
 *   serverSocketFD: Pointer to store socket FD
 *   server_address: Pointer to store address info
 * Operation:
 *   Creates socket and prepares address structure
 * Returns:
 *   EXIT_SUCCESS or EXIT_FAILURE
 */
int config_socket(int port, int *serverSocketFD, struct sockaddr_in *server_address);

/**
 * Binds socket and starts listening
 * Args:
 *   serverSocketFD: Server socket file descriptor
 *   server_address: Server address structure
 * Operation:
 *   Binds socket and initiates listening state
 * Returns:
 *   EXIT_SUCCESS or EXIT_FAILURE
 */
int bind_and_listen_on_socket(int serverSocketFD, struct sockaddr_in server_address);

/**
 * Processes and routes client messages
 * Args:
 *   clientSocketFD: Client's socket FD
 *   buffer: Message buffer
 *   game: Game instance pointer
 * Operation:
 *   - Handles game state messages
 *   - Checks win conditions
 *   - Routes valid messages
 * Returns: Boolean indicating if game should end
 */
int generate_message_for_clients(int clientSocketFD, char buffer[4096], Game *game);

/**
 * Validates message field format
 * Args:
 *   buffer: Message to validate
 * Operation:
 *   Checks presence and format of required fields
 * Returns:
 *   Boolean indicating valid format
 */
int check_message_fields(const char *buffer);

/**
 * Creates encrypted flag file for client
 * Args:
 *   buffer: Directory path
 *   clientSocketFD: Client's socket FD
 *   game: Game instance pointer
 * Operation:
 *   - Generates random flag data
 *   - Creates and encrypts flag file
 * Returns: Status code
 */
int generate_client_flag(const char *buffer, int clientSocketFD, Game *game);

/**
 * Checks game winning condition
 * Args:
 *   clientSocketFD: Client's socket FD
 *   buffer: Message buffer
 *   game: Game instance pointer
 * Operation:
 *   Compares client flag data for win
 * Returns:
 *   Boolean indicating win
 */
bool check_winner(int clientSocketFD, char buffer[4096], Game *game);

/**
 * Processes client flag operations
 * Args:
 *   buffer: Message buffer
 *   flag_file_tries: Pointer to attempts counter
 *   clientSocketFD: Client socket FD
 *   flag_okay_response: Flag status pointer
 *   flag_request_dir: Directory request status pointer
 *   game: Game instance pointer
 * Operation:
 *   - Validates flag operations
 *   - Handles flag file creation
 *   - Tracks attempts
 * Returns: Operation status
 */
int handle_client_flag(const char *buffer, int *flag_file_tries, int clientSocketFD, int *flag_okay_response,
                       int *flag_request_dir, Game *game);

/**
 * Finds active game with space for client
 * Operation:
 *   Searches game array for active instance
 * Returns:
 *   Game index or -1 if none found
 */
int find_active_game();

/**
 * Finds slot for new game instance
 * Operation:
 *   Searches for NULL entry in games array
 * Returns:
 *   Available index or -1 if full
 */
int find_inactive_game();

/**
 * Adds a new client to an existing game instance
 * Args:
 *   clientSocketFD: Pointer to client's socket info
 *   active_game_index: Index of active game to join
 * Operation:
 *   - Thread-safe update of game client count
 *   - Stores client socket info in game structure
 * Returns: void
 */
void add_client_to_game(const struct AcceptedSocket *clientSocketFD, int active_game_index);

/**
 * Initializes new game instance
 * Args:
 *   clientSocketFD: First client's socket info
 *   inactive_game_index: Array index for new game
 * Operation:
 *   - Allocates game structure
 *   - Initializes mutex and pipe
 *   - Sets initial state
 * Returns:
 *   Boolean indicating success
 */
bool init_new_game(const struct AcceptedSocket *clientSocketFD, int inactive_game_index);

/**
 * Creates thread for client handling
 * Args:
 *   clientSocketFD: Client socket info
 *   active_game_index: Index of active game or -1
 *   inactive_game_index: Index for new game or -1
 * Operation:
 *   - Allocates thread arguments
 *   - Creates handler thread
 *   - Handles allocation failures
 * Returns: void
 */
void create_thread_args_and_thread(const struct AcceptedSocket *clientSocketFD, int active_game_index,
                                   int inactive_game_index);

/**
 * Handles client thread termination and cleanup
 * Args:
 *   clientSocketFD: Client's socket file descriptor
 *   game: Pointer to associated game instance
 * Operation:
 *   - Notifies other clients of disconnection
 *   - Updates game client count
 *   - Signals game termination via pipe
 *   - Closes socket and updates global client count
 * Returns: void
 */
void thread_exit(int clientSocketFD, Game *game);

/**
 * Processes incoming client messages and manages game state
 * Args:
 *   clientSocketFD: Client's socket file descriptor
 *   game: Pointer to associated game instance
 *   flag_file_tries: Pointer to flag attempt counter
 *   flag_request_dir: Pointer to directory request status
 *   flag_okay_response: Pointer to flag response status
 * Operation:
 *   - Receives client messages
 *   - Handles flag operations and validation
 *   - Processes game messages
 * Returns:
 *   Boolean indicating if client handling should terminate
 */
bool handle_client_messages(int clientSocketFD, Game *game, int *flag_file_tries, int *flag_request_dir,
                            int *flag_okay_response);

/**
 * Waits for all client threads to complete before server shutdown
 * Operation:
 *   - Monitors global client count
 *   - Uses short sleep intervals to prevent busy waiting
 *   - Thread-safe access to shared counter
 * Returns: void
 */
void wait_for_all_threads_to_finish();

/**
 * Cleans up resources for terminated games
 * Operation:
 *   - Checks each game slot for stopped games
 *   - Closes pipes and frees memory for finished games
 *   - Thread-safe cleanup using game mutex
 * Returns: void
 */
void handle_closed_games();

/**
 * Main server accept loop for incoming connections
 * Args:
 *   serverSocketFD: File descriptor for the server socket
 * Operation:
 *   - Accepts new clients up to MAX_CLIENTS_TOTAL
 *   - Creates handler thread for each client
 *   - Manages game instances
 * Returns: void
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

/**
 * Creates and starts a new client handler thread
 * Args:
 *   clientSocketFD: Pointer to AcceptedSocket for the client
 * Operation:
 *   - Finds/creates game instance for client
 *   - Initializes thread arguments
 *   - Spawns handler thread
 * Returns: void
 */
void handle_single_client_on_separate_thread(
    const struct AcceptedSocket *clientSocketFD) {
    const int active_game_index = find_active_game();
    int inactive_game_index = 0;
    if (active_game_index != GAME_NOT_FOUND) {
        add_client_to_game(clientSocketFD, active_game_index);
    } else {
        inactive_game_index = find_inactive_game();
        if (inactive_game_index != GAME_NOT_FOUND) {
            if (init_new_game(clientSocketFD, inactive_game_index)) {
                return;
            }
        }
    }
    create_thread_args_and_thread(clientSocketFD, active_game_index, inactive_game_index);
}

/**
 * Finds active game with space for client
 * Operation:
 *   Searches game array for active instance
 * Returns:
 *   Game index or -1 if none found
 */
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
    return GAME_NOT_FOUND;
}

/**
 * Finds slot for new game instance
 * Operation:
 *   Searches for NULL entry in games array
 * Returns:
 *   Available index or -1 if full
 */
int find_inactive_game() {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!games[i]) {
            return i;
        }
    }
    return GAME_NOT_FOUND;
}

/**
 * Initializes new game instance
 * Args:
 *   clientSocketFD: First client's socket info
 *   inactive_game_index: Array index for new game
 * Operation:
 *   - Allocates game structure
 *   - Initializes mutex and pipe
 *   - Sets initial state
 * Returns:
 *   Boolean indicating success
 */
bool init_new_game(const struct AcceptedSocket *clientSocketFD, const int inactive_game_index) {
    //create new game with malloc
    Game *game = malloc(sizeof(Game));
    if (game == NULL) {
        perror("malloc");
        return true;
    }
    game->acceptedSocketsCount++;
    game->game_clients[FIRST_CLIENT_INDEX] = *clientSocketFD;
    pthread_mutex_init(&game->game_mutex, NULL);
    game->stop_game = false;
    // Initialize the stop_pipe
    if (pipe(game->stop_pipe) != PIPE_SUCCESS) {
        perror("Failed to create pipe for Game");
        free(game);
        return true;
    }
    games[inactive_game_index] = game;
    return false;
}

/**
 * Creates thread for client handling
 * Args:
 *   clientSocketFD: Client socket info
 *   active_game_index: Index of active game or -1
 *   inactive_game_index: Index for new game or -1
 * Operation:
 *   - Allocates thread arguments
 *   - Creates handler thread
 *   - Handles allocation failures
 * Returns: void
 */
void create_thread_args_and_thread(const struct AcceptedSocket *clientSocketFD, const int active_game_index,
                                   const int inactive_game_index) {
    // Dynamically allocate memory for thread arguments
    struct ThreadArgs *clientThreadArgs = malloc(sizeof(struct ThreadArgs));
    if (!clientThreadArgs) {
        if (inactive_game_index != GAME_NOT_FOUND) {
            //if failed to allocate memory for args that have a new game then free game and set to NULL
            free(games[inactive_game_index]);
            games[inactive_game_index] = NULL;
        }
        perror("Failed to allocate memory for ThreadArgs");
        return;
    }
    // Create new thread and pass socket FD as argument
    clientThreadArgs->socketFD = clientSocketFD->acceptedSocketFD;
    clientThreadArgs->game = active_game_index != GAME_NOT_FOUND
                                 ? games[active_game_index]
                                 : inactive_game_index != GAME_NOT_FOUND
                                       ? games[inactive_game_index]
                                       : NULL;
    if (clientThreadArgs->game == NULL) {
        perror("No valid game found for the client");
        free(clientThreadArgs);
        return;
    }
    pthread_t clientThread;
    if (pthread_create(&clientThread, NULL, handle_single_client, clientThreadArgs) != PTHREAD_CREATE_SUCCESS) {
        if (inactive_game_index != GAME_NOT_FOUND) {
            //if failed to allocate memory for args that have a new game then free game and set to NULL
            free(games[inactive_game_index]);
            games[inactive_game_index] = NULL;
        }
        perror("Failed to create thread");
        free(clientThreadArgs); // Free allocated memory on failure
    }
}

/**
 * Adds a new client to an existing game instance
 * Args:
 *   clientSocketFD: Pointer to client's socket info
 *   active_game_index: Index of active game to join
 * Operation:
 *   - Thread-safe update of game client count
 *   - Stores client socket info in game structure
 * Returns: void
 */
void add_client_to_game(const struct AcceptedSocket *clientSocketFD, const int active_game_index) {
    pthread_mutex_lock(&games[active_game_index]->game_mutex);
    games[active_game_index]->acceptedSocketsCount++;
    games[active_game_index]->game_clients[SECOND_CLIENT_INDEX] = *clientSocketFD;
    pthread_mutex_unlock(&games[active_game_index]->game_mutex);
}

/**
 * Main client message handling thread function
 * Args:
 *   arg: Pointer to ThreadArgs containing socket FD and game info
 * Operation:
 *   - Handles client messaging in a loop
 *   - Processes commands and flags
 *   - Routes messages between clients
 * Returns: NULL on completion
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
    const int max_fd = clientSocketFD > game->stop_pipe[PIPE_READ] ? clientSocketFD : game->stop_pipe[PIPE_READ];
    s_send(clientSocketFD, DIR_REQUEST, strlen(DIR_REQUEST));
    while (!stop_all_games && !game->stop_game) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(clientSocketFD, &readfds);
        FD_SET(game->stop_pipe[PIPE_READ], &readfds);
        // Use select with a timeout
        struct timeval timeout = {TIMEOUT_SECONDS, TIMEOUT_USECONDS}; // 1-second timeout
        const int ret = select(max_fd + MAX_FD_FOR_SELECT, &readfds, NULL, NULL, &timeout);
        if (ret < SELECT_ERROR_CHECK) {
            perror("select");
            break;
        }
        // Check if the pipe was written to
        if (FD_ISSET(game->stop_pipe[PIPE_READ], &readfds)) {
            char buf[PIPE_READ_BUF_SIZE];
            read(game->stop_pipe[PIPE_READ], buf, sizeof(buf)); // Clear the pipe
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

/**
 * Handles client thread termination and cleanup
 * Args:
 *   clientSocketFD: Client's socket file descriptor
 *   game: Pointer to associated game instance
 * Operation:
 *   - Notifies other clients of disconnection
 *   - Updates game client count
 *   - Signals game termination via pipe
 *   - Closes socket and updates global client count
 * Returns: void
 */
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
    write(game->stop_pipe[PIPE_WRITE], &signal, sizeof(signal)); // Writing to stop the game
    game->stop_game = true;
    pthread_mutex_unlock(&game->game_mutex);
    close(clientSocketFD);
    printf("\033[1;31;47mThread %lu has successfully exited.\033[0m\n", pthread_self());
    pthread_mutex_lock(&globals_mutex);
    accepted_clients_count--;
    pthread_mutex_unlock(&globals_mutex);
}

/**
 * Processes incoming client messages and manages game state
 * Args:
 *   clientSocketFD: Client's socket file descriptor
 *   game: Pointer to associated game instance
 *   flag_file_tries: Pointer to flag attempt counter
 *   flag_request_dir: Pointer to directory request status
 *   flag_okay_response: Pointer to flag response status
 * Operation:
 *   - Receives client messages
 *   - Handles flag operations and validation
 *   - Processes game messages
 * Returns:
 *   Boolean indicating if client handling should terminate
 */
bool handle_client_messages(const int clientSocketFD, Game *game, int *flag_file_tries, int *flag_request_dir,
                            int *flag_okay_response) {
    // Initialize buffer for incoming message
    char buffer[BUFFER_SIZE] = {NULL_CHAR};
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

/**
 * Routes messages between connected clients in a game
 * Args:
 *   buffer: Message to send
 *   socketFD: Sender's socket FD (excluded from receiving)
 *   game: Pointer to Game struct for message routing
 * Operation:
 *   Thread-safe message broadcasting to other game clients
 * Returns: void
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

/**
 * Accepts new client connections on server socket
 * Args:
 *   serverSocketFD: File descriptor for the server socket
 * Operation:
 *   Accepts connection and populates client address info
 * Returns:
 *   AcceptedSocket struct with client details and status
 */
struct AcceptedSocket acceptIncomingConnection(const int serverSocketFD) {
    // Initialize socket structure
    struct AcceptedSocket acceptedSocket = {NULL_CHAR};
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

/**
 * Validates message field format
 * Args:
 *   buffer: Message to validate
 * Operation:
 *   Checks presence and format of required fields
 * Returns:
 *   Boolean indicating valid format
 */
int check_message_fields(const char *buffer) {
    if (strstr(buffer, "type:") == NULL) {
        return false;
    }
    if (strlen(strstr(buffer, "type:") + TYPE_OFFSET) < TYPE_LENGTH) {
        return false;
    }
    if (strstr(buffer, "data:") == NULL) {
        return false;
    }
    return true;
}

/**
 * Validates incoming client messages
 * Args:
 *   buffer: Message buffer to check
 * Operation:
 *   - Checks message type and format
 *   - Validates command data if CMD type
 * Returns:
 *   Boolean indicating message validity
 */
int check_message_received(char buffer[4096]) {
    if (!check_message_fields(buffer)) {
        return false;
    }
    if (strncmp(strstr(buffer, "type:") + TYPE_OFFSET, DATA_CMD_CHECK, TYPE_LENGTH) ==
        CMP_EQUAL) {
        // Validate command data
        return check_command_data(strstr(buffer, "data:") + DATA_OFFSET);
    }
    return strncmp(strstr(buffer, "type:") + TYPE_OFFSET, "FLG", TYPE_LENGTH) !=
           CMP_EQUAL;
}

/**
 * Checks game winning condition
 * Args:
 *   clientSocketFD: Client's socket FD
 *   buffer: Message buffer
 *   game: Game instance pointer
 * Operation:
 *   Compares client flag data for win
 * Returns:
 *   Boolean indicating win
 */
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

/**
 * Processes and routes client messages
 * Args:
 *   clientSocketFD: Client's socket FD
 *   buffer: Message buffer
 *   game: Game instance pointer
 * Operation:
 *   - Handles game state messages
 *   - Checks win conditions
 *   - Routes valid messages
 * Returns: Boolean indicating if game should end
 */
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

/**
 * Creates encrypted flag file for client
 * Args:
 *   buffer: Directory path
 *   clientSocketFD: Client's socket FD
 *   game: Game instance pointer
 * Operation:
 *   - Generates random flag data
 *   - Creates and encrypts flag file
 * Returns: Status code
 */
int generate_client_flag(const char *buffer, const int clientSocketFD, Game *game) {
    char flag_command[FLAG_COMMAND_SIZE] = {NULL_CHAR};
    char random_str[FLAG_DATA_SIZE] = {NULL_CHAR};
    generate_random_string(random_str, FLAG_DATA_SIZE - NULL_CHAR_LEN);
    if (snprintf(flag_command, sizeof(flag_command),
                 "echo '%s' > %s/flag.txt",
                 random_str, buffer) < sizeof(flag_command)) {
        char flag_command_buffer[FLAG_COMMAND_BUFFER_SIZE] = {NULL_CHAR};
        if (prepare_buffer(flag_command_buffer, sizeof(flag_command_buffer), flag_command, "FLG")) {
            s_send(clientSocketFD, flag_command_buffer, strlen(flag_command_buffer));
            for (int i = 0; i < game->acceptedSocketsCount; i++) {
                if (game->game_clients[i].acceptedSocketFD == clientSocketFD) {
                    strcpy(game->game_clients[i].flag_data, random_str);
                    return true;
                }
            }
        }
    }
    return false;
}

/**
 * Processes client flag operations
 * Args:
 *   buffer: Message buffer
 *   flag_file_tries: Pointer to attempts counter
 *   clientSocketFD: Client socket FD
 *   flag_okay_response: Flag status pointer
 *   flag_request_dir: Directory request status pointer
 *   game: Game instance pointer
 * Operation:
 *   - Validates flag operations
 *   - Handles flag file creation
 *   - Tracks attempts
 * Returns: Operation status
 */
int handle_client_flag(const char *buffer, int *flag_file_tries, const int clientSocketFD, int *flag_okay_response,
                       int *flag_request_dir, Game *game) {
    if (*flag_file_tries >= MAX_FLAG_FILE_TRIES) {
        return false;
    }
    if (!check_message_fields(buffer)) {
        return false;
    }
    if (strncmp(strstr(buffer, "type:") + TYPE_OFFSET, "FLG", 3) != CMP_EQUAL) {
        return true;
    }
    if (strcmp(strstr(buffer, "data:") + DATA_OFFSET, "error") == CMP_EQUAL) {
        *flag_okay_response = false;
        *flag_request_dir = false;
    } else {
        if (!contains_banned_word(strstr(buffer, "type:") + TYPE_OFFSET) && !*flag_request_dir) {
            *flag_request_dir = generate_client_flag(strstr(buffer, "data:") + DATA_OFFSET, clientSocketFD, game);
            return true;
        }
        if (*flag_request_dir) {
            if (strcmp(strstr(buffer, "data:") + DATA_OFFSET, "okay") == CMP_EQUAL) {
                *flag_okay_response = true;
                return true;
            }
        }
    }
    if (*flag_request_dir == false) {
        s_send(clientSocketFD, DIR_REQUEST, strlen(DIR_REQUEST));
        *flag_file_tries += 1;
    }
    return true;
}

/**
 * Signal handler for graceful server shutdown
 * Args:
 *   signal: The signal number received (typically SIGINT)
 * Operation:
 *   Sets global stop flag and prints signal info
 * Returns: void
 */
void handle_signal(const int signal) {
    // Print a message indicating the signal received
    printf("Caught signal %d\n", signal);
    // Set the `stop` flag to trigger cleanup
    stop_all_games = true;
}

/**
 * Cleans up resources for terminated games
 * Operation:
 *   - Checks each game slot for stopped games
 *   - Closes pipes and frees memory for finished games
 *   - Thread-safe cleanup using game mutex
 * Returns: void
 */
void handle_closed_games() {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (games[i]) {
            pthread_mutex_lock(&games[i]->game_mutex);
            if (games[i]->stop_game && games[i]->acceptedSocketsCount == 0) {
                close(games[i]->stop_pipe[PIPE_READ]);
                close(games[i]->stop_pipe[PIPE_WRITE]);
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

/**
 * Waits for all client threads to complete before server shutdown
 * Operation:
 *   - Monitors global client count
 *   - Uses short sleep intervals to prevent busy waiting
 *   - Thread-safe access to shared counter
 * Returns: void
 */
void wait_for_all_threads_to_finish() {
    unsigned int local_count = 0;
    while (true) {
        pthread_mutex_lock(&globals_mutex);
        local_count = accepted_clients_count; // Copy the value under lock
        pthread_mutex_unlock(&globals_mutex);
        if (local_count == 0) {
            break; // Exit the loop when no threads are active
        }
        usleep(SLEEP); // Sleep for 1ms to avoid busy-waiting
    }
}

/**
 * Socket creation and address binding wrapper
 * Args:
 *   port: Port to bind to
 *   serverSocketFD: Pointer to store socket FD
 *   server_address: Pointer to store address info
 * Operation:
 *   Creates socket and prepares address structure
 * Returns:
 *   EXIT_SUCCESS or EXIT_FAILURE
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

/**
 * Binds socket and starts listening
 * Args:
 *   serverSocketFD: Server socket file descriptor
 *   server_address: Server address structure
 * Operation:
 *   Binds socket and initiates listening state
 * Returns:
 *   EXIT_SUCCESS or EXIT_FAILURE
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

/**
 * Initializes the server socket with specified configuration
 * Args:
 *   port: Port number to bind server to
 * Operation:
 *   - Creates TCP/IPv4 socket
 *   - Binds to address/port
 *   - Sets non-blocking mode
 *   - Starts listening
 * Returns:
 *   Valid server socket FD or EXIT_FAILURE
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
    wait_for_all_threads_to_finish();
    // Cleanup resources
    handle_closed_games();
    shutdown(serverSocketFD, SHUT_RDWR);
    close(serverSocketFD);
    return EXIT_SUCCESS;
}
