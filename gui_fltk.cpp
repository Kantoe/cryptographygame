// gui_fltk.cpp
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
#include <pthread.h>

extern "C" {
void cleanup_gui();
}

#include "cryptography_game_util.h"

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
} GuiComponents;

static GuiComponents *gui = nullptr;
static pthread_mutex_t gui_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *encryption_methods[] = {
    "None",
    "aes-256-cbc",
    "aes-128-cbc",
    "des-ede3",
    nullptr
};

void update_cwd_label(const char *new_cwd) {
    if (gui && gui->cwd_label) {
        gui->cwd_label->label(new_cwd);
        gui->window->redraw();
    }
}

void append_to_text_view(const char *message) {
    if (gui && gui->text_buffer) {
        gui->text_buffer->append(message);
        gui->text_buffer->append("\n");
        gui->text_display->redraw();
    }
}

void display_message(const char *message, const int is_error) {
    if (is_error) {
        fl_alert("Error: %s", message);
    } else {
        fl_message("%s", message);
    }
}

static void submit_callback(Fl_Widget *, void *) {
    pthread_mutex_lock(&gui_mutex);

    const char *command = gui->command_input->value();
    const char *key = gui->key_input->value();
    const char *path = gui->file_path_input->value();
    const char *encryption_method = gui->encryption_choice->text();

    if (strcmp(encryption_method, "None") == 0) {
        encryption_method = "";
    }
    char message[512];
    snprintf(message, sizeof(message), "Sending: %s | %s | %s | %s", command, encryption_method, key, path);
    //add logic to send to server what is needed
    append_to_text_view(message);
    pthread_mutex_unlock(&gui_mutex);
}

void cleanup_gui() {
    if (!gui) return;

    delete gui->window;
    delete gui->text_display;
    delete gui->text_buffer;
    delete gui->command_input;
    delete gui->key_input;
    delete gui->file_path_input;
    delete gui->encryption_choice;
    delete gui->cwd_label;
    delete gui->submit_button;

    delete gui;
    gui = nullptr;
}

void start_gui(const int socket_fd) {
    cleanup_gui(); // Ensure old GUI is cleaned before starting a new one

    gui = new GuiComponents;
    gui->socket_fd = socket_fd;
    gui->window = new Fl_Window(700, 400, "Client GUI");
    gui->text_display = new Fl_Text_Display(20, 20, 660, 150);
    gui->text_buffer = new Fl_Text_Buffer();
    gui->text_display->buffer(gui->text_buffer);

    gui->command_input = new Fl_Input(120, 180, 350, 30, "Command:");
    gui->encryption_choice = new Fl_Choice(120, 220, 350, 30, "Encryption:");
    for (int i = 0; encryption_methods[i] != nullptr; i++) {
        gui->encryption_choice->add(encryption_methods[i]);
    }
    gui->encryption_choice->value(0);

    gui->key_input = new Fl_Input(120, 260, 350, 30, "Key:");
    gui->file_path_input = new Fl_Input(120, 300, 350, 30, "Path:");

    gui->cwd_label = new Fl_Box(FL_NO_BOX, 20, 360, 660, 30, "Working Directory:");

    gui->submit_button = new Fl_Button(250, 370, 100, 30, "Send");
    gui->submit_button->callback(submit_callback);

    gui->window->end();
    gui->window->show();
    Fl::run();
}
