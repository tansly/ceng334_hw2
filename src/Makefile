CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -lncurses -pthread

.PHONY: all
all: hw2

hw2: main.c do_not_submit.h
	$(CC) main.c $(CFLAGS) -o hw2

.PHONY: clean
clean:
	rm -f ./hw2 *.o
