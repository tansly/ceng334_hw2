#include "util.h"

#include <assert.h>
#include <curses.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

void *ant_main(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_testcancel();
    }

    assert(0);
    return NULL;
}

static void print_usage(char **argv)
{
    fprintf(stderr, "Usage: %s n_ants n_food max_seconds\n", argv[0]);
}

static pthread_t *ants_create(int n_ants)
{
    int i;
    pthread_t *threads = malloc(n_ants * sizeof *threads);
    for (i = 0; i < n_ants; i++) {
        if (pthread_create(&threads[i], NULL, ant_main, NULL) != 0) {
            perror("ants_create(): pthread_create()");
            exit(EXIT_FAILURE);
        }
    }
    return threads;
}

static void ants_cancel_join(pthread_t *threads, int n_ants)
{
    int i;
    for (i = 0; i < n_ants; i++) {
        if (pthread_cancel(threads[i]) != 0) {
            perror("ants_cancel_join(): pthread_cancel()");
            exit(EXIT_FAILURE);
        }
    }
    for (i = 0; i < n_ants; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("ants_cancel_join(): pthread_join()");
            exit(EXIT_FAILURE);
        }
    }
    free(threads);
}

int main(int argc, char **argv)
{
    srand(time(NULL));

    int n_ants;
    int n_food;
    int max_seconds;
    if (argc != 4) {
        print_usage(argv);
        return 1;
    }
    if (sscanf(argv[1], "%d", &n_ants) != 1) {
        print_usage(argv);
        return EXIT_FAILURE;
    }
    if (sscanf(argv[2], "%d", &n_food) != 1) {
        print_usage(argv);
        return EXIT_FAILURE;
    }
    if (sscanf(argv[3], "%d", &max_seconds) != 1) {
        print_usage(argv);
        return EXIT_FAILURE;
    }

    pthread_t *ant_threads = ants_create(n_ants);
    ants_cancel_join(ant_threads, n_ants);

    return 0;

    //////////////////////////////
    // Fills the grid randomly to have something to draw on screen.
    // Empty spaces have to be -.
    // You should get the number of ants and foods from the arguments 
    // and make sure that a food and an ant does not get placed at the same cell.
    // You must use putCharTo() and lookCharAt() to access/change the grid.
    // You should be delegating ants to separate threads
    int i,j;
    for (i = 0; i < GRIDSIZE; i++) {
        for (j = 0; j < GRIDSIZE; j++) {
            putCharTo(i, j, '-');
        }
    }
    int a,b;
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b)!= '-');
        putCharTo(a, b, 'P');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, '1');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, 'o');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, 'S');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, '$');
    }
    //////////////////////////////

    // you have to have following command to initialize ncurses.
    startCurses();

    // You can use following loop in your program. But pay attention to 
    // the function calls, they do not have any access control, you 
    // have to ensure that.
    char c;
    while (TRUE) {
        drawWindow();

        c = 0;
        c = getch();

        if (c == 'q' || c == ESC) break;
        if (c == '+') {
            setDelay(getDelay()+10);
        }
        if (c == '-') {
            setDelay(getDelay()-10);
        }
        if (c == '*') {
            setSleeperN(getSleeperN()+1);
        }
        if (c == '/') {
            setSleeperN(getSleeperN()-1);
        }
        usleep(DRAWDELAY);

        // each ant thread have to sleep with code similar to this
        //usleep(getDelay() * 1000 + (rand() % 5000));
    }

    // do not forget freeing the resources you get
    endCurses();

    return 0;
}
