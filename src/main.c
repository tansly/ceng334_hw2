#include "util.h"
#include "sem.h"

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
static pthread_mutex_t sleeper_lock = PTHREAD_MUTEX_INITIALIZER;
/* Mutex protecting the delay value,
 * i.e the functions getDelay() and setDelay()
 */
static pthread_mutex_t delay_lock = PTHREAD_MUTEX_INITIALIZER;
/* Condition variable for the sleepers.
 * It must be broadcast (not signal) since there may be several threads waiting
 * on it but only a specific one, the one with the proper id, can continue.
 */
static pthread_cond_t sleeper_cond = PTHREAD_COND_INITIALIZER;
/* Variable that signals the threads to continue or stop and the
 * mutex to protect it.
 */
static pthread_mutex_t running_lock = PTHREAD_MUTEX_INITIALIZER;
static int running = 1;
/* Locks for individual cells. Same size as the grid, stored in row-major order.
 * Allocated and initialized by ants_create(), free'd by ants_stop_join().
 */
static pthread_mutex_t *cell_locks;
/* Semaphore signalling if the grid is available, i.e. not used by any thread.
 * This will block the main thread from entering while there are ant threads
 * locking cells, but ant threads will still be able to lock cells independently
 * from each other. Required to implement the Lightswitch pattern.
 */
static struct semaphore grid_available;
/* Number of locked cells and its mutex.
 * Required to implement the Lightswitch pattern, see Downey for details.
 */
static pthread_mutex_t cells_locked_lock = PTHREAD_MUTEX_INITIALIZER;
static int cells_locked;

/* Lock the cell at the given position. If this is the first cell to be locked,
 * also block the main thread from doing a whole grid access (i.e. drawWindow()).
 * Other cells can still be locked independently.
 * We will implement a Lightswitch (see Downey) based pattern in order to
 * achieve this goal.
 */
static void lock_cell(int i, int j)
{
    /* Lightswitch pattern, lock phase */
    pthread_mutex_lock(&cells_locked_lock);
    if (++cells_locked == 1) {
        /* First in locks. This lock will prevent the main thread from
         * locking the grid while an ant thread holds a cell lock and an ant
         * thread from locking a cell while the main thread holds the grid lock.
         */
        semaphore_wait(&grid_available);
    }
    pthread_mutex_unlock(&cells_locked_lock);

    pthread_mutex_lock(&cell_locks[i*GRIDSIZE + j]);
}

/* Unlock the cell at the given position.
 * If this is the last cell to be unlocked, also unblock the main thread
 * from doing a whole grid access (i.e. drawWindow()).
 */
static void unlock_cell(int i, int j)
{
    pthread_mutex_unlock(&cell_locks[i*GRIDSIZE + j]);

    /* Lightswitch pattern, unlock phase */
    pthread_mutex_lock(&cells_locked_lock);
    if (--cells_locked == 0) {
        /* Last out unlocks */
        semaphore_signal(&grid_available);
    }
    pthread_mutex_unlock(&cells_locked_lock);
}

void *ant_main(void *arg)
{
    char state = REPR_ANT;
    int i_pos, j_pos;
    int id = *(int*)arg;
    free(arg);

    /* Find somewhere to sit. */
    while (i_pos = rand() % GRIDSIZE, j_pos = rand() % GRIDSIZE,
            lock_cell(i_pos, j_pos),
            lookCharAt(i_pos, j_pos) != REPR_EMPTY) {
        unlock_cell(i_pos, j_pos);
    }
    putCharTo(i_pos, j_pos, '1');
    unlock_cell(i_pos, j_pos);

    while (pthread_mutex_lock(&running_lock), running) {
        pthread_mutex_unlock(&running_lock);

        /* Check and sleep if necessary. */
        pthread_mutex_lock(&sleeper_lock);
        if (getSleeperN() > id) {
            assert(state == REPR_ANT || state == REPR_FOODANT);
            if (state == REPR_ANT) {
                state = REPR_SLEEPANT;
            } else if (state == REPR_FOODANT) {
                state = REPR_FOODSLEEPANT;
            }
            lock_cell(i_pos, j_pos);
            putCharTo(i_pos, j_pos, state);
            unlock_cell(i_pos, j_pos);
        }
        while (getSleeperN() > id) {
            pthread_cond_wait(&sleeper_cond, &sleeper_lock);
        }
        pthread_mutex_unlock(&sleeper_lock);

        /* After a possible sleep */
        if (state == REPR_SLEEPANT) {
            state = REPR_ANT;
            lock_cell(i_pos, j_pos);
            putCharTo(i_pos, j_pos, state);
            unlock_cell(i_pos, j_pos);
        } else if (state == REPR_FOODSLEEPANT) {
            state = REPR_FOODANT;
            lock_cell(i_pos, j_pos);
            putCharTo(i_pos, j_pos, state);
            unlock_cell(i_pos, j_pos);
        } else {
            assert(state == REPR_ANT);
        }
        assert(state == REPR_ANT || state == REPR_FOODANT);

        if (state == REPR_ANT) {

        } else /* if (state == REPR_FOODANT) */ {

        }

        pthread_mutex_lock(&delay_lock);
        int delay = getDelay();
        pthread_mutex_unlock(&delay_lock);
        usleep(delay*1000 + (rand() % 5000));
    }
    pthread_mutex_unlock(&running_lock);

    return NULL;
}

