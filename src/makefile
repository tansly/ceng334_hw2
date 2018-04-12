CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -lncurses -pthread -ggdb -Og

.PHONY: all
all: hw2

hw2: main.c do_not_submit.h
	$(CC) $(CFLAGS) main.c -o hw2

.PHONY: clean
clean:
	rm -f ./hw2
