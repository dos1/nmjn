#include "chat.h"

#include <glib.h>
#include <glib-unix.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>


struct server {
	int repo_key, repo_id;
	int sem_key, sem_id;
	int msg_key, msg_id;
	shm_type *repo;
	GMainLoop *loop;
} sv;

void log_line(const gchar *format, ...) {
	va_list vl;
	va_start(vl, format);
	char text[1024] = {};
	g_vsnprintf(text, 1024, format, vl);
	va_end(vl);

	char buf[1024];

	{
		char timebuf[64];
		time_t     now = time(NULL);
		struct tm *tstruct = localtime(&now);
		strftime(timebuf, sizeof(struct tm), "%H:%M:%S", tstruct);

		g_snprintf(buf, 1024, "[%s] %s\n", timebuf, text);
	}

	g_print("%s", buf);

}

void p(int semid, int semnum) {
	struct sembuf operation;
	operation.sem_num = semnum;
	operation.sem_op = -1;
	operation.sem_flg = 0;
	if(semop(semid, &operation, 1) == 1){
		log_line("Could not lower a semaphore %d:%d: %s", semid, semnum, strerror(errno));
		exit(1);
	}
}

void v(int semid, int semnum) {
	struct sembuf operation;
	operation.sem_num = semnum;
	operation.sem_op = 1;
	operation.sem_flg = 0;
	if(semop(semid, &operation, 1) == 1){
		log_line("Could not rise a semaphore %d:%d: %s", semid, semnum, strerror(errno));
		exit(1);
	}
}

int getClient(int client_key){
	int i=0;
	while (i<MAX_SERVER_COUNT*MAX_USER_COUNT_PER_SERVER) {
		if (sv.repo->clients[i].queue_key == client_key) {
			return i;
		}
		i++;
	}
	return -1;
}

int getClientByName(char name[16]) {
	int i=0;
	while (i<MAX_SERVER_COUNT*MAX_USER_COUNT_PER_SERVER) {
		if (g_strcmp0(sv.repo->clients[i].name,name) == 0) {
			return i;
		}
		i++;
	}

	return -1;
}

void deregister(int user) {
	p(sv.sem_id, CLIENT);
	if (user>=0) {
		log_line("Deregistering user %s (%d, room %s), bye bye!", sv.repo->clients[user].name, sv.repo->clients[user].queue_key, sv.repo->clients[user].room);
		sv.repo->clients[user].queue_key = -1;
		sv.repo->clients[user].server_queue_key = -1;
		strcpy(sv.repo->clients[user].name, "");
		strcpy(sv.repo->clients[user].room, "");
	} else {
		log_line("Tried to deregister non-existent user %d", user);
	}
	v(sv.sem_id, CLIENT);
}

void join(int user, gchar *room) {
	log_line("%s joins %s", sv.repo->clients[user].name, room);
	p(sv.sem_id, CLIENT);
	if (user>=0) {
		strcpy(sv.repo->clients[user].room, room);
	} else {
		log_line("...or not.");
	}
	v(sv.sem_id, CLIENT);
}

void room_msg(standard_message *standard, int user) {
	log_line("[%s] <%s> %s", standard->content.recipient, standard->content.sender, standard->content.message);
	int i=0;
	while (i<MAX_SERVER_COUNT*MAX_USER_COUNT_PER_SERVER) {
		if ((sv.repo->clients[i].server_queue_key == sv.msg_key) && (g_strcmp0(sv.repo->clients[i].room,standard->content.recipient)==0) && (i!=user)) {
			int client = msgget(sv.repo->clients[i].queue_key, 0777);
			msgsnd(client, standard, sizeof(standard_message), IPC_NOWAIT);
		}
		i++;
	}
}

