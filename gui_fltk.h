// gui_fltk.h
#ifndef GUI_FLTK_H
#define GUI_FLTK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>

extern pthread_mutex_t output_mutex;
extern pthread_mutex_t cwd_buffer_mutex;
extern char output_buffer[4096];
extern char cwd_buffer[1024];
extern bool output_updated;
extern bool cwd_updated;

/**
 * Initializes and displays main GUI
 * Args:
 *   socket_fd: Socket for server communication
 */
void start_gui(int socket_fd, const unsigned char *encryption_key);

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
void display_message(const char *message);

/**
 * Frees GUI resources
 * Operation:
 *   - Deletes widgets in reverse order
 *   - Frees GUI structure
 *   - Nulls global pointer
 * Returns: void
 */
void cleanup_gui();

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
void set_connection_status(bool is_closed);

#ifdef __cplusplus
}
#endif

#endif // GUI_FLTK_H
