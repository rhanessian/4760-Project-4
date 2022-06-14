#define _XOPEN_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/wait.h>  
#include <unistd.h>    
#include <string.h> 
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include "structure.h"

pid_t pids[MAXPIDS];

int shmid, msgid;
struct shrd_mem *shm;

struct queue_item {
	unsigned long seconds;
	unsigned long nanoseconds;
	int process_id;
};

struct queue_node {
	struct queue_item process;
	struct queue_node *next;
};

struct queue {
	struct queue_node *first;
	struct queue_node *last;
};

int queue_pop (struct queue *q, struct queue_item *process) {
	struct queue_node *current;
	if(!q->first)
		return -1;
	memcpy(process, &(q->first->process), sizeof(struct queue_item));
	current = q->first;
	if (q->first == q->last)
		q->first = q->last = NULL;
	else
		q->first = q->first->next;
	free(current);
	return 0;
}

void queue_push (struct queue *q, struct queue_item process) {
	struct queue_node *new_node;
	new_node = (struct queue_node *)calloc(sizeof(struct queue_node), 1);
	memcpy(&(new_node->process), &(process), sizeof(struct queue_item));
	if (!q->first && !q->last){
		q->first = q->last = new_node;
		return;
	}
	q->last->next = new_node;
	q->last = new_node;
}

// Catches signal
void sighandler(int signum) {
	printf("\nCaught signal %d, coming out...\n", signum);
	for (int i = 0; i < MAXPIDS; i++)
		if (pids[i] != 0)	
			kill(pids[i], SIGCHLD);
	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);
	msgctl(msgid, IPC_RMID, NULL);
	exit(1);
}

void delete_pid(pid_t pid) {
	for (int i = 0; i < MAXPIDS; i++) 
		if (pid == pids[i])
			pids[i] = 0;
}

void handle_child(int signum) {
	pid_t pid = wait(NULL);
	printf("Caught signal from pid %ld\n", (long)pid);
	delete_pid(pid);
}

int find_space(void) {
	for (int i = 0; i < MAXPIDS; i++) 
		if (pids[i] == 0)
			return i;
	return -1;
}

int main (int argc, char *argv[]) {
	signal(SIGINT, sighandler);
	signal(SIGCHLD, handle_child);
	signal(SIGALRM, sighandler);
	
	FILE* f = fopen("filename.txt", "w");
	
	remove("cstest");
	for (int i = 0; i < MAXPIDS; i++){
		char filename[32];
		snprintf(filename, sizeof(filename), "logfile.%d", i);
		remove(filename);
	}
	
	int c, n;
	
	int ss = 100;
	
	opterr = 0;
	
	while  ((c = getopt (argc, argv, "t:")) != -1)
		switch (c) {
			case 't': 
				ss = atoi(optarg);
				break;
			default: 
				printf("unknown\n");
				break;
		}
	
	if (argc <= optind) {
		printf("Need argument n\n");
		return(-1);
	} else 
		n = atoi(argv[optind]);	
	
	
	if (n > 20){
		fprintf(stderr, "Warning: n cannot be greater than 20.\n");
		n = 20;
	}
	
	alarm(ss);
	
	printf("ss = %d, n = %d\n", ss, n);
	key_t key_glock = ftok("master.c", 380);
	shmid = shmget(key_glock, sizeof(struct shrd_mem), 0666 | IPC_CREAT);
	shm = shmat(shmid, 0, 0);
	
	shm->sec = 0;
	shm->nanosec = 0;
	
	key_t young_dolph = ftok("master.c", 2938);
	msgid = msgget(young_dolph, 0666 | IPC_CREAT);
	
	if(msgid < 0)
		perror("msgget");
	
	pid_t pid;
	
	unsigned int next_sec = 1, next_nanosec = 0;
	
	int num_proc = 0;
	
	while(1) {
		int ind;
		while ((ind = find_space()) < 0)
			;
		
		long current = ((long)shm->sec * 1000000000ul) + ((long)shm->nanosec);
		long next = ((long)next_sec * 1000000000ul) + ((long)next_nanosec);
		
		if((current > next) && (num_proc < 1)){
			if((pid = fork()) == 0) {
				char string_num[8];
				snprintf(string_num, sizeof(string_num), "%d", ind);
				char *args[] = {"./child", string_num, 0};
				char *env[] = { 0 };
				execve("./child", args, env);
				perror("execve");
				exit(1);
			} else {
				pids[ind] = pid;
				num_proc++;
				fprintf(f, "Generating process with PID %d and putting it in queue %d at time %u:%u", pid, 0, shm->sec, shm->nanosec);
			}
		}
		
		if(num_proc > 0){
			struct mesg_buffer buf;
			buf.mesg_type = 1000000;
			snprintf(buf.mesg_text, sizeof(buf.mesg_text), "%d", 10000);

			if(msgsnd(msgid, &buf, sizeof(buf.mesg_text), 0) < 0)
				perror("Message didn't send");
			else {
				printf("Sent message\n");
				fprintf(f, "Dispatching process with PID %d from queue %d at time %u:%u", pid, 0, shm->sec, shm->nanosec);
			}
			struct mesg_buffer message;
		
			do {
				int retn = msgrcv(msgid, &message, sizeof(message.mesg_text), -9999, 0);
				if(retn < 0) {
					if(errno != EINTR) {
						perror("Message not received");
						break;
					} else 
						printf("EINTR received\n");	
				} else {
					printf("Received %s, Message type: %ld\n", message.mesg_text, message.mesg_type);
					break;
				}
			} while(1);
		}
		
		shm->nanosec += 10;
		
		if(shm->nanosec >= 1000000000) {
			shm->nanosec -= 1000000000;
			shm->sec++;
		}
	}	
		
	while(wait(NULL) > 0)
		;

	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);
	msgctl(msgid, IPC_RMID, NULL);
	
	fclose(f);
	
	return 0;
}










