#include "gui_fltk.h"
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/fl_ask.H>

extern "C" {
#include "cryptography_game_util.h"
}

#define SLEEP 3000
#define SECONDS 0
#define MAX_COMMAND_LENGTH 250
#define COMMAND_BUFFER_SIZE 512
#define COMMAND_MESSAGE_SIZE 1024
#define MAX_OPENSSL_LENGTH 400
#define FIRST_ENC_LIST_INDEX 0
#define MESSAGE_WINDOW_ARGS 300, 100, "Message"
#define MESSAGE_WINDOW_BOX_ARGS 10, 10, 280, 40
#define MESSAGE_WINDOW_BUTTON_ARGS 110, 60, 80, 30, "OK"
#define WIN_WIDTH_ALIGNMENT
#define TEXT_DISPLAY_DIMS(margin, win_w, h) margin, margin, win_w - 2 * margin, h
#define TEXT_SIZE 14
#define CWD_LABEL_DIMS(margin, win_w, y)  margin, y, win_w - 2 * margin, 20
#define CMD_LABEL_DIMS(margin, y, width)  margin, y, width, 20
#define CMD_INPUT_DIMS(margin, y, width)  margin, y + 25, width, 30
#define ENC_LABEL_DIMS(x, y, width)  x, y - 25, width, 20
#define ENC_CHOICE_DIMS(x, y, width)  x, y, width, 30
#define KEY_LABEL_DIMS(x, y, width)  x + width + 10, y - 25, width, 20
#define KEY_INPUT_DIMS(x, y, width)  x + width + 10, y, width, 30
#define PATH_LABEL_DIMS(x, y, width)  x + 2 * (width + 10), y - 25, width, 20
#define PATH_INPUT_DIMS(x, y, width)  x + 2 * (width + 10), y, width, 30
#define ENCRYPTION_SECTION_MARGIN_MULTIPLIER 3
#define ENCRYPTION_SECTION_WIDTH_PCT 35
#define ENCRYPTION_CONTROL_GAP 20
#define ENCRYPTION_CONTROL_COUNT 3
#define ENCRYPTION_BTN_WIDTH 100
#define ENCRYPTION_BTN_HEIGHT 30
#define ENCRYPTION_BTN_Y_OFFSET 40
#define ENCRYPTION_PCT_DIVISOR 100
#define WINDOW_SIZE_PCT 80
#define MARGIN_SIZE 20
#define COMMAND_SECTION_WIDTH_PCT 60
#define TEXT_DISPLAY_HEIGHT_MULTIPLIER 4
#define TEXT_DISPLAY_MARGIN_MULTIPLIER 5
#define TEXT_DISPLAY_OFFSET 20
#define COMMAND_MARGIN_MULTIPLIER 3
#define Y_POS_MARGIN_MULTIPLIER 2
#define Y_POS_INCREMENT_A 20
#define Y_POS_INCREMENT_B 10
#define ELEMENT_HEIGHT 30
#define UI_REFRESH_INTERVAL 0.1


/**
 * Modal window for displaying messages to the user
 * Components:
 *   win: Main window container
 *   box: Text display box
 *   btn: OK button for dismissal
 * Operation:
 *   - Created dynamically when message needs to be shown
 *   - Displays modal until user dismisses
 *   - Self-deletes after closing
 */
struct MessageWindow {
    Fl_Window *win;
    Fl_Box *box;
    Fl_Button *btn;

    explicit MessageWindow(const char *message) {
        win = new Fl_Window(MESSAGE_WINDOW_ARGS);
        box = new Fl_Box(MESSAGE_WINDOW_BOX_ARGS, message);
        box->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        btn = new Fl_Button(MESSAGE_WINDOW_BUTTON_ARGS);
        btn->callback(close_cb, this);
        win->end();
        win->set_modal();
        win->show();
    }

    ~MessageWindow() {
        delete btn; // Free button first
        delete box; // Free text box
        delete win; // Finally, free window
    }

    static void close_cb(Fl_Widget *, void *data) {
        auto *msgWin = static_cast<MessageWindow *>(data);
        msgWin->win->hide();
        Fl::repeat_timeout(SECONDS, delete_cb, msgWin); // Safe delayed delete
    }

    static void delete_cb(void *data) {
        delete static_cast<MessageWindow *>(data); // Delete safely
    }
};

/**
 * Shows a modal message window
 * Args:
 *   message: Text string to display in the window
 * Operation:
 *   - Creates MessageWindow with provided text
 *   - Shows window modal
 *   - Window self-deletes when closed
 * Returns: void
 */
void display_message(const char *message) {
    new MessageWindow(message);
}

