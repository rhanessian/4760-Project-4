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

void sighandler(int signum) {
	printf("Caught signal %d, coming out...\n", signum);
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
	
	for (int i = 0; i < n; i++) {
		int ind;
		while ((ind = find_space()) < 0)
			;
			
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
		}
		
		struct mesg_buffer buf;
		buf.mesg_type = 1;
		strcpy(buf.mesg_text, "Testing");
		
		if(msgsnd(msgid, &buf, sizeof(buf.mesg_text), 0) < 0)
			perror("Message didn't send");
		else
			printf("Sent message\n");
	
		struct mesg_buffer message;
		
		do {
			int retn = msgrcv(msgid, &message, sizeof(message.mesg_text), 2, 0);
			if(retn < 0){
				if(errno != EINTR){
					perror("Message not received");
					break;
				} else 
					printf("EINTR received\n");	
			} else {
				printf("Received %s\n", message.mesg_text);
				break;
			}
		} while(1);
	}	
		
	while(wait(NULL) > 0)
		;

	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);
	msgctl(msgid, IPC_RMID, NULL);
	
	return 0;
}










