#include "util.h"

#include <assert.h>
#include <curses.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define REPR_EMPTY '-'
#define REPR_FOOD 'o'
#define REPR_ANT '1'
#define REPR_FOODANT 'P'
#define REPR_SLEEPANT 'S'
#define REPR_SLEEPFOODANT '$'

enum ant_state {
    STATE_ANT,
    STATE_FOODANT,
    STATE_TIREDANT,
    STATE_SLEEPANT,
    STATE_SLEEPFOODANT,
    STATE_SLEEPTIREDANT
};

struct coordinate {
    int x;
    int y;
};

/* Mutex protecting the number of sleepers,
 * i.e. the functions getSleeperN() and setSleeperN().
 */
static pthread_mutex_t sleeper_lock = PTHREAD_MUTEX_INITIALIZER;
/* Condition variable for the sleepers.
 * It must be broadcast (not signal) since there may be several threads waiting
 * on it but only a specific one, the one with the proper id, can continue.
 */
static pthread_cond_t sleeper_cond = PTHREAD_COND_INITIALIZER;
/* Mutex protecting the delay value,
 * i.e the functions getDelay() and setDelay()
 */
static pthread_mutex_t delay_lock = PTHREAD_MUTEX_INITIALIZER;
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
static sem_t grid_available;
/* Number of locked cells and its mutex.
 * Required to implement the Lightswitch pattern, see Downey for details.
 */
static pthread_mutex_t cells_locked_lock = PTHREAD_MUTEX_INITIALIZER;
static int cells_locked;
/* Turnstile to prevent starvation of the main thread
 */
static sem_t turnstile;

static int sem_wait_nointr(sem_t *sem)
{
    int ret;
    while ((ret = sem_wait(sem)) == -1 && errno == EINTR)
        ;
    return ret;
}

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
        sem_wait_nointr(&grid_available);
    }
    pthread_mutex_unlock(&cells_locked_lock);

    pthread_mutex_lock(&cell_locks[i*GRIDSIZE + j]);
}

static int trylock_cell(int i, int j)
{
    /* Lightswitch pattern, lock phase */
    pthread_mutex_lock(&cells_locked_lock);
    if (++cells_locked == 1) {
        /* First in locks. This lock will prevent the main thread from
         * locking the grid while an ant thread holds a cell lock and an ant
         * thread from locking a cell while the main thread holds the grid lock.
         */
        sem_wait_nointr(&grid_available);
    }
    pthread_mutex_unlock(&cells_locked_lock);

    if (pthread_mutex_trylock(&cell_locks[i*GRIDSIZE + j]) != 0) {
        pthread_mutex_lock(&cells_locked_lock);
        if (--cells_locked == 0) {
            sem_post(&grid_available);
        }
        pthread_mutex_unlock(&cells_locked_lock);
        return 0;
    } else {
        return 1;
    }
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
        sem_post(&grid_available);
    }
    pthread_mutex_unlock(&cells_locked_lock);
}

static char state_to_repr(enum ant_state state)
{
    switch (state) {
        case STATE_ANT:
        case STATE_TIREDANT:
            return REPR_ANT;
        case STATE_FOODANT:
            return REPR_FOODANT;
        case STATE_SLEEPANT:
        case STATE_SLEEPTIREDANT:
            return REPR_SLEEPANT;
        case STATE_SLEEPFOODANT:
            return REPR_SLEEPFOODANT;
        default:
            assert(0);
            return '\0';
    }
}

static int state_is_awake(enum ant_state state)
{
    return state == STATE_ANT || state == STATE_FOODANT ||
                    state == STATE_TIREDANT;
}

static int state_is_asleep(enum ant_state state)
{
    return state == STATE_SLEEPANT || state == STATE_SLEEPFOODANT ||
                    state == STATE_SLEEPTIREDANT;
}

/* Returns the awake counterpart of state.
 * Behaviour is undefined if state is not a valid sleeping state,
 * i.e. state_is_asleep(state) == false.
 */
static enum ant_state state_wake(enum ant_state state)
{
    switch (state) {
        case STATE_SLEEPANT:
            return STATE_ANT;
        case STATE_SLEEPFOODANT:
            return STATE_FOODANT;
        case STATE_SLEEPTIREDANT:
            return STATE_TIREDANT;
        default:
            assert(state_is_asleep(state));
            break;
    }

    assert(0);
    return STATE_ANT;
}

/* Returns the sleeping counterpart of state.
 * Behaviour is undefined if state is not a valid awake state,
 * i.e. state_is_awake(state) is false.
 */
static enum ant_state state_sleep(enum ant_state state)
{
    switch (state) {
        case STATE_ANT:
            return STATE_SLEEPANT;
        case STATE_FOODANT:
            return STATE_SLEEPFOODANT;
        case STATE_TIREDANT:
            return STATE_SLEEPTIREDANT;
        default:
            assert(state_is_awake(state));
            break;
    }

    assert(0);
    return STATE_SLEEPANT;
}

