CC=gcc
CFLAGS=-Wall -Wextra -std=gnu11 -pedantic -lncurses -lpthread

.PHONY: all
all: hw2

hw2: main.c do_not_submit.h sem.h sem.c
	$(CC) $(CFLAGS) main.c sem.c -o hw2

.PHONY: clean
clean:
	rm -f ./hw2 *.o
