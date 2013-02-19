#include "chat.h"

#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <glib/gprintf.h>

struct client {
		int server_queue, client_queue, client_key, server_key;
		GtkTextView *textview;
		GtkEntry* entry;
		gchar *nick;
} cl;


void display_line(GtkTextView *textview, const gchar *format, ...) {
	va_list vl;
	va_start(vl, format);
	char text[1024] = {};
	g_vsnprintf(text, 1024, format, vl);
	va_end(vl);

	if (text[0]==0) return;

	char buf[1024];

	{
		char timebuf[64];
		time_t     now = time(NULL);
		struct tm *tstruct = localtime(&now);
		strftime(timebuf, sizeof(struct tm), "%H:%M:%S", tstruct);

		g_snprintf(buf, 1024, "[%s] %s\n", timebuf, text);
	}

	GtkTextBuffer *buffer;
	GtkTextIter iter;

	buffer = gtk_text_view_get_buffer (textview);

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert (buffer, &iter, buf, -1);

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_view_scroll_to_iter (textview, &iter, 0.0, FALSE, 0, 0);

	g_print("%s", buf);

}


int get_key(int key) {
	int qid = -1;
	do {
		qid = msgget(key, IPC_CREAT | IPC_EXCL | 0777);
		key++;
	} while (qid == -1);
	key--;
	display_line(cl.textview, "Got key %d", key);
	cl.client_key = key;
	return qid;
}

void server_disconnect() {
	if (cl.server_key!=-1) {
		int ret = msgctl(cl.client_queue, IPC_RMID, NULL);
		if (ret!=0) {
			display_line(cl.textview, "Could not remove message queue: %s", strerror(errno));
		}
		display_line(cl.textview, "Disconnected from server %d", cl.server_key);
		cl.server_key = -1;
	} else {
		display_line(cl.textview, "Not connected.");
	}
}

int server_connect(int key, gchar *nick) {
	cl.client_queue = get_key(42);
	cl.server_key = key;
	display_line(cl.textview, "Trying to connect to server %d...", cl.server_key);
	cl.server_queue = msgget(cl.server_key, 0777);
	if (cl.server_queue == -1) {
		display_line(cl.textview, "Could not connect to server: %s", strerror(errno));
		server_disconnect();
		return 1;
	}

	display_line(cl.textview, "Connected to server %d", cl.server_key);

	compact_message msg;
	msg.type = MSG_REGISTER;
	msg.content.id = 1;
	strcpy(msg.content.sender, nick);
	msg.content.value = cl.client_key;
	msgsnd(cl.server_queue, &msg, sizeof(compact_message), IPC_NOWAIT);

	cl.nick = g_strdup(nick);

	return 0;
}

void leave() {
	if (cl.server_key==-1) {
		display_line(cl.textview, "Not connected.");
		return;
	}

	compact_message msg;
	msg.type=MSG_LEAVE;
	msg.content.value = cl.client_key;
	display_line(cl.textview, "Leaving current room...");
	msgsnd(cl.server_queue, &msg, sizeof(compact_message), IPC_NOWAIT);
}

void join(const gchar *room) {
	if (cl.server_key==-1) {
		display_line(cl.textview, "Not connected.");
		return;
	}

	standard_message msg;
	msg.type=MSG_JOIN;
	strcpy(msg.content.sender, "dos");
	strcpy(msg.content.message, room);
	display_line(cl.textview, "Attempting to join %s...", room);
	msgsnd(cl.server_queue, &msg, sizeof(standard_message), IPC_NOWAIT);
}