static void print_usage(char **argv)
{
    fprintf(stderr, "Usage: %s n_ants n_food max_seconds\n", argv[0]);
}

/* Allocate and initialize cell locks and create the ant threads.
 * If we happen to need any more resources for the ant threads in the future,
 * also allocate them here.
 */
static pthread_t *ants_create(int n_ants)
{
    int i;
    pthread_t *threads = malloc(n_ants * sizeof *threads);

    /* Allocate and initialize the cell locks and the global grid lock. */
    cell_locks = malloc(GRIDSIZE * GRIDSIZE * sizeof *cell_locks);
    for (i = 0; i < GRIDSIZE * GRIDSIZE; i++) {
        pthread_mutex_init(&cell_locks[i], NULL);
    }
    semaphore_init(&grid_available, 1);

    /* Create the threads */
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
 * This function stops and joins the threads in the given array,
 * and it frees other resources (if there are any) allocated by ants_create().
 */
static void ants_stop_join(pthread_t *threads, int n_ants)
{
    int i;
    pthread_mutex_lock(&running_lock);
    running = 0;
    pthread_mutex_unlock(&running_lock);
    /* Wake all sleeping threads for them to be able to terminate.
    */
    pthread_mutex_lock(&sleeper_lock);
    setSleeperN(0);
    pthread_cond_broadcast(&sleeper_cond);
    pthread_mutex_unlock(&sleeper_lock);
    for (i = 0; i < n_ants; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("ants_stop_join(): pthread_join()");
            exit(EXIT_FAILURE);
        }
    }
    free(threads);
    free(cell_locks);
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

    startCurses();
    pthread_t *ant_threads = ants_create(n_ants);
    /* Ants are running. From now on, the grid must be protected.
     */

    time_t start_time;
    time_t curr_time;
    for (start_time = time(NULL), curr_time = time(NULL);
            difftime(curr_time, start_time) < max_seconds;
            curr_time = time(NULL)) {

        semaphore_wait(&grid_available);
        drawWindow();
        semaphore_signal(&grid_available);

        int c = getch();
        if (c == 'q' || c == ESC) {
            break;
        } else if (c == '+') {
            pthread_mutex_lock(&delay_lock);
            setDelay(getDelay() + 10);
            pthread_mutex_unlock(&delay_lock);
        }
        if (c == '-') {
            pthread_mutex_lock(&delay_lock);
            setDelay(getDelay() - 10);
            pthread_mutex_unlock(&delay_lock);
        }
        if (c == '*') {
            pthread_mutex_lock(&sleeper_lock);
            setSleeperN(getSleeperN() + 1);
            pthread_mutex_unlock(&sleeper_lock);
        }
        if (c == '/') {
            pthread_mutex_lock(&sleeper_lock);
            setSleeperN(getSleeperN() - 1);
            pthread_cond_broadcast(&sleeper_cond);
            pthread_mutex_unlock(&sleeper_lock);
        }

        usleep(DRAWDELAY);
    }

    ants_stop_join(ant_threads, n_ants);
    endCurses();
    return 0;
}
