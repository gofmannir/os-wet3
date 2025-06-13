#include "reader_writer.h"

void readers_lock() {
    pthread_mutex_lock(&lock);

    while (writers_waiting > 0 || current_writers > 0) {
        pthread_cond_wait(&accept_read, &lock);
    }

    current_readers++;

    pthread_mutex_unlock(&lock);
}

void readers_unlock() {
    pthread_mutex_lock(&lock);

    current_readers--;

    if (current_readers == 0) {
        pthread_cond_signal(&accept_write);
    }

    pthread_mutex_unlock(&lock);
}

void writers_lock() {
    pthread_mutex_lock(&lock);

    writers_waiting++;

    while (current_readers > 0 || current_writers > 0) {
        pthread_cond_wait(&accept_write, &lock);
    }

    writers_waiting--;
    current_writers++;

    pthread_mutex_unlock(&lock);
}

void writers_unlock() {
    pthread_mutex_lock(&lock);

    current_writers--;

    if (writers_waiting > 0) {
        pthread_cond_signal(&accept_write);
    } else {
        pthread_cond_broadcast(&accept_read);
    }

    pthread_mutex_unlock(&lock);
}