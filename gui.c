#include <gtk/gtk.h>
#include "cryptography_game_util.h"

static void on_command_enter(GtkWidget *widget, gpointer data) {
    const char *command = gtk_entry_get_text(GTK_ENTRY(widget));
    GtkTextView *text_view = GTK_TEXT_VIEW(data);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, command, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", -1);
    gtk_entry_set_text(GTK_ENTRY(widget), "");
}

static void on_encryption_selected(GtkComboBoxText *widget, gpointer data) {
    const char *method = gtk_combo_box_text_get_active_text(widget);
    if (method) {
        g_print("Selected encryption: %s\n", method);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Client GUI");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget *cwd_label = gtk_label_new("Current Directory: /home");
    gtk_box_pack_start(GTK_BOX(vbox), cwd_label, FALSE, FALSE, 0);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    GtkWidget *entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
    g_signal_connect(entry, "activate", G_CALLBACK(on_command_enter), text_view);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "aes-256-cbc");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "aes-128-cbc");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "des-ede3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "bf-cbc");
    gtk_box_pack_start(GTK_BOX(hbox), combo, FALSE, FALSE, 0);
    g_signal_connect(combo, "changed", G_CALLBACK(on_encryption_selected), NULL);

    GtkWidget *key_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(key_entry), "Enter encryption key");
    gtk_box_pack_start(GTK_BOX(hbox), key_entry, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}

