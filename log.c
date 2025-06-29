#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/time.h>
#include <unistd.h>
#include "segel.h"

#include "reader_writer.c"
#include "log.h"
#define INITIAL_CAPACITY 1024

// Opaque struct definition
struct Server_Log {
    char* buffer;
    int length;
    int capacity;
};

// Creates a new server log instance (stub)
server_log create_log() {
    writers_waiting = 0;
    current_readers = 0;
    current_writers = 0;
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&accept_write, NULL);
    pthread_cond_init(&accept_read, NULL);
    server_log log = malloc(sizeof(struct Server_Log));
    if (!log) return NULL;
    log->buffer = malloc(INITIAL_CAPACITY);
    if (!log->buffer) {
        free(log);
        return NULL;
    }
    log->buffer[0] = '\0';
    log->length = 0;
    log->capacity = INITIAL_CAPACITY;
    return log;
}


// Destroys and frees the log (stub)
void destroy_log(server_log log) {
    if (log) {
        free(log->buffer);
        free(log);
    }
}

int get_log(server_log log, char** dst) {
    if (!log || !dst) return 0;
    readers_lock();
    *dst = malloc(log->length + 1);
    if (!*dst) {
        readers_unlock();
        return 0;
    }
    memcpy(*dst, log->buffer, log->length);
    (*dst)[log->length] = '\0';
    int result_len = log->length;
    readers_unlock();
    return result_len;
}

// Appends a new entry to the log (no-op stub)
void add_to_log(server_log log, const char* data, int data_len) {
    if (!log || !data || data_len <= 0) return;
    writers_lock();

    // usleep(200000);
    if (log->length + data_len + 1 > log->capacity) {
        int new_capacity = (log->length + data_len + 1) * 2;
        char* new_buf = realloc(log->buffer, new_capacity);
        if (!new_buf) {
            writers_unlock();
            return;
        }
        log->buffer = new_buf;
        log->capacity = new_capacity;
    }

    memcpy(log->buffer + log->length, data, data_len);
    log->length += data_len;
    // Add newline after the copied data
    log->buffer[log->length++] = '\n';
    log->buffer[log->length] = '\0';
    writers_unlock();
}