static gboolean process(gpointer data) {

	compact_message *compact = g_malloc(sizeof(compact_message)+50);
	standard_message *standard = g_malloc(sizeof(standard_message)+50);
	server_message *server = g_malloc(sizeof(server_message)+50);

	if( msgrcv(sv.msg_id, compact, sizeof(compact_message), MSG_REGISTER, IPC_NOWAIT) != -1 ) {

		log_line("Yay! Got register request from %s (%d), new buddy's coming!", compact->content.sender, compact->content.value);

		int clients = 0, pos = -1, error = 0;

		p(sv.sem_id, CLIENT);

		int i;
		for (i=0; i<MAX_SERVER_COUNT*MAX_USER_COUNT_PER_SERVER; i++) {
			if (sv.repo->clients[i].server_queue_key == sv.msg_key) clients++;
			if ((sv.repo->clients[i].queue_key == -1) && (pos == -1)) pos=i;
			if (clients==MAX_USER_COUNT_PER_SERVER) { error = -2; log_line("Server full."); break; }
			if (g_strcmp0(sv.repo->clients[i].name, compact->content.sender)==0) { error = -1; log_line("User exists."); break; }
		}

		if (error==0) {
			sv.repo->clients[pos].queue_key = compact->content.value;
			sv.repo->clients[pos].server_queue_key = sv.msg_key;
			strcpy(sv.repo->clients[pos].name, compact->content.sender);
			strcpy(sv.repo->clients[pos].room, GLOBAL_ROOM_NAME);
			log_line("User registered!");
		}
		v(sv.sem_id, CLIENT);

		int client = msgget(compact->content.value, 0777);
		compact->content.value = error;
		msgsnd(client, compact, sizeof(compact_message), IPC_NOWAIT);

	}	else if( msgrcv(sv.msg_id, compact, sizeof(compact_message), MSG_UNREGISTER, IPC_NOWAIT) != -1 ) {
		log_line("Got deregister request...");
		deregister(getClient(compact->content.value));
	}	else if( msgrcv(sv.msg_id, standard, sizeof(standard_message), MSG_JOIN, IPC_NOWAIT) != -1 ) {

		int user = getClientByName(standard->content.sender);
		join(user, standard->content.message);

		if (user!=-1) {
			compact->type = MSG_JOIN;
			compact->content.id = standard->content.id;
			compact->content.value = 0;
			int client = msgget(sv.repo->clients[user].queue_key, 0777);
			msgsnd(client, compact, sizeof(compact_message), IPC_NOWAIT);
		}
	}	else if( msgrcv(sv.msg_id, compact, sizeof(compact_message), MSG_LEAVE, IPC_NOWAIT) != -1 ){

		join(getClient(compact->content.value), GLOBAL_ROOM_NAME);

		int msg_id = msgget(compact->content.value, 0777);
		compact->content.value = 0;
		msgsnd(msg_id, compact, sizeof(compact_message), IPC_NOWAIT);

	}	else if ( msgrcv(sv.msg_id, compact, sizeof(compact_message), MSG_LIST, IPC_NOWAIT) != -1 ) {

		int user = getClient(compact->content.value);
		if (user==-1) user = getClientByName(compact->content.sender);

		if (user==-1) {
			log_line("Non-existent user tried to request a user list. WTF?");
		} else {

			user_list list;
			list.type = MSG_LIST;
			list.content.id = compact->content.id;

			int i = 0;


			for (i=0; i<MAX_USER_LIST_LENGTH; i++) {
				strcpy(list.content.list[i],"");
			}

			p(sv.sem_id, CLIENT);
			char room[20];
			strcpy(room, sv.repo->clients[user].room);
			int j=0;
			for (i=0; i<MAX_SERVER_COUNT*MAX_USER_COUNT_PER_SERVER; i++) {
				if ((sv.repo->clients[i].queue_key != -1) && (g_strcmp0(sv.repo->clients[i].room, room)==0)) {
					strcpy(list.content.list[j], sv.repo->clients[i].name);
					j++;
				}
				i++;
			}
			//log_line("Sending list to user %s...", sv.repo->clients[user].name);
			v(sv.sem_id, CLIENT);

			int client = msgget(compact->content.value, 0777);
			msgsnd(client, &list, sizeof(user_list), IPC_NOWAIT);
		}
	}	else if( msgrcv(sv.msg_id, standard, sizeof(standard_message), MSG_ROOM, IPC_NOWAIT) != -1 ){

		server->type = MSG_SERVER;
		server->content.msg = *standard;
		p(sv.sem_id, CLIENT);
		int user=getClientByName(standard->content.sender);
		if (user!=-1) {
			room_msg(standard, user);
			v(sv.sem_id, CLIENT);
			p(sv.sem_id,SERVER);
			int i=0;
			while (i<MAX_SERVER_COUNT) {
				if ((sv.repo->servers[i].queue_key!=-1) && (sv.repo->servers[i].queue_key!=sv.msg_key)) {
					int serv = msgget(sv.repo->servers[i].queue_key, 0777);
					msgsnd(serv, server, sizeof(server_message), IPC_NOWAIT);
				}
				i++;
			}
			v(sv.sem_id,SERVER);
		} else {
			log_line("Got message from unregistered user %s", standard->content.sender);
			v(sv.sem_id, CLIENT);
		}
	}	else if( msgrcv(sv.msg_id, standard, sizeof(standard_message), MSG_PRIVATE, IPC_NOWAIT) != -1 ) {

		server->type = MSG_SERVER;
		server->content.msg = *standard;

		p(sv.sem_id, CLIENT);
		int user = getClientByName(standard->content.sender);
		int user2 = getClientByName(standard->content.recipient);

		if ((user!=-1) && (user2!=-1)) {
			if (sv.repo->clients[user].server_queue_key == sv.msg_key) {
				log_line("Sending PM from %s to %s", standard->content.sender, standard->content.recipient);
				int client = msgget(sv.repo->clients[user].queue_key, 0777);
				msgsnd(client, standard, sizeof(standard_message), IPC_NOWAIT);
			} else {
				log_line("Sending PM from %s to %s on server %d", standard->content.sender, standard->content.recipient, sv.repo->clients[user].server_queue_key);
				int serv = msgget(sv.repo->clients[user].server_queue_key, 0777);
				msgsnd(serv, server, sizeof(server_message), IPC_NOWAIT);
			}
		} else {
			log_line("Could not send PM [%d to %d]", user, user2);
		}

		v(sv.sem_id, CLIENT);
	}	else if( msgrcv(sv.msg_id, server, sizeof(server_message), MSG_SERVER, IPC_NOWAIT) != -1 ){

		standard_message standard = server->content.msg;
		p(sv.sem_id, CLIENT);
		int user;
		switch(standard.type){
			case MSG_PRIVATE:
				user = getClientByName(standard.content.recipient);
				log_line("Got PM from %s to %s from foreign server", standard.content.sender, standard.content.recipient);
				if (user!=-1) {
					int client=msgget(sv.repo->clients[user].queue_key, 0777);
					msgsnd(client, &standard, sizeof(standard_message), IPC_NOWAIT);
				} else {
					log_line("Recipient does not exist!");
				}
				break;
			case MSG_ROOM:
				room_msg(&standard, -1);
				break;
			default:
				break;
		}
		v(sv.sem_id, CLIENT);

	}

	g_free(compact);
	g_free(standard);
	g_free(server);

	return TRUE;
}

