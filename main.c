#include <gtk/gtk.h>
#include "chat.h"

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>

int server_queue = -1, client_queue = -1, client_key = -1;

void on_window_destroy (GObject* object, gpointer user_data) {
		gtk_main_quit();
}

void derp(GtkEntry* object, GtkTextView *user_data) {
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	const gchar *text;

	text = gtk_entry_get_text (object);

	if (text[0]==0) return;

	buffer = gtk_text_view_get_buffer (user_data);

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert (buffer, &iter, text, -1);
	gtk_text_buffer_insert (buffer, &iter, "\n", -1);

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_view_scroll_to_iter (user_data, &iter, 0.0, FALSE, 0, 0);

	gtk_entry_set_text(object, "");
}


int get_key(int key) {
	int qid = -1;
	do {
		qid = msgget(key, IPC_CREAT | IPC_EXCL | 0777);
		key++;
	} while (qid == -1);
	key--;
	g_print("Got key %d\n", key);
	client_key = key;
	return qid;
}


static gboolean idle(gpointer data) {
	//g_print("derp %d\n", queue);
	compact_message *compact = g_malloc(sizeof(compact_message));
	standard_message *standard = g_malloc(sizeof(standard_message));
	if (msgrcv(client_queue, compact, sizeof(compact_message), MSG_HEARTBEAT, IPC_NOWAIT)!=-1) {
		g_print("puk puk %i\n", compact->content.id);
		compact->content.value = client_key;
		msgsnd(server_queue, compact, sizeof(compact_message), IPC_NOWAIT);
	} else if (msgrcv(client_queue, compact, sizeof(compact_message), 2, IPC_NOWAIT)!=-1) {
		g_print("compact type: %lu, content.id: %d, content.sender: %s, content.value = %d\n", compact->type, compact->content.id, compact->content.sender, compact->content.value);
	} else if (msgrcv(client_queue, standard, sizeof(standard_message), 888, IPC_NOWAIT)!=-1) {
		g_print("standard type: %lu, content.sender: %s\n", standard->type, standard->content.sender);
	} //TODO: rest of message types
	g_free(compact);
	g_free(standard);
	return TRUE;
}

int server_connect() {
	client_queue = get_key(42);
	int server_key = 679; //FIXME: hardcoded server key
	g_print("Trying to connect to server %d...\n", server_key);
	server_queue = msgget(server_key, 0777);
	if (server_queue == -1) {
		perror("Could not connect to server");
		return 1;
	}
	compact_message msg;
	msg.type = MSG_REGISTER;
	msg.content.id = 1;
	strcpy(msg.content.sender, "dos"); //FIXME: hardcoded nickname
	msg.content.value = client_key;
	msgsnd(server_queue, &msg, sizeof(compact_message), IPC_NOWAIT);

	return 0;
}

void server_disconnect() {
	int ret = msgctl(client_queue, IPC_RMID, NULL);
	if (ret!=0) {
		perror("Could not remove message queue");
	}
}

int main (int argc, char** argv) {

	if (server_connect()!=0) {
		return 1;
	}

	gtk_init(&argc, &argv);

	GtkBuilder *builder;
	builder = gtk_builder_new();
	gtk_builder_add_from_file(builder, "../interface.xml", NULL); // FIXME: path
	GtkWidget *window;
	window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));

	GtkTextView* textview = GTK_TEXT_VIEW(gtk_builder_get_object(builder, "textview1"));
	GtkTextBuffer *buffer;
	GtkTextIter iter;

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (textview), &iter, 0.0, FALSE, 0, 0);

	gtk_text_buffer_insert (buffer, &iter, "NMJN client 0.666\n", -1);

	gtk_builder_connect_signals(builder, NULL);

	g_object_unref(G_OBJECT(builder));

	g_idle_add(idle, NULL);

	gtk_widget_show(window);
	gtk_main();

	server_disconnect();
	return 0;
}
