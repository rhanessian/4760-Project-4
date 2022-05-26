#include <sys/msg.h>
#define MAXPIDS 20

struct shrd_mem {
	bool choosing[MAXPIDS];
	int numbers[MAXPIDS];
};

struct mesg_buffer {
    long mesg_type;
    char mesg_text[100];
};