int repository_create() {

	log_line("Creating new repository...");

	sv.repo_key = 1024;
	sv.repo_id = -1;
	while (sv.repo_id == -1) {
		sv.repo_id = shmget(sv.repo_key, sizeof(shm_type), 0777 | IPC_CREAT | IPC_EXCL);
		sv.repo_key++;
	}
	sv.repo_key--;
	sv.repo = (shm_type*) shmat(sv.repo_id, NULL, 0);
	if (sv.repo == (void*)-1) {
		log_line("Could not get shared memory segment: %s", strerror(errno));
		return 1;
	}

	sv.sem_key = 1024;
	sv.sem_id = -1;
	while (sv.sem_id == -1) {
		sv.sem_id = semget(sv.sem_key, SEMAPHORE_COUNT, 0777 | IPC_CREAT | IPC_EXCL);
		sv.sem_key++;
	}
	sv.sem_key--;

	sv.repo->key_semaphores = sv.sem_key;

	semctl(sv.sem_id, SERVER, SETVAL, 1);
	semctl(sv.sem_id, CLIENT, SETVAL, 1);
	semctl(sv.sem_id, LOG, SETVAL, 1);

	int i = 0;

	p(sv.sem_id, SERVER);
	for (i=0; i<MAX_SERVER_COUNT; i++) {
		sv.repo->servers[i].queue_key = -1;
	}
	v(sv.sem_id, SERVER);

	p(sv.sem_id, CLIENT);
	for (i=0; i<MAX_USER_COUNT_PER_SERVER*MAX_SERVER_COUNT; i++) {
		strcpy(sv.repo->clients[i].name,"");
		strcpy(sv.repo->clients[i].room,"");
		sv.repo->clients[i].queue_key = -1;
	}
	v(sv.sem_id, CLIENT);

	return 0;
}


