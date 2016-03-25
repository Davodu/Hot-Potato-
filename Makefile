CC = gcc
CFLAGS = -g

all: ringmaster player

ringmaster: ringmaster.o
	$(CC) $(CFLAGS) -o $@ ringmaster.o

player: player.o
	$(CC) $(CFLAGS) -o $@ player.o

ringmaster.o:	ringmaster.c potato.h

player.o:	player.c potato.h

clobber:
	rm -f *.o *~

clean:
	rm -rf *.o ringmaster player *~