/**
 * Main application GUI container and state
 * Components:
 *   window: Main application window
 *   text_display: Output text area
 *   text_buffer: Buffer for text display
 *   command_input: Command entry field
 *   key_input: Encryption key input
 *   file_path_input: File path input
 *   encryption_choice: Encryption method selector
 *   cwd_label: Current working directory display
 *   submit_button: Decrypt action button
 *   socket_fd: Server communication socket
 *   cmd_label: Command input label
 *   enc_label: Encryption selector label
 *   key_label: Key input label
 *   path_label: File path label
 *   connection_closed: Connection status flag
 * Operation:
 *   - Initialized at program start
 *   - Maintains all GUI state
 *   - Accessed through global pointer
 *   - Cleaned up on program exit
 */
typedef struct {
    Fl_Window *window;
    Fl_Text_Display *text_display;
    Fl_Text_Buffer *text_buffer;
    Fl_Input *command_input;
    Fl_Input *key_input;
    Fl_Input *file_path_input;
    Fl_Choice *encryption_choice;
    Fl_Box *cwd_label;
    Fl_Button *submit_button;
    int socket_fd;
    const unsigned char *encryption_key;
    Fl_Box *cmd_label;
    Fl_Box *enc_label;
    Fl_Box *key_label;
    Fl_Box *path_label;
    bool connection_closed;
} GuiComponents;

static GuiComponents *gui = nullptr;

static const char *encryption_methods[] = {
    "None",
    "aes-256-cbc",
    "aes-128-cbc",
    "des-ede3",
    nullptr
};

/**
 * @brief Periodic callback function for updating the GUI.
 *
 * This function is triggered at regular intervals to refresh the GUI window.
 * It ensures that the window is redrawn to reflect any changes.
 *
 * @note The void * parameter is unused but required by the FLTK callback signature.
 */
static void periodic_update_cb(void *) {
    if (gui && gui->window) {
        gui->window->redraw();
    }
    Fl::repeat_timeout(UI_REFRESH_INTERVAL, periodic_update_cb);
}

/**
 * Updates connection state and notifies user
 * Args:
 *   is_closed: Boolean indicating if connection is closed
 * Operation:
 *   - Sets global connection status
 *   - Displays notification if connection closed
 *   - Updates UI state
 * Returns: void
 */
void set_connection_status(const bool is_closed) {
    if (gui) {
        gui->connection_closed = is_closed;
        if (is_closed) {
            display_message("Connection closed, close window");
        }
    }
}

/**
 * Updates working directory display
 * Args:
 *   new_cwd: String containing new working directory path
 * Operation:
 *   - Updates CWD label if GUI exists
 *   - Triggers window redraw
 * Returns: void
 */
void update_cwd_label(const char *new_cwd) {
    if (gui && gui->cwd_label) {
        gui->cwd_label->copy_label(new_cwd);
    }
}

/**
 * Adds text to main display area
 * Args:
 *   message: Text string to append
 * Operation:
 *   - Appends to text buffer if GUI exists
 *   - Triggers text display redraw
 * Returns: void
 */
void append_to_text_view(const char *message) {
    if (gui && gui->text_buffer) {
        gui->text_buffer->append(message);
    }
}

/**
 * Window deletion callback
 * Args:
 *   data: Pointer to window to delete
 * Operation:
 *   - Casts data to Fl_Window
 *   - Deletes after FLTK processes events
 * Returns: void
 */
void delete_win_cb(void *data) {
    delete static_cast<Fl_Window *>(data); // Safe delete after FLTK processes events
}

/**
 * Window close handler
 * Args:
 *   widget: Triggering widget (unused)
 *   data: Window to close
 * Operation:
 *   - Hides window
 *   - Schedules deletion
 * Returns: void
 */
void close_cb(Fl_Widget *, void *data) {
    auto *win = static_cast<Fl_Window *>(data);
    win->hide();
    Fl::add_timeout(SECONDS, delete_win_cb, win); // Schedule safe deletion
}

/**
 * Processes standard commands
 * Args:
 *   command: Command string to execute
 * Operation:
 *   - Validates command and connection
 *   - Prepares and sends to server
 *   - Updates display
 * Returns: void
 */
static void handle_regular_command(const char *command) {
    if (!gui) return;
    if (gui->connection_closed) {
        display_message("Connection closed, close window");
        return;
    }
    if (strlen(command) == 0 || strlen(command) > MAX_COMMAND_LENGTH) {
        display_message("Unsupported command");
        return;
    }
    char buffer[COMMAND_BUFFER_SIZE];
    prepare_buffer(buffer, sizeof(buffer), command, "CMD");
    s_send(gui->socket_fd, gui->encryption_key, buffer, strlen(buffer));
    char message[COMMAND_MESSAGE_SIZE];
    snprintf(message, sizeof(message), ":$> %s\n", command);
    append_to_text_view(message);
}

