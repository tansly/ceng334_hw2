CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -lncurses -pthread -ggdb -Og

.PHONY: all
all: hw2

hw2: main.o util.o util.h
	$(CC) $(CFLAGS) main.o util.o -o hw2

main.o: main.c util.h

util.o: util.c util.h

.PHONY: clean
clean:
	rm -f *.o ./hw2