/* Search for the needle in the given array of coordinates. Given array may have
 * invalidated entries, with the coordinates set to -1; however, it must contain
 * at least valid_n valid entries. Given coordinates are locked and checked for
 * a match in the given order until the needle is found or valid_n number of
 * entries are checked. On the first match found, true is returned and the matching
 * coordinate is "moved" into *found_pos (the entry is copied into *found_pos
 * and the original one in check_pos is invalidated).
 * The lock for the matching coordinate is kept locked on return.
 * Only the lock for the matching coordinate (one returned in found_pos)
 * is held on return; if no match is found, no locks are kept held, the value of
 * found_pos is undefined and the array is not modified.
 * The calling thread must not be holding any locks for the given coordinates.
 */
static int find_and_lock(struct coordinate *check_pos, int valid_n, char needle,
        struct coordinate *found_pos)
{
    int checked = 0;
    while (checked < valid_n) {
        if (check_pos->x != -1) {
            lock_cell(check_pos->x, check_pos->y);
            if (lookCharAt(check_pos->x, check_pos->y) == needle) {
                *found_pos = *check_pos;
                check_pos->x = check_pos->y = -1;
                return 1;
            }
            unlock_cell(check_pos->x, check_pos->y);

            checked++;
        }
        check_pos++;
    }
    return 0;
}

static int find_and_trylock(struct coordinate *check_pos, int valid_n, char needle,
        struct coordinate *found_pos)
{
    int checked = 0;
    while (checked < valid_n) {
        if (check_pos->x != -1) {
            checked++;
            if (!trylock_cell(check_pos->x, check_pos->y)) {
                continue;
            }
            if (lookCharAt(check_pos->x, check_pos->y) == needle) {
                *found_pos = *check_pos;
                check_pos->x = check_pos->y = -1;
                return 1;
            }
            unlock_cell(check_pos->x, check_pos->y);
        }
        check_pos++;
    }
    return 0;
}

static void shuffle_array(struct coordinate *array, int n)
{
    int i;
    for (i = n - 1; i >= 1; i--) {
        int j = rand() % (i + 1);
        struct coordinate tmp = array[j];
        array[j] = array[i];
        array[i] = tmp;
    }
}

/* Fills the array with neighbour coordinates,
 * returns the number of valid coordinates.
 * Array length must be at least 8.
 */
static int fill_neighbours(struct coordinate pos, struct coordinate *neighbours)
{
    int valid = 0;
    int x, y;
    for (x = -1; x <= 1; x++) {
        for (y = -1; y <= 1; y++) {
            if (x == 0 && y == 0) {
                continue;
            }

            struct coordinate check = { pos.x + x, pos.y + y };
            if (check.x < 0 || check.y < 0 || check.x >= GRIDSIZE ||
                    check.y >= GRIDSIZE) {
                struct coordinate invalid = { -1, -1 };
                *neighbours++ = invalid;
            } else {
                *neighbours++ = check;
                valid++;
            }
        }
    }
    return valid;
}

