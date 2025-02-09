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
        win = new Fl_Window(300, 100, "Message");
        box = new Fl_Box(10, 10, 280, 40, message);
        box->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        btn = new Fl_Button(110, 60, 80, 30, "OK");
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
        Fl::repeat_timeout(0, delete_cb, msgWin); // Safe delayed delete
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
        gui->cwd_label->label(new_cwd);
        gui->window->redraw();
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
        usleep(5000);
        gui->text_buffer->append(message);
        gui->text_display->redraw();
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
    Fl::add_timeout(0, delete_win_cb, win); // Schedule safe deletion
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
    if (strlen(command) == 0 || strlen(command) > 250) {
        display_message("Unsupported command");
        return;
    }
    char buffer[512];
    prepare_buffer(buffer, sizeof(buffer), command, "CMD");
    s_send(gui->socket_fd, buffer, strlen(buffer));
    char message[1024];
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
    if (strcmp(encryption_method, "None") == 0 || strlen(key) == 0 || strlen(path) == 0 ||
        strlen(key) + strlen(path) > 225) {
        display_message("Unsupported command");
        return;
    }
    char command[512];
    snprintf(command, sizeof(command), "openssl enc -d -%s -in %s -out %s.dec -k %s -pbkdf2 && mv %s.dec %s",
             encryption_method, path, path, key, path, path);
    char buffer[1024];
    prepare_buffer(buffer, sizeof(buffer), command, "CMD");
    s_send(gui->socket_fd, buffer, strlen(buffer));
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
    gui->encryption_choice->value(0);
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
    gui->text_display = new Fl_Text_Display(margin, margin,
                                            win_w - 2 * margin,
                                            text_display_h);
    gui->text_buffer = new Fl_Text_Buffer();
    gui->text_display->buffer(gui->text_buffer);
    gui->text_display->textfont(FL_COURIER);
    gui->text_display->textsize(14);
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
    gui->cwd_label = new Fl_Box(margin, y_pos, win_w - 2 * margin, 20, "/home");
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
    gui->cmd_label = new Fl_Box(margin, y_pos, command_width, 20, "Command Input:");
    gui->cmd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->command_input = new Fl_Input(margin, y_pos + 25, command_width, 30);
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
    gui->enc_label = new Fl_Box(encryption_x, y_pos - 25,
                                control_width, 20, "Decryption:");
    gui->enc_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->encryption_choice = new Fl_Choice(encryption_x, y_pos,
                                           control_width, 30);
    for (int i = 0; encryption_methods[i] != nullptr; i++) {
        gui->encryption_choice->add(encryption_methods[i]);
    }
    gui->encryption_choice->value(0);

    gui->key_label = new Fl_Box(encryption_x + control_width + 10,
                                y_pos - 25, control_width, 20, "Key:");
    gui->key_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->key_input = new Fl_Input(encryption_x + control_width + 10,
                                  y_pos, control_width, 30);

    gui->path_label = new Fl_Box(encryption_x + 2 * (control_width + 10),
                                 y_pos - 25, control_width, 20, "Path:");
    gui->path_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->file_path_input = new Fl_Input(encryption_x + 2 * (control_width + 10),
                                        y_pos, control_width, 30);
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
    const int encryption_width = ((win_w - 3 * margin) * 35) / 100;
    const int control_width = (encryption_width - 20) / 3;

    create_encryption_controls(gui, encryption_x, y_pos, control_width);

    gui->submit_button = new Fl_Button(encryption_x + encryption_width - 100,
                                       y_pos + 40, 100, 30, "Decrypt");
    gui->submit_button->callback(submit_callback);
}

/**
 * Initializes and displays main GUI
 * Args:
 *   socket_fd: Socket for server communication
 */
void start_gui(const int socket_fd) {
    cleanup_gui();

    const int screen_w = Fl::w();
    const int screen_h = Fl::h();
    const int win_w = (screen_w * 80) / 100;
    const int win_h = (screen_h * 80) / 100;

    gui = new GuiComponents;
    memset(gui, 0, sizeof(GuiComponents));
    gui->socket_fd = socket_fd;
    gui->window = new Fl_Window(win_w, win_h, "Cryptography Game Client");

    constexpr int margin = 20;
    const int text_display_h = win_h - (4 * 30 + 5 * margin + 2 * 20);
    const int command_width = ((win_w - 3 * margin) * 60) / 100;

    initialize_text_display(gui, margin, win_w, text_display_h);

    int y_pos = text_display_h + 2 * margin;
    initialize_cwd_section(gui, margin, win_w, y_pos);

    y_pos += 20 + 10;
    initialize_command_section(gui, margin, y_pos, command_width);
    initialize_encryption_section(gui, margin, win_w, y_pos, command_width);

    gui->window->resizable(gui->text_display);
    gui->window->end();
    gui->window->show();
    Fl::run();
}
