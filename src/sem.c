#include "sem.h"

#include <pthread.h>

void semaphore_init(struct semaphore *sem, int value)
{
    pthread_mutex_init(&sem->mutex, NULL);
    pthread_cond_init(&sem->cond, NULL);
    sem->value = value;
    sem->wakeups = 0;
}

void semaphore_wait(struct semaphore *sem)
{
    pthread_mutex_lock(&sem->mutex);
    if (--sem->value < 0) {
        do {
            pthread_cond_wait(&sem->cond, &sem->mutex);
        } while (sem->wakeups < 1);
        --sem->wakeups;
    }
    pthread_mutex_unlock(&sem->mutex);
}

int semaphore_trywait(struct semaphore *sem)
{
    int granted;
    pthread_mutex_lock(&sem->mutex);
    if (sem->value > 0) {
        --sem->value;
        granted = 1;
    } else {
        granted = 0;
    }
    pthread_mutex_unlock(&sem->mutex);
    return granted;
}

void semaphore_signal(struct semaphore *sem)
{
    pthread_mutex_lock(&sem->mutex);
    if (++sem->value <= 0) {
        ++sem->wakeups;
        pthread_cond_signal(&sem->cond);
    }
    pthread_mutex_unlock(&sem->mutex);
}
