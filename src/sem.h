#ifndef SEM_H
#define SEM_H

struct semaphore;

void semaphore_init(struct semaphore *sem);

void semaphore_wait(struct semaphore *sem);

int semaphore_trywait(struct semaphore *sem);

void semaphore_signal(struct semaphore *sem);

#endif
