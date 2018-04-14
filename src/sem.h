#ifndef SEM_H
#define SEM_H

#include <pthread.h>

struct semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int value;
    int wakeups;
};

void semaphore_init(struct semaphore *sem, int value);

void semaphore_wait(struct semaphore *sem);

int semaphore_trywait(struct semaphore *sem);

void semaphore_signal(struct semaphore *sem);

#endif
