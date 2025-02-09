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

void display_message(const char *message) {
    new MessageWindow(message); // Allocates safely, deleted later
}

// GUI Components structure
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

void set_connection_status(const bool is_closed) {
    if (gui) {
        gui->connection_closed = is_closed;
        if (is_closed) {
            display_message("Connection closed, close window");
        }
    }
}

void update_cwd_label(const char *new_cwd) {
    if (gui && gui->cwd_label) {
        gui->cwd_label->label(new_cwd);
        gui->window->redraw();
    }
}

void append_to_text_view(const char *message) {
    if (gui && gui->text_buffer) {
        usleep(5000);
        gui->text_buffer->append(message);
        gui->text_display->redraw();
    }
}

void delete_win_cb(void *data) {
    delete static_cast<Fl_Window *>(data); // Safe delete after FLTK processes events
}

void close_cb(Fl_Widget *, void *data) {
    auto *win = static_cast<Fl_Window *>(data);
    win->hide();
    Fl::add_timeout(0, delete_win_cb, win); // Schedule safe deletion
}

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

static void command_input_callback(Fl_Widget *w, void *) {
    auto *input = dynamic_cast<Fl_Input *>(w);
    const char *command = input->value();
    handle_regular_command(command);
    input->value(""); // Clear the input after sending
}

static void submit_callback(Fl_Widget *, void *) {
    handle_encryption_command();
    gui->encryption_choice->value(0);
    gui->key_input->value("");
    gui->file_path_input->value("");
}

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

void start_gui(const int socket_fd) {
    cleanup_gui(); // Ensure old GUI is cleaned before starting a new one
    // Get screen dimensions
    const int screen_w = Fl::w();
    const int screen_h = Fl::h();
    // Calculate window size (80% of screen)
    const int win_w = (screen_w * 80) / 100;
    const int win_h = (screen_h * 80) / 100;
    gui = new GuiComponents;
    // Initialize all pointers to nullptr
    memset(gui, 0, sizeof(GuiComponents));
    gui->socket_fd = socket_fd;
    gui->window = new Fl_Window(win_w, win_h, "Cryptography Game Client");
    // Calculate margins and spacing
    constexpr int margin = 20;
    constexpr int input_height = 30;
    constexpr int label_height = 20;
    constexpr int spacing = 10;
    // Text display takes up most of the window
    const int text_display_h = win_h - (4 * input_height + 5 * margin + 2 * label_height);
    gui->text_display = new Fl_Text_Display(margin, margin,
                                            win_w - 2 * margin,
                                            text_display_h);
    gui->text_buffer = new Fl_Text_Buffer();
    gui->text_display->buffer(gui->text_buffer);
    gui->text_display->textfont(FL_COURIER);
    gui->text_display->textsize(14);
    // Bottom section layout
    int y_pos = text_display_h + 2 * margin;
    // Working directory label
    gui->cwd_label = new Fl_Box(margin, y_pos, win_w - 2 * margin, label_height, "/home");
    gui->cwd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y_pos += label_height + spacing;
    // Command section
    const int command_width = ((win_w - 3 * margin) * 60) / 100;
    // Command label
    gui->cmd_label = new Fl_Box(margin, y_pos, command_width, label_height, "Command Input:");
    gui->cmd_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    y_pos += label_height + 5;
    // Command input
    gui->command_input = new Fl_Input(margin, y_pos, command_width, input_height);
    gui->command_input->callback(command_input_callback);
    gui->command_input->when(FL_WHEN_ENTER_KEY);
    // Encryption controls section
    const int encryption_x = margin + command_width + margin;
    const int encryption_width = ((win_w - 3 * margin) * 35) / 100;
    const int control_width = (encryption_width - 2 * spacing) / 3;
    // Encryption method section
    gui->enc_label = new Fl_Box(encryption_x, y_pos - label_height - 5,
                                control_width, label_height, "Decryption:");
    gui->enc_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->encryption_choice = new Fl_Choice(encryption_x, y_pos,
                                           control_width, input_height);
    for (int i = 0; encryption_methods[i] != nullptr; i++) {
        gui->encryption_choice->add(encryption_methods[i]);
    }
    gui->encryption_choice->value(0);
    // Key section
    gui->key_label = new Fl_Box(encryption_x + control_width + spacing,
                                y_pos - label_height - 5,
                                control_width, label_height, "Key:");
    gui->key_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->key_input = new Fl_Input(encryption_x + control_width + spacing,
                                  y_pos, control_width, input_height);
    // Path section
    gui->path_label = new Fl_Box(encryption_x + 2 * (control_width + spacing),
                                 y_pos - label_height - 5,
                                 control_width, label_height, "Path:");
    gui->path_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    gui->file_path_input = new Fl_Input(encryption_x + 2 * (control_width + spacing),
                                        y_pos, control_width, input_height);
    y_pos += input_height + spacing;
    // Submit button
    gui->submit_button = new Fl_Button(encryption_x + encryption_width - 100,
                                       y_pos, 100, input_height, "Decrypt");
    gui->submit_button->callback(submit_callback);
    gui->window->resizable(gui->text_display);
    gui->window->end();
    gui->window->show();
    Fl::run();
}