/**
 * Processes file decryption operations
 * Operation:
 *   - Validates encryption parameters
 *   - Constructs OpenSSL command
 *   - Sends to server
 *   - Resets input fields
 * Returns: void
 */
static void handle_encryption_command() {
    if (!gui) return;
    if (gui->connection_closed) {
        display_message("Connection closed, close window");
        return;
    }
    const char *encryption_method = gui->encryption_choice->text();
    const char *key = gui->key_input->value();
    const char *path = gui->file_path_input->value();
    if (strcmp(encryption_method, "None") == CMP_EQUAL || strlen(key) == 0 || strlen(path) == 0 ||
        strlen(key) + strlen(path) > MAX_OPENSSL_LENGTH) {
        display_message("Unsupported command");
        return;
    }
    char command[COMMAND_BUFFER_SIZE];
    snprintf(command, sizeof(command), "openssl enc -d -%s -in %s -out %s.dec -k %s -pbkdf2 && mv %s.dec %s",
             encryption_method, path, path, key, path, path);
    char buffer[COMMAND_MESSAGE_SIZE];
    prepare_buffer(buffer, sizeof(buffer), command, "CMD");
    s_send(gui->socket_fd, gui->encryption_key, buffer, strlen(buffer));
}

/**
 * Handles command input events
 * Args:
 *   w: Triggering widget
 *   data: User data (unused)
 * Operation:
 *   - Gets command from input
 *   - Processes command
 *   - Clears input field
 * Returns: void
 */
static void command_input_callback(Fl_Widget *w, void *) {
    auto *input = dynamic_cast<Fl_Input *>(w);
    const char *command = input->value();
    handle_regular_command(command);
    input->value(""); // Clear the input after sending
}

/**
 * Handles decrypt button clicks
 * Args:
 *   widget: Triggering widget (unused)
 *   data: User data (unused)
 * Operation:
 *   - Triggers decryption
 *   - Resets input fields
 * Returns: void
 */
static void submit_callback(Fl_Widget *, void *) {
    handle_encryption_command();
    gui->encryption_choice->value(FIRST_ENC_LIST_INDEX);
    gui->key_input->value("");
    gui->file_path_input->value("");
}

/**
 * Frees GUI resources
 * Operation:
 *   - Deletes widgets in reverse order
 *   - Frees GUI structure
 *   - Nulls global pointer
 * Returns: void
 */
void cleanup_gui() {
    if (!gui) return;
    // Delete all widgets in reverse order of creation
    delete gui->submit_button;
    delete gui->file_path_input;
    delete gui->key_input;
    delete gui->encryption_choice;
    delete gui->command_input;
    // Delete all labels
    delete gui->path_label;
    delete gui->key_label;
    delete gui->enc_label;
    delete gui->cmd_label;
    delete gui->cwd_label;
    // Delete text display components
    delete gui->text_display;
    delete gui->text_buffer;
    // Delete the window
    delete gui->window;
    // Delete the structure itself
    delete gui;
    gui = nullptr;
}

/**
 * Initializes text display area
 * Args:
 *   gui: Pointer to GUI components
 *   margin: Window margin size
 *   win_w: Window width
 *   text_display_h: Height of text display area
 */
void initialize_text_display(GuiComponents *gui, const int margin, const int win_w, const int text_display_h) {
    gui->text_display = new Fl_Text_Display(TEXT_DISPLAY_DIMS(margin, win_w, text_display_h));
    gui->text_buffer = new Fl_Text_Buffer();
    gui->text_display->buffer(gui->text_buffer);
    gui->text_display->textfont(FL_COURIER);
    gui->text_display->textsize(TEXT_SIZE);
}

/**
 * Initializes working directory section
 * Args:
 *   gui: Pointer to GUI components
 *   margin: Window margin size
 *   win_w: Window width
 *   y_pos: Vertical position for section
 */
void initialize_cwd_section(GuiComponents *gui, const int margin, const int win_w, const int y_pos) {
    gui->cwd_label = new Fl_Box(CWD_LABEL_DIMS(margin, win_w, y_pos), "/home");
    gui->cwd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
}

/**
 * Initializes command input section
 * Args:
 *   gui: Pointer to GUI components
 *   margin: Window margin size
 *   win_w: Window width
 *   y_pos: Vertical position for section
 *   command_width: Width of command section
 */
void initialize_command_section(GuiComponents *gui, const int margin, const int y_pos, const int command_width) {
    gui->cmd_label = new Fl_Box(CMD_LABEL_DIMS(margin, y_pos, command_width), "Command Input:");
    gui->cmd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    gui->command_input = new Fl_Input(CMD_INPUT_DIMS(margin, y_pos, command_width));
    gui->command_input->callback(command_input_callback);
    gui->command_input->when(FL_WHEN_ENTER_KEY);
}