void *ant_main(void *arg)
{
    enum ant_state state = STATE_ANT;
    struct coordinate curr_pos;
    struct coordinate neighbours_pos[8]; /* 8 neighbours */
    int id = *(int*)arg;
    free(arg);

    /* Find somewhere to sit. */
    while (curr_pos.x = rand() % GRIDSIZE, curr_pos.y = rand() % GRIDSIZE,
            lock_cell(curr_pos.x, curr_pos.y),
            lookCharAt(curr_pos.x, curr_pos.y) != REPR_EMPTY) {
        unlock_cell(curr_pos.x, curr_pos.y);
    }
    putCharTo(curr_pos.x, curr_pos.y, state_to_repr(state));
    unlock_cell(curr_pos.x, curr_pos.y);

    while (pthread_mutex_lock(&running_lock), running) {
        pthread_mutex_unlock(&running_lock);

        /* Check and sleep if necessary. */
        assert(state_is_awake(state));
        pthread_mutex_lock(&sleeper_lock);
        if (getSleeperN() > id) {
            state = state_sleep(state);
            lock_cell(curr_pos.x, curr_pos.y);
            putCharTo(curr_pos.x, curr_pos.y, state_to_repr(state));
            unlock_cell(curr_pos.x, curr_pos.y);
        }
        while (getSleeperN() > id) {
            pthread_cond_wait(&sleeper_cond, &sleeper_lock);
        }
        pthread_mutex_unlock(&sleeper_lock);

        /* After a possible sleep */
        if (state_is_asleep(state)) {
            state = state_wake(state);
            lock_cell(curr_pos.x, curr_pos.y);
            putCharTo(curr_pos.x, curr_pos.y, state_to_repr(state));
            unlock_cell(curr_pos.x, curr_pos.y);
        }
        assert(state_is_awake(state));

        /* TODO: Figure out if the Helgrind lock order error is a serious matter.
         * Read the manual:
         * (http://valgrind.org/docs/manual/hg-manual.html#hg-manual.lock-orders).
         * I guess we get those errors because the ants move around and they can
         * and *will* take locks in random orders all the time. If the error
         * is caused by not consistently acquiring locks in the same order as the first
         * time they are acquired, the errors are benign; our logic requires that,
         * and we (hopefully) are in control. I suppose this will not be the
         * source of a deadlock or else, so suppress the errors.
         */
        int valid_neighbours = fill_neighbours(curr_pos, neighbours_pos);
        shuffle_array(neighbours_pos, ARRAY_SIZE(neighbours_pos));
        sem_wait_nointr(&turnstile);
        sem_post(&turnstile);
        if (state == STATE_ANT) {
            struct coordinate found_pos;
            /* Check da hood for da food */
            if (find_and_lock(neighbours_pos, valid_neighbours, REPR_FOOD, &found_pos)) {
                lock_cell(curr_pos.x, curr_pos.y);
                putCharTo(curr_pos.x, curr_pos.y, REPR_EMPTY);
                unlock_cell(curr_pos.x, curr_pos.y);
                state = STATE_FOODANT;
                putCharTo(found_pos.x, found_pos.y, state_to_repr(state));
                unlock_cell(found_pos.x, found_pos.y);
                curr_pos = found_pos;
            } else if (find_and_lock(neighbours_pos, valid_neighbours, REPR_EMPTY, &found_pos)) {
                lock_cell(curr_pos.x, curr_pos.y);
                putCharTo(curr_pos.x, curr_pos.y, REPR_EMPTY);
                unlock_cell(curr_pos.x, curr_pos.y);
                putCharTo(found_pos.x, found_pos.y, state_to_repr(state));
                unlock_cell(found_pos.x, found_pos.y);
                curr_pos = found_pos;
            } /* else no food and no empty positions, do nothing */
        } else if (state == STATE_FOODANT) {
            struct coordinate found_food_pos;
            struct coordinate found_empty_pos;
            /* Check da hood for da food */
            if (find_and_lock(neighbours_pos, valid_neighbours, REPR_FOOD, &found_food_pos)) {
                /* XXX: Fixed the deadlock by attacking the no-preemption condition.
                 * Is there a better way to fix it, and does it even work
                 * properly now?
                 */
                if (find_and_trylock(neighbours_pos, valid_neighbours - 1, REPR_EMPTY, &found_empty_pos)) {
                    lock_cell(curr_pos.x, curr_pos.y);
                    putCharTo(curr_pos.x, curr_pos.y, REPR_FOOD);
                    unlock_cell(curr_pos.x, curr_pos.y);
                    state = STATE_TIREDANT;
                    putCharTo(found_empty_pos.x, found_empty_pos.y, state_to_repr(state));
                    unlock_cell(found_empty_pos.x, found_empty_pos.y);
                    curr_pos = found_empty_pos;
                }
                unlock_cell(found_food_pos.x, found_food_pos.y);
            } else if (find_and_lock(neighbours_pos, valid_neighbours, REPR_EMPTY, &found_empty_pos)) {
                lock_cell(curr_pos.x, curr_pos.y);
                putCharTo(curr_pos.x, curr_pos.y, REPR_EMPTY);
                unlock_cell(curr_pos.x, curr_pos.y);
                putCharTo(found_empty_pos.x, found_empty_pos.y, state_to_repr(state));
                unlock_cell(found_empty_pos.x, found_empty_pos.y);
                curr_pos = found_empty_pos;
            }
        } else /* if (state == STATE_TIREDANT) */ {
            struct coordinate found_pos;
            if (find_and_lock(neighbours_pos, valid_neighbours, REPR_EMPTY, &found_pos)) {
                lock_cell(curr_pos.x, curr_pos.y);
                putCharTo(curr_pos.x, curr_pos.y, REPR_EMPTY);
                unlock_cell(curr_pos.x, curr_pos.y);
                state = STATE_ANT;
                putCharTo(found_pos.x, found_pos.y, state_to_repr(state));
                unlock_cell(found_pos.x, found_pos.y);
                curr_pos = found_pos;
            }
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

    /* Allocate and initialize the cell locks and semaphores used.*/
    cell_locks = malloc(GRIDSIZE * GRIDSIZE * sizeof *cell_locks);
    for (i = 0; i < GRIDSIZE * GRIDSIZE; i++) {
        pthread_mutex_init(&cell_locks[i], NULL);
    }
    if (sem_init(&grid_available, 0, 1) != 0) {
        perror("ants_create(): sem_init()");
        exit(EXIT_FAILURE);
    }
    if (sem_init(&turnstile, 0, 1) != 0) {
        perror("ants_create(): sem_init()");
        exit(EXIT_FAILURE);
    }

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
    sem_destroy(&grid_available);
    sem_destroy(&turnstile);
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

        sem_wait_nointr(&turnstile);
        sem_wait_nointr(&grid_available);
        drawWindow();
        sem_post(&turnstile);
        sem_post(&grid_available);

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
