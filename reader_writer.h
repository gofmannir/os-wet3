#pragma once
#include <pthread.h>

int writers_waiting;
int current_readers;
int current_writers;
pthread_mutex_t lock;
pthread_cond_t accept_write;
pthread_cond_t accept_read;

void readers_lock();
void readers_unlock();
void writers_lock();
void writers_unlock();