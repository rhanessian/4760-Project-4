CC = gcc

all: master child

master: master.c
	gcc -std=c99 $(CFLAGS) -o master master.c

child: child.c
	gcc -std=c99 $(CFLAGS) -o child child.c

clean:
	$(RM) child master