/**
 * Creates encryption method, key, and path controls
 * Args:
 *   gui: Pointer to GUI components
 *   encryption_x: Starting X position
 *   y_pos: Vertical position
 *   control_width: Width of each control
 *   spacing: Space between controls
 */
void create_encryption_controls(GuiComponents *gui, const int encryption_x, const int y_pos, const int control_width) {
    gui->enc_label = new Fl_Box(ENC_LABEL_DIMS(encryption_x, y_pos, control_width), "Decryption:");
    gui->enc_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    gui->encryption_choice = new Fl_Choice(ENC_CHOICE_DIMS(encryption_x, y_pos, control_width));
    for (int i = 0; encryption_methods[i] != nullptr; i++) {
        gui->encryption_choice->add(encryption_methods[i]);
    }
    gui->encryption_choice->value(FIRST_ENC_LIST_INDEX);
    gui->key_label = new Fl_Box(KEY_LABEL_DIMS(encryption_x, y_pos, control_width), "Key:");
    gui->key_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    gui->key_input = new Fl_Input(KEY_INPUT_DIMS(encryption_x, y_pos, control_width));
    gui->path_label = new Fl_Box(PATH_LABEL_DIMS(encryption_x, y_pos, control_width), "Path:");
    gui->path_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    gui->file_path_input = new Fl_Input(PATH_INPUT_DIMS(encryption_x, y_pos, control_width));
}

/**
 * Initializes encryption section including controls and submit button
 * Args:
 *   gui: Pointer to GUI components
 *   margin: Window margin size
 *   win_w: Window width
 *   y_pos: Vertical position for section
 *   command_width: Width of command section
 */
void initialize_encryption_section(GuiComponents *gui, const int margin, const int win_w, const int y_pos,
                                   const int command_width) {
    const int encryption_x = margin + command_width + margin;
    const int encryption_width = (win_w - ENCRYPTION_SECTION_MARGIN_MULTIPLIER * margin) * ENCRYPTION_SECTION_WIDTH_PCT
                                 / ENCRYPTION_PCT_DIVISOR;
    const int control_width = (encryption_width - ENCRYPTION_CONTROL_GAP) / ENCRYPTION_CONTROL_COUNT;
    create_encryption_controls(gui, encryption_x, y_pos, control_width);
    gui->submit_button = new Fl_Button(encryption_x + encryption_width - ENCRYPTION_BTN_WIDTH,
                                       y_pos + ENCRYPTION_BTN_Y_OFFSET,
                                       ENCRYPTION_BTN_WIDTH,
                                       ENCRYPTION_BTN_HEIGHT,
                                       "Decrypt");
    gui->submit_button->callback(submit_callback);
}

/**
 * Initializes and displays main GUI
 * Args:
 *   socket_fd: Socket for server communication
 */
void start_gui(const int socket_fd, const unsigned char *encryption_key) {
    cleanup_gui();
    const int screen_w = Fl::w();
    const int screen_h = Fl::h();
    const int win_w = screen_w * WINDOW_SIZE_PCT / ENCRYPTION_PCT_DIVISOR;
    const int win_h = screen_h * WINDOW_SIZE_PCT / ENCRYPTION_PCT_DIVISOR;
    gui = new GuiComponents;
    memset(gui, 0, sizeof(GuiComponents));
    gui->socket_fd = socket_fd;
    gui->encryption_key = encryption_key;
    gui->window = new Fl_Window(win_w, win_h, "Cryptography Game Client");
    constexpr int margin = MARGIN_SIZE;
    const int text_display_h = win_h - (TEXT_DISPLAY_HEIGHT_MULTIPLIER * ELEMENT_HEIGHT +
                                        TEXT_DISPLAY_MARGIN_MULTIPLIER * margin +
                                        TEXT_DISPLAY_OFFSET);
    const int command_width = (win_w - COMMAND_MARGIN_MULTIPLIER * margin) *
                              COMMAND_SECTION_WIDTH_PCT / ENCRYPTION_PCT_DIVISOR;
    initialize_text_display(gui, margin, win_w, text_display_h);
    int y_pos = text_display_h + Y_POS_MARGIN_MULTIPLIER * margin;
    initialize_cwd_section(gui, margin, win_w, y_pos);
    y_pos += Y_POS_INCREMENT_A + Y_POS_INCREMENT_B;
    initialize_command_section(gui, margin, y_pos, command_width);
    initialize_encryption_section(gui, margin, win_w, y_pos, command_width);
    gui->window->resizable(gui->text_display);
    gui->window->end();
    gui->window->show();
    Fl::add_timeout(UI_REFRESH_INTERVAL, periodic_update_cb);
    Fl::run();
}
