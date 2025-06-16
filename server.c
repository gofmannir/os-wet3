#include "segel.h"
#include "request.h"
#include "log.h"
#include <pthread.h>

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// Parses command-line arguments
void getargs(int *port, int *threads, int *queue_size, int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <port> <threads> <queue_size>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);
}
// TODO: HW3 — Initialize thread pool and request queue
// This server currently handles all requests in the main thread.
// You must implement a thread pool (fixed number of worker threads)
// that process requests from a synchronized queue.

typedef struct {
    // holds the information to be served by the thread
    int connfd;
    struct timeval arrival_time;
} request_t;

request_t dequeue_request();
void enqueue_request(request_t req);

request_t* request_queue; // This should be a synchronized queue
pthread_mutex_t queue_mutex;
pthread_cond_t queue_not_full;
pthread_cond_t queue_not_empty;

// create a queue to hold requests
int QUEUE_MAX_SIZE;
int queue_current_size = 0;
// for fifo handling
int queue_front = 0;
int queue_rear = 0;

int active_requests = 0;

server_log log_requests;


void* worker_thread(void *arg) {
    // Worker thread function to process requests from the queue
    // This is a placeholder; you need to implement the logic to
    // dequeue requests and handle them.

    struct Threads_stats *t = (struct Threads_stats *)arg;
    while (1) {
        request_t req = dequeue_request();
        struct timeval dispatch_time;
        gettimeofday(&dispatch_time, NULL);
        requestHandle(req.connfd, req.arrival_time, dispatch_time, t, log_requests);
        Close(req.connfd);

        // lock again for reducing active requests
        pthread_mutex_lock(&queue_mutex);
        active_requests--;
        pthread_cond_signal(&queue_not_full);
        pthread_mutex_unlock(&queue_mutex);
    }
}

void enqueue_request(request_t req) {
    // when appending we should wait if the queue is full
    // threads are editting the queue variables thus, we should protext with lock
    pthread_mutex_lock(&queue_mutex);

    // wait for a signal if the queue is full
    while (QUEUE_MAX_SIZE == queue_current_size + active_requests) { // while the queue is full we will wait
        pthread_cond_wait(&queue_not_full, &queue_mutex);
        // who is sending the signal? - a thread that finishes handling a request
    }

    // append to the queue
    request_queue[queue_rear] = req;
    queue_rear = (queue_rear + 1) % QUEUE_MAX_SIZE;
    queue_current_size++;
    
    // if this is the furst request, we should awake waiting threads
    // we cand use busy waiting, thus, we will signal the condition variable
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_mutex);
}


request_t dequeue_request() {
    pthread_mutex_lock(&queue_mutex);

    while (queue_current_size == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_mutex);
    }

    request_t req = request_queue[queue_front];
    queue_front = (queue_front + 1) % QUEUE_MAX_SIZE;
    queue_current_size--; // TODO: check to reduce only when request is finished
    active_requests++; // maybe redundent

    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_mutex);

    return req;
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, threads, clientlen;
    struct sockaddr_in clientaddr;
    getargs(&port, &threads, &QUEUE_MAX_SIZE, argc, argv);

    pthread_t* thread_pool = malloc(sizeof(pthread_t) * threads);
    struct Threads_stats *thread_stats_array = malloc(sizeof(struct Threads_stats) * threads);
    for (int i = 1; i <= threads; i++) {
        thread_stats_array[i].id = i;
        thread_stats_array[i].stat_req = 0;
        thread_stats_array[i].dynm_req = 0;
        thread_stats_array[i].post_req = 0;
        thread_stats_array[i].total_req = 0;
        pthread_create(&thread_pool[i], NULL, worker_thread, &thread_stats_array[i]);
    }

    log_requests = create_log();

    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_not_full, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    request_queue = malloc(sizeof(request_t) * QUEUE_MAX_SIZE);


    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

        // TODO: HW3 — Record the request arrival time here

        // DEMO PURPOSE ONLY:
        // This is a dummy request handler that immediately processes
        // the request in the main thread without concurrency.
        // Replace this with logic to enqueue the connection and let
        // a worker thread process it from the queue.

        // threads_stats t = malloc(sizeof(struct Threads_stats));
        // t->id = 0;             // Thread ID (placeholder)
        // t->stat_req = 0;       // Static request count
        // t->dynm_req = 0;       // Dynamic request count
        // t->total_req = 0;      // Total request count

        // struct timeval arrival, dispatch;
        // arrival.tv_sec = 0; arrival.tv_usec = 0;   // DEMO: dummy timestamps
        // dispatch.tv_sec = 0; dispatch.tv_usec = 0; // DEMO: dummy timestamps
        // gettimeofday(&arrival, NULL);

        struct timeval arrival_time;
        gettimeofday(&arrival_time, NULL);
        request_t req;
        req.connfd = connfd;
        req.arrival_time = arrival_time;

        printf("New connection: %d\n", connfd);
        enqueue_request(req);
        // Call the request handler (immediate in main thread — DEMO ONLY)
        // requestHandle(connfd, arrival, dispatch, t, log);

        /**
         * the thread will handle the request
         * The thread will close the connfd
         */

        // free(t); // Cleanup
        // Close(connfd); // Close the connection
    }

    // Clean up the server log before exiting
    destroy_log(log_requests);

    free(request_queue);
    free(thread_pool);
    
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&queue_not_empty);
    pthread_cond_destroy(&queue_not_full);
    // TODO: HW3 — Add cleanup code for thread pool and queue
}   
