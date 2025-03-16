// gui_fltk.h
#ifndef GUI_FLTK_H
#define GUI_FLTK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes and displays main GUI
 * Args:
 *   socket_fd: Socket for server communication
 */
void start_gui(int socket_fd, const unsigned char *encryption_key);

/**
 * Updates working directory display
 * Args:
 *   new_cwd: String containing new working directory path
 * Operation:
 *   - Updates CWD label if GUI exists
 *   - Triggers window redraw
 * Returns: void
 */
void update_cwd_label(const char *new_cwd);

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
  * Adds text to main display area
  * Args:
  *   message: Text string to append
  * Operation:
  *   - Appends to text buffer if GUI exists
  *   - Triggers text display redraw
  * Returns: void
*/
void append_to_text_view(const char *message);

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
