#include "util.h"

#include <assert.h>
#include <curses.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define REPR_EMPTY '-'
#define REPR_FOOD 'o'
#define REPR_ANT '1'
#define REPR_FOODANT 'P'
#define REPR_SLEEPANT 'S'
#define REPR_FOODSLEEPANT '$'

/* Mutex protecting the number of sleepers,
 * i.e. the functions getSleeperN() and setSleeperN().
 */
pthread_mutex_t sleeper_lock = PTHREAD_MUTEX_INITIALIZER;
/* Condition variable for the sleepers.
 * It must be broadcast (not signal) since there may be several threads waiting
 * on it but only a specific one, the one with the proper id, can continue.
 */
pthread_cond_t sleeper_cond = PTHREAD_COND_INITIALIZER;
/* Global grid lock. Will implement finer-grained locking later.
 */
pthread_mutex_t grid_lock = PTHREAD_MUTEX_INITIALIZER;

/* Checks the number of sleepers against the id, sleeps if necessary.
 * XXX: Where to put REPR_SLEEPANT or REPR_FOODSLEEPANT?
 */
void ant_sleep(int id)
{
    pthread_mutex_lock(&sleeper_lock);
    while (getSleeperN() > id) {
        pthread_cond_wait(&sleeper_cond, &sleeper_lock);
    }
    pthread_mutex_unlock(&sleeper_lock);
}

void *ant_main(void *arg)
{
    int id = *(int*)arg;
    free(arg);

    /* Find somewhere to sit.
     */
    int i_pos, j_pos;
    pthread_mutex_lock(&grid_lock);
    while (i_pos = rand() % GRIDSIZE, j_pos = rand() % GRIDSIZE,
            lookCharAt(i_pos, j_pos) != REPR_EMPTY)
        ;
    putCharTo(i_pos, j_pos, '1');
    pthread_mutex_unlock(&grid_lock);

    for (;;) {
        ant_sleep(id);
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
        /* If we pass &i, threads may get the wrong value if the loop
         * goes on before they read *i. So we pass a copy of each i.
         * The malloc'ed int is free'd by the thread that gets it.
         */
        int *id = malloc(sizeof *id);
        *id = i;
        if (pthread_create(&threads[i], NULL, ant_main, id) != 0) {
            perror("ants_create(): pthread_create()");
            exit(EXIT_FAILURE);
        }
    }
    return threads;
}

/* Ant threads live for the lifetime of the program.
 * Before freeing global resources, we should stop and join them.
 * (Can we get away with just detaching them in the beginning?
 * I think we cannot because there may be some threads accessing the global
 * resources before termination. We must not free them before the threads exit)
 * This function cancels and joins the threads in the given array.
 */
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

    /* Initialize grid with food at random locations.
     * We are the only thread now, so we cool.
     */
    int i, j;
    for (i = 0; i < GRIDSIZE; i++) {
        for (j = 0; j < GRIDSIZE; j++) {
            putCharTo(i, j, REPR_EMPTY);
        }
    }
    for (i = 0; i < n_food; i++) {
        int a, b;
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        } while (lookCharAt(a, b) != REPR_EMPTY);
        putCharTo(a, b, REPR_FOOD);
    }

    pthread_t *ant_threads = ants_create(n_ants);
    /* Ants are running. From now on, the grid must be protected.
     */
    pthread_mutex_lock(&grid_lock);
    startCurses();
    pthread_mutex_unlock(&grid_lock);

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

    ants_cancel_join(ant_threads, n_ants);
    endCurses();
    return 0;
}
