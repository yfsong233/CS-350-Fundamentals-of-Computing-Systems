/*******************************************************************************
* Simple FIFO Order Server Implementation
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch.
*
* Usage:
*     <build directory>/server <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. For debugging or more details, refer
*     to the accompanying documentation and logs.
*
*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/sched.h>
#include <signal.h>
#include <inttypes.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include <sys/select.h>
#include "common.h"
#include <fcntl.h> // For fcntl
#include <unistd.h> // For close
#include <errno.h>

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

/* Max number of requests that can be queued */
#define QUEUE_SIZE 500

/* a FIFO data structure to hold requests */
struct queue {
	struct request arr[QUEUE_SIZE];
    int front;
    int rear;
    int count;  // current num of reqs in the queue
};

/* holds info a child thread should know */
struct worker_params {
	struct queue *q; // pointer to the shared queue b/w parent and child
    int conn_socket;  // connection socket to communicate with the client
	struct timespec receipt_time;  // when parent process received request
};

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request to_add, struct queue * the_queue)
{
	int retval = 0;
	sem_wait(queue_mutex);

	if (the_queue->count == QUEUE_SIZE) {
		retval = -1;  // queue is full
		goto exit;
	}
	the_queue->rear = (the_queue->rear + 1) % QUEUE_SIZE;
	the_queue->arr[the_queue->rear] = to_add;
	the_queue->count = the_queue->count + 1;
	
	exit:
	sem_post(queue_mutex);
	sem_post(queue_notify);
	return retval;
}


/* Add a new request <request> to the shared queue <the_queue> */
struct request get_from_queue(struct queue *the_queue)
{
	struct request retval;  // hold the req retrieved from the front of queue
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	
	if (the_queue->count == 0) {
		// this should not get triggered due to sem_wait(queue_notify)
		perror("attempting to get elem from an empty queue.");
		goto exit;
	}

	retval = the_queue->arr[the_queue->front];
	the_queue->front = (the_queue->front + 1) % QUEUE_SIZE;
	the_queue->count = the_queue->count - 1;

	exit:
	sem_post(queue_mutex);
	return retval;
}


/* Implement this method to correctly dump the status of the queue
 * following the format Q:[R<request ID>,R<request ID>,...] */
void dump_queue_status(struct queue *the_queue)
{
	int i;
	sem_wait(queue_mutex);
	int cnt = the_queue->count;
	int frnt = the_queue->front;
	printf("Q:[");
	for (i = 0; i < cnt; i++) {
		printf("R%llu", the_queue->arr[(frnt + i) % QUEUE_SIZE].req_id); 
		if (i < cnt - 1) {  // if not the last req in queue
			printf(",");
		}
	}
	printf("]\n");

	sem_post(queue_mutex);
}


/* Main logic of the worker thread */
/* IMPLEMENT HERE THE MAIN FUNCTION OF THE WORKER */

