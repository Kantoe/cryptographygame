// gui_fltk.h
#ifndef GUI_FLTK_H
#define GUI_FLTK_H

#ifdef __cplusplus
extern "C" {
#endif

void start_gui(int socket_fd);

void update_cwd_label(const char *new_cwd);

void display_message(const char *message);

void append_to_text_view(const char *message);

void cleanup_gui();

void set_connection_status(bool is_closed);

#ifdef __cplusplus
}
#endif

#endif // GUI_FLTK_H