void repository_detach() {

	if (sv.repo_key==-1) return;

	log_line("Detaching from repository %d...", sv.repo_key);

	int counter = 0;
	p(sv.sem_id,SERVER);
	int i;
	for (i=0; i<MAX_SERVER_COUNT; i++) {
		if(sv.repo->servers[i].queue_key != -1) counter++;
		if(sv.repo->servers[i].queue_key == sv.msg_key) sv.repo->servers[i].queue_key = -1;
	}
	v(sv.sem_id,SERVER);

	p(sv.sem_id,CLIENT);
	for (i=0; i<MAX_SERVER_COUNT*MAX_USER_COUNT_PER_SERVER; i++) {
		if(sv.repo->clients[i].queue_key == -1) continue;
		if(sv.repo->clients[i].server_queue_key == sv.msg_key) sv.repo->clients[i].queue_key = -1;
	}
	v(sv.sem_id,CLIENT);

	shmdt(sv.repo);

	if (counter == 1) {
		// I was alone...
		log_line("Noone's left, deleting repository.");
		shmctl(sv.repo_id, IPC_RMID, 0);
		semctl(sv.sem_id, 0, IPC_RMID, 0);
	}
	msgctl(sv.msg_id, IPC_RMID, 0);

	sv.repo_key = -1;

}

int repository_attach(int key) {

	if (sv.repo_key!=-1) repository_detach();
	sv.repo_key = key;
	sv.repo_id = shmget(sv.repo_key, sizeof(shm_type), 0777);
	if (sv.repo_id == -1){
		log_line("Could not connect to repository %d: %s", sv.repo_key, strerror(errno));
		return repository_create();
	}
	sv.repo = (shm_type*) shmat(sv.repo_id, NULL, 0);
	if (sv.repo == (void*)-1 ){
		log_line("Could not attach to repository %d: %s", sv.repo_key, strerror(errno));
		return repository_create();
	}

	sv.sem_key = sv.repo->key_semaphores;
	sv.sem_id = semget(sv.sem_key, SEMAPHORE_COUNT, 0777 );
	if(sv.sem_id == -1){
		log_line("Could not attach to semaphores of repository %d: %s", sv.repo_key, strerror(errno));
		return repository_create();
	}
	return 0;
}


gboolean quit(gpointer data) {
	repository_detach();
	g_main_loop_quit(sv.loop);
	return FALSE;
}

int main(int argc, char** argv){

	sv.loop = g_main_loop_new(NULL, FALSE);

	log_line("NMJN-server 0.666, launching...");

	sv.repo_key = sv.repo_id = sv.sem_key = sv.sem_id = sv.msg_key = sv.msg_id = -1;
	sv.repo = NULL;

	if (argc<2) {
		if (repository_create()) return 1;
	} else {
		if (repository_attach(atoi(argv[1]))) return 1;
	}

	//create a server msg queue

	sv.msg_key = 2048;
	sv.msg_id = -1;
	while(sv.msg_id == -1){
		sv.msg_id = msgget(sv.msg_key, 0777 | IPC_CREAT | IPC_EXCL);
		sv.msg_key++;
	}
	sv.msg_key--;

	{
		p(sv.sem_id, SERVER);

		int i = 0;
		while ((i<MAX_SERVER_COUNT) && (sv.repo->servers[i].queue_key != -1)) {
			i++;
		}
		if (i<MAX_SERVER_COUNT) {
			sv.repo->servers[i].queue_key = sv.msg_key;
		} else {
			log_line("Oh noes, there's no place for us here!");
			v(sv.sem_id, SERVER);
			repository_detach();
			if (repository_create()) return 1;
			p(sv.sem_id, SERVER);
			sv.repo->servers[0].queue_key = sv.msg_key;
		}

		v(sv.sem_id, SERVER);
	}

	log_line("Alive and kicking.");
	log_line("Connect more servers to %d", sv.repo_key);
	log_line("Connect clients to %d", sv.msg_key);

	g_idle_add(process, NULL);
	g_unix_signal_add(SIGINT, quit, NULL);
	g_main_loop_run(sv.loop);

	log_line("Exiting...");
	return 0;
}

