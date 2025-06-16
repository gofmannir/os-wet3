#include "segel.h"
#include "request.h"
#include "log.h"
#include <pthread.h>
#include <semaphore.h>

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

typedef struct request_t {
    // holds the information to be served by the thread
    int connfd;
    struct timeval arrival_time;
} request_t;

request_t dequeue_request();
void enqueue_request(request_t req);

struct request_t* request_queue = NULL; // This should be a synchronized queue
pthread_mutex_t queue_mutex;

// create a queue to hold requests
int QUEUE_MAX_SIZE;
sem_t queue_size;
sem_t active_requests_sem; // semaphore to control
// for fifo handling
int queue_front = 0;
int queue_rear = 0;

server_log log_requests;

pthread_t* thread_pool;
struct Threads_stats *thread_stats_array;

void* worker_thread(void *arg) {
    // Worker thread function to process requests from the queue
    // This is a placeholder; you need to implement the logic to
    // dequeue requests and handle them.

    struct Threads_stats *t = (struct Threads_stats *)arg;
    while (1) {
        request_t req = dequeue_request();
        struct timeval dispatch_time;
        gettimeofday(&dispatch_time, NULL);
        struct timeval diff;
        timersub(&dispatch_time, &req.arrival_time, &diff);
        requestHandle(req.connfd, req.arrival_time, diff, t, log_requests);
        Close(req.connfd);

        sem_post(&queue_size); // signal that a request has been processed
    }
}

void enqueue_request(request_t req) {
    pthread_mutex_lock(&queue_mutex);

    // append to the queue
    request_queue[queue_rear] = req;
    queue_rear = (queue_rear + 1) % QUEUE_MAX_SIZE;

    pthread_mutex_unlock(&queue_mutex);
    sem_post(&active_requests_sem); // signal that an active request is available
}


request_t dequeue_request() {

    sem_wait(&active_requests_sem); // wait d for an active request to be available

    pthread_mutex_lock(&queue_mutex);

    request_t req = request_queue[queue_front];
    queue_front = (queue_front + 1) % QUEUE_MAX_SIZE;

    pthread_mutex_unlock(&queue_mutex);

    return req;
}


// Signal handling to release memory
void signal_handler(int signum) {
    printf("\nReceived SIGINT, cleaning up...\n");
    destroy_log(log_requests);
    free(request_queue);
    free(thread_pool);
    free(thread_stats_array);

    pthread_mutex_destroy(&queue_mutex);

    sem_destroy(&queue_size);
    sem_destroy(&active_requests_sem);

    exit(0);
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, port, threads, clientlen;
    struct sockaddr_in clientaddr;
    getargs(&port, &threads, &QUEUE_MAX_SIZE, argc, argv);

    sem_init(&queue_size, 0, QUEUE_MAX_SIZE);
    sem_init(&active_requests_sem, 0, 0);
    thread_pool = malloc(sizeof(pthread_t) * threads);

    thread_stats_array = malloc(sizeof(struct Threads_stats) * threads);

    request_queue = malloc(sizeof(struct request_t) * QUEUE_MAX_SIZE);

    for (int i = 0; i < threads; i++) {
        thread_stats_array[i].id = i+1;
        thread_stats_array[i].stat_req = 0;
        thread_stats_array[i].dynm_req = 0;
        thread_stats_array[i].post_req = 0;
        thread_stats_array[i].total_req = 0;
        pthread_create(&thread_pool[i], NULL, worker_thread, &thread_stats_array[i]);
    }


    log_requests = create_log();
    pthread_mutex_init(&queue_mutex, NULL);

    listenfd = Open_listenfd(port);
    // Register signal handler for SIGINT
    signal(SIGINT, signal_handler);
    while (1) {
        clientlen = sizeof(clientaddr);
        
        sem_wait(&queue_size); 

        connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

        struct timeval arrival_time;
        gettimeofday(&arrival_time, NULL);
        request_t req;
        req.connfd = connfd;
        req.arrival_time = arrival_time;

        enqueue_request(req);
        
    }
    
}   
