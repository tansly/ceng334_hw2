#include "sem.h"

#include <pthread.h>

struct semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int value;
    int wakeups;
};

void semaphore_init(struct semaphore *sem)
{
    (void)sem;
}

void semaphore_wait(struct semaphore *sem)
{
    (void)sem;
}

int semaphore_trywait(struct semaphore *sem)
{
    (void)sem;
    return 0;
}

void semaphore_signal(struct semaphore *sem)
{
    (void)sem;
}
