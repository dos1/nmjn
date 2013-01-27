#include <gtk/gtk.h>

void on_window_destroy (GObject* object, gpointer user_data) {
		gtk_main_quit();
}

void derp(GtkEntry* object, GtkTextView *user_data) {
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	const gchar *text;

	text = gtk_entry_get_text (GTK_ENTRY (object));

	if (text[0]==0) return;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (user_data));

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (user_data), &iter, 0.0, FALSE, 0, 0);

	gtk_text_buffer_insert (buffer, &iter, "\n", -1);
	gtk_text_buffer_insert (buffer, &iter, text, -1);
	gtk_entry_set_text(object, "");
}

int main (int argc, char** argv) {
	gtk_init(&argc, &argv);

	GtkBuilder *builder;
	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "../interface.xml", NULL); // FIXME
	GtkWidget *window;
	window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

	GtkTextView* textview = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "textview1"));
	GtkTextBuffer *buffer;
	GtkTextIter iter;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (textview), &iter, 0.0, FALSE, 0, 0);

	gtk_text_buffer_insert (buffer, &iter, "NMJN client 0.666", -1);

	gtk_builder_connect_signals(builder, NULL);

	g_object_unref(G_OBJECT(builder));

	gtk_widget_show(window);
	gtk_main();
  return 0;
}
