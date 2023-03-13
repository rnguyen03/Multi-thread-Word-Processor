CC=gcc
CFLAGS= -Wall -pedantic -g -std=c11 -D_POSIX_C_SOURCE=200809L

ALL: A2

A2: A2.o
	$(CC) A2.o -o A2

A2.o: A2.c
	$(CC) $(CFLAGS) -c A2.c 

clean:
	rm *.o *.hist A2