void derp(GtkEntry* object, GtkTextView *user_data) {
	const gchar *text;

	text = gtk_entry_get_text (object);

	if (text[0]=='/') {
		gchar **set = g_strsplit(text, " ", -1), **temp=set;
		gchar *args[g_strv_length(set)]; int i=0;
		while (*temp != NULL) {
			if (**temp!=0) {
				//g_print("%s\n", *temp);
				args[i]=*temp;
				i++;
			}
			temp++;
		}
		args[i]=NULL;

		if (g_strcmp0(args[0], "/connect")==0) {
			if (g_strv_length(args)>2) server_connect(atoi(args[1]), args[2]);
			else display_line(user_data, "Not enough parameters.");
		} else if (g_strcmp0(args[0], "/disconnect")==0) {
			server_disconnect();
		} else if (g_strcmp0(args[0], "/join")==0) {
			if (g_strv_length(args)>1) join(args[1]);
			else display_line(user_data, "Not enough parameters.");
		} else if (g_strcmp0(args[0], "/leave")==0) {
			leave();
		} else if (g_strcmp0(args[0], "/help")==0) {
			display_line(user_data, "No help for you, silly pants!");
		} else if (g_strcmp0(args[0], "/quit")==0) {
			server_disconnect();
			gtk_main_quit();
		} else {
			display_line(user_data, "Invalid command. Type /help to show list of possible commands.");
		}

		g_strfreev(set);
	} else {
		display_line(user_data, "%s", text);
	}

	gtk_entry_set_text(object, "");
}

static gboolean idle(gpointer data) {
	compact_message *compact = g_malloc(sizeof(compact_message));
	standard_message *standard = g_malloc(sizeof(standard_message));
	if (msgrcv(cl.client_queue, compact, sizeof(compact_message), MSG_HEARTBEAT, IPC_NOWAIT)!=-1) {
		//HEARTBEAT
		g_print("puk puk %i\n", compact->content.id);
		compact->content.value = cl.client_key;
		msgsnd(cl.server_queue, compact, sizeof(compact_message), IPC_NOWAIT);
	} else if (msgrcv(cl.client_queue, compact, sizeof(compact_message), MSG_REGISTER, IPC_NOWAIT)!=-1) {
		//REGISTERED

		if (compact->content.value==0) {
			//OK
			display_line(cl.textview, "Registered as %s", compact->content.sender);
			display_line(cl.textview, "You're now in global room.");

		} else if (compact->content.value==-1) {
			//nick exists
			display_line(cl.textview, "Login taken.");
			server_disconnect();
		} else if (compact->content.value==-2) {
			//server full
			display_line(cl.textview, "Server full.");
			server_disconnect();
		}
		//g_print("compact type: %lu, content.id: %d, content.sender: %s, content.value = %d\n", compact->type, compact->content.id, compact->content.sender, compact->content.value);

	} else if (msgrcv(cl.client_queue, compact, sizeof(compact_message), MSG_JOIN, IPC_NOWAIT)!=-1) {
		//JOIN
		if (compact->content.value==0) {
			//OK
			display_line(cl.textview, "You're now in selected room.");
		} else if (compact->content.value!=0) {
			//nick exists
			display_line(cl.textview, "Something bad has happened [join].");
		}
	} else if (msgrcv(cl.client_queue, compact, sizeof(compact_message), MSG_LEAVE, IPC_NOWAIT)!=-1) {
		//LEAVE
		if (compact->content.value==0) {
			//OK
			display_line(cl.textview, "You're now in global room.");
		} else if (compact->content.value!=0) {
			//nick exists
			display_line(cl.textview, "Something bad has happened [leave].");
		}
	}	else if (msgrcv(cl.client_queue, standard, sizeof(standard_message), 888, IPC_NOWAIT)!=-1) {
		g_print("standard type: %lu, content.sender: %s\n", standard->type, standard->content.sender);
	} //TODO: rest of message types
	g_free(compact);
	g_free(standard);
	return TRUE;
}

void on_window_destroy (GObject* object, gpointer user_data) {
		server_disconnect();
		gtk_main_quit();
}

int main (int argc, char** argv) {

	cl.client_key = cl.client_queue = cl.server_key = cl.server_queue = -1;

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

	cl.textview = textview;
	cl.entry = GTK_ENTRY(gtk_builder_get_object(builder, "entry1"));

	gtk_builder_connect_signals(builder, NULL);

	g_object_unref(G_OBJECT(builder));

	g_idle_add(idle, NULL);

	gtk_widget_show(window);

	display_line(cl.textview, "Type /help to show list of supported commands.");
	display_line(cl.textview, "To connect, use /connect SERVER_KEY NICKNAME");

	gtk_main();

	return 0;
}