int worker_main(void* arg) {
    struct worker_params* wrk = (struct worker_params*)arg;
	struct timespec receipt_time, start_time, completion_time;
	wrk->receipt_time = receipt_time;
	int conn_socket = wrk->conn_socket;

    while(1) {
        struct request current_req = get_from_queue(wrk->q); // dequeue a request from the shared queue.
        clock_gettime(CLOCK_MONOTONIC, &start_time);  // timestamp marking start of processing

		get_elapsed_busywait(current_req.req_length.tv_sec, current_req.req_length.tv_nsec); // busywait (request processing)

		// send a response to client
		ssize_t bytes_sent = send(conn_socket, &current_req.req_id, sizeof(current_req.req_id), 0);
		if (bytes_sent <= 0) {
            perror("failed to send response");
        }
;
		clock_gettime(CLOCK_MONOTONIC, &completion_time);  // timestamp marking end of processing
		
		printf("R%lu:%f,%f,%f,%f,%f\n",
            current_req.req_id,
            TSPEC_TO_DOUBLE(current_req.req_timestamp), // where do the time that request was sent by client come from? 
            TSPEC_TO_DOUBLE(current_req.req_length),  
            TSPEC_TO_DOUBLE(wrk->receipt_time),  // when parent process received request
            TSPEC_TO_DOUBLE(start_time),  // when worker dequeues the request and starts processing
            TSPEC_TO_DOUBLE(completion_time)
        );

		dump_queue_status(wrk->q); // dump current queue info
        
    }

	free(wrk);
    return 0;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket)
{
	/* The connection with the client is alive here. Let's
	 * initialize the shared queue. */
	size_t in_bytes;

    struct request *req = (struct request *)malloc(sizeof(struct request));  // allocate memory for incoming request
    struct queue *the_queue = (struct queue *)malloc(sizeof(struct queue));  // create the shared queue
    the_queue->front = 0;  // initialize queue
    the_queue->rear = QUEUE_SIZE - 1;
    the_queue->count = 0;
    struct timespec receipt_time;  // time marking when parent receives package

	/* Queue ready to go here. Let's start the worker thread. */
    struct worker_params* wrk = malloc(sizeof(struct worker_params)); // wrk holds what to tell/share with the child
    if (!wrk) {
        perror("failed to allocate memoty for worker thread");
        return;
    }
    wrk->q = the_queue;  // initialize wrk
    wrk->conn_socket = conn_socket;
    void *worker_stack = malloc(STACK_SIZE); 
	if (worker_stack == NULL) {
        perror("failed to allocate stack");
        free(the_queue);
        return;
    }
	// use clone syscall to create a worker thread
	int flags = CLONE_THREAD | CLONE_VM | CLONE_SIGHAND | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM;
    pid_t worker = clone(worker_main, worker_stack + STACK_SIZE, flags, wrk);
	if (worker == -1) {
        perror("failed to create worker thread");
        free(worker_stack);
        free(the_queue);
        return;
    }

	/* We are ready to proceed with the rest of the request
	 * handling logic. */
	do {
		// receive request from client
        in_bytes = recv(conn_socket, req, sizeof(struct request), 0);
        
        struct timespec received_time;
        clock_gettime(CLOCK_MONOTONIC, &received_time);
		wrk->receipt_time = received_time;  
		// mark time received and tell child (later for dumping queue status)

		// add the received req to queue
        struct request req_to_add = *req; 
        add_to_queue(req_to_add, the_queue); 
        
    } while (in_bytes > 0);

	/* PERFORM ORDERLY DEALLOCATION AND OUTRO HERE */
	free(req);
	free(wrk);
    free(worker_stack);
    free(the_queue);
	close(conn_socket);
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;

	/* Get port to bind our socket to */
	if (argc > 1) {
		socket_port = strtol(argv[1], NULL, 10);
		printf("INFO: setting server port as: %d\n", socket_port);
	} else {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

	/* Now onward to create the right type of socket */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);

	if (sockfd < 0) {
		ERROR_INFO();
		perror("Unable to create socket");
		return EXIT_FAILURE;
	}

	/* Before moving forward, set socket to reuse address */
	optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

	/* Convert INADDR_ANY into network byte order */
	any_address.s_addr = htonl(INADDR_ANY);

	/* Time to bind the socket to the right port  */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(socket_port);
	addr.sin_addr = any_address;

	/* Attempt to bind the socket with the given parameters */
	retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to bind socket");
		return EXIT_FAILURE;
	}

	/* Let us now proceed to set the server to listen on the selected port */
	retval = listen(sockfd, BACKLOG_COUNT);

	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to listen on socket");
		return EXIT_FAILURE;
	}

	/* Ready to accept connections! */
	printf("INFO: Waiting for incoming connection...\n");
	client_len = sizeof(struct sockaddr_in);
	accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);

	if (accepted == -1) {
		ERROR_INFO();
		perror("Unable to accept connections");
		return EXIT_FAILURE;
	}

	/* Initialize queue protection variables. DO NOT TOUCH. */
	queue_mutex = (sem_t *)malloc(sizeof(sem_t));
	queue_notify = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(queue_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue mutex");
		return EXIT_FAILURE;
	}
	retval = sem_init(queue_notify, 0, 0);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize queue notify");
		return EXIT_FAILURE;
	}
	/* DONE - Initialize queue protection variables. DO NOT TOUCH */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;

}
