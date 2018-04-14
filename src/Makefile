CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -lncurses -pthread -ggdb -Og

.PHONY: all
all: hw2

hw2: main.o util.o util.h sem.o sem.h
	$(CC) $(CFLAGS) main.o util.o sem.o -o hw2

main.o: main.c util.h

util.o: util.c util.h

sem.o: sem.c sem.h

.PHONY: clean
clean:
	rm -f *.o ./hw2
