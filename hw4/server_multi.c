/*******************************************************************************
* Dual-Threaded FIFO Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client requests in First In,
*     First Out (FIFO) order. The server binds to the specified port number
*     provided as a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue size.
*
* Usage:
*     <build directory>/server -q <queue_size> -w <workers> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests
*     workers     - The number of parallel threads to process requests.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 29, 2023
*
* Notes:
*     Ensure to have proper permissions and available port before running the
*     server. The server relies on a FIFO mechanism to handle requests, thus
*     guaranteeing the order of processing. If the queue is full at the time a
*     new request is received, the request is rejected with a negative ack.
*
*******************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sched.h>
#include <signal.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s -q <queue size> <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* Mutex needed to protect the threaded printf. DO NOT TOUCH */
sem_t * printf_mutex;

/* Synchronized printf for multi-threaded operation */
#define sync_printf(...)			\
	do {					\
		sem_wait(printf_mutex);		\
		printf(__VA_ARGS__);		\
		sem_post(printf_mutex);		\
	} while (0)

/* START - Variables needed to protect the shared queue. DO NOT TOUCH */
sem_t * queue_mutex;
sem_t * queue_notify;
/* END - Variables needed to protect the shared queue. DO NOT TOUCH */

struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
};

// Structure to represent a queue for managing requests
struct queue {
    size_t wr_pos;          // Write position in the queue
    size_t rd_pos;          // Read position in the queue
    size_t max_size;        // Maximum size of the queue
    size_t available;       // Number of available slots in the queue
    struct request_meta * requests;  // Array to store request metadata
};

// Structure for passing connection parameters to functions
struct connection_params {
    size_t queue_size;  // Size of the queue
    size_t workers;     // Number of worker threads
};

// Structure for passing parameters to worker threads
struct worker_params {
    int conn_socket;     // Connection socket descriptor
    int worker_done;     // Flag to signal worker thread termination
    struct queue * the_queue;  // Pointer to the shared queue
    int worker_id;       // ID of the worker thread
};

// Enumeration for worker thread commands
enum worker_command {
    WORKERS_START,  // Command to start worker threads
    WORKERS_STOP    // Command to stop worker threads
};

void queue_init(struct queue * the_queue, size_t queue_size)
{
	the_queue->rd_pos = 0;
	the_queue->wr_pos = 0;
	the_queue->max_size = queue_size;
	the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta)
						     * the_queue->max_size);
	the_queue->available = queue_size;
}

/* Add a new request <request> to the shared queue <the_queue> */
int add_to_queue(struct request_meta to_add, struct queue * the_queue)
{
	int retval = 0;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */

	/* Make sure that the queue is not full */
	if (the_queue->available == 0) {
		retval = 1;
	} else {
		/* If all good, add the item in the queue */
		the_queue->requests[the_queue->wr_pos] = to_add;
		the_queue->wr_pos = (the_queue->wr_pos + 1) % the_queue->max_size;
		the_queue->available--;

		/* QUEUE SIGNALING FOR CONSUMER --- DO NOT TOUCH */
		sem_post(queue_notify);
	}

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

/* Add a new request <request> to the shared queue <the_queue> */
struct request_meta get_from_queue(struct queue * the_queue)
{
	struct request_meta retval;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_notify);
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	retval = the_queue->requests[the_queue->rd_pos];
	the_queue->rd_pos = (the_queue->rd_pos + 1) % the_queue->max_size;
	the_queue->available++;

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
	return retval;
}

void dump_queue_status(struct queue * the_queue)
{
	size_t i, j;
	/* QUEUE PROTECTION INTRO START --- DO NOT TOUCH */
	sem_wait(queue_mutex);
	/* QUEUE PROTECTION INTRO END --- DO NOT TOUCH */

	/* WRITE YOUR CODE HERE! */
	/* MAKE SURE NOT TO RETURN WITHOUT GOING THROUGH THE OUTRO CODE! */
	sync_printf("Q:[");

	for (i = the_queue->rd_pos, j = 0; j < the_queue->max_size - the_queue->available;
	     i = (i + 1) % the_queue->max_size, ++j)
	{
		sync_printf("R%ld%s", the_queue->requests[i].request.req_id,
		       ((j+1 != the_queue->max_size - the_queue->available)?",":""));
	}

	sync_printf("]\n");

	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

/* Main logic of the worker thread */
int worker_main (void * arg)
{
	struct timespec now;
	struct worker_params * params = (struct worker_params *)arg;

	/* Print the first alive message. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	sync_printf("[#WORKER#] %lf Worker Thread Alive!\n", TSPEC_TO_DOUBLE(now));

	/* Okay, now execute the main logic. */
	while (!params->worker_done) {

		struct request_meta req;
		struct response resp;
		req = get_from_queue(params->the_queue);

		/* Detect wakeup after termination asserted */
		if (params->worker_done)
			break;

		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);
		busywait_timespec(req.request.req_length);
		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);

		/* Now provide a response! */
		resp.req_id = req.request.req_id;
		resp.ack = RESP_COMPLETED;
		send(params->conn_socket, &resp, sizeof(struct response), 0);

		sync_printf("T%d R%ld:%lf,%lf,%lf,%lf,%lf\n",
		       params->worker_id,
		       req.request.req_id,
		       TSPEC_TO_DOUBLE(req.request.req_timestamp),
		       TSPEC_TO_DOUBLE(req.request.req_length),
		       TSPEC_TO_DOUBLE(req.receipt_timestamp),
		       TSPEC_TO_DOUBLE(req.start_timestamp),
		       TSPEC_TO_DOUBLE(req.completion_timestamp)
			);

		dump_queue_status(params->the_queue);
	}

	return EXIT_SUCCESS;
}

// Function to control the starting and stopping of worker threads
int control_workers(enum worker_command cmd, size_t worker_count, struct worker_params * common_params)
{
    // Static pointers for worker stacks, parameters, and IDs to persist across function calls
    static char ** worker_stacks = NULL;
    static struct worker_params ** worker_params = NULL;
    static int * worker_ids = NULL;

    // If the command is to start worker threads
    if (cmd == WORKERS_START) {
        size_t i;

        // Allocate memory for worker stacks, parameters, and thread IDs
        worker_stacks = (char **)malloc(worker_count * sizeof(char *));
        worker_params = (struct worker_params **)malloc(worker_count * sizeof(struct worker_params *));
        worker_ids = (int *)malloc(worker_count * sizeof(int));

        // Check for allocation failure
        if (!worker_stacks || !worker_params) {
            ERROR_INFO();
            perror("Unable to allocate descriptor arrays for threads.");
            return EXIT_FAILURE;
        }

        // Initialize each worker's stack, parameters, and set their IDs
        for (i = 0; i < worker_count; ++i) {
            worker_ids[i] = -1;

            worker_stacks[i] = malloc(STACK_SIZE);
            worker_params[i] = (struct worker_params *)malloc(sizeof(struct worker_params));

            // Check for allocation failure
            if (!worker_stacks[i] || !worker_params[i]) {
                ERROR_INFO();
                perror("Unable to allocate memory for thread.");
                return EXIT_FAILURE;
            }

            // Set up the worker parameters
            worker_params[i]->conn_socket = common_params->conn_socket;
            worker_params[i]->the_queue = common_params->the_queue;
            worker_params[i]->worker_done = 0;
            worker_params[i]->worker_id = i;
        }

        // Start the worker threads
        for (i = 0; i < worker_count; ++i) {
            worker_ids[i] = clone(worker_main, worker_stacks[i] + STACK_SIZE,
                                  CLONE_THREAD | CLONE_VM | CLONE_SIGHAND |
                                  CLONE_FS | CLONE_FILES | CLONE_SYSVSEM,
                                  worker_params[i]);

            // Check if thread creation is successful
            if (worker_ids[i] < 0) {
                ERROR_INFO();
                perror("Unable to start thread.");
                return EXIT_FAILURE;
            } else {
                sync_printf("INFO: Worker thread %ld (TID = %d) started!\n",
                            i, worker_ids[i]);
            }
        }
    }

    // If the command is to stop worker threads
    else if (cmd == WORKERS_STOP) {
        size_t i;

        // Check if the worker threads have been started
        if (!worker_stacks || !worker_params || !worker_ids) {
            return EXIT_FAILURE;
        }

        // Signal all worker threads to terminate
        for (i = 0; i < worker_count; ++i) {
            if (worker_ids[i] < 0) {
                continue;
            }

            // Set the termination flag
            worker_params[i]->worker_done = 1;
        }

        // Unblock and wait for all threads to complete
        for (i = 0; i < worker_count; ++i) {
            if (worker_ids[i] < 0) {
                continue;
            }

            sem_post(queue_notify); // Unblock if waiting on the queue
            waitpid(-1, NULL, 0);   // Wait for the thread to terminate
            sync_printf("INFO: Worker thread exited.\n");
        }

        // Deallocate memory for worker stacks, parameters, and IDs
        for (i = 0; i < worker_count; ++i) {
            free(worker_stacks[i]);
            free(worker_params[i]);
        }

        free(worker_stacks);
        worker_stacks = NULL;

        free(worker_params);
        worker_params = NULL;

        free(worker_ids);
        worker_ids = NULL;
    }

    // Handle invalid command case
    else {
        ERROR_INFO();
        perror("Invalid thread control command.");
        return EXIT_FAILURE;
    }

    // Return success if everything went as expected
    return EXIT_SUCCESS;
}



// This function handles the network connection with a client. It manages the receiving
// of requests from the client, adding them to a processing queue, and signaling worker threads.
// The function continues running until the client connection is interrupted.
void handle_connection(int conn_socket, struct connection_params conn_params)
{
    // Declare pointers for request metadata and the request queue
    struct request_meta * req;
    struct queue * the_queue;
    size_t in_bytes;

    // Parameters shared among all worker threads
    struct worker_params common_worker_params;
    int res;

    // Allocate memory for the queue and initialize it with the specified size
    the_queue = (struct queue *)malloc(sizeof(struct queue));
    queue_init(the_queue, conn_params.queue_size);

    // Set the connection socket and the queue in the common worker parameters
    common_worker_params.conn_socket = conn_socket;
    common_worker_params.the_queue = the_queue;
    // Start the worker threads and check for successful initiation
    res = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

    // If worker threads couldn't start, clean up and return
    if (res != EXIT_SUCCESS) {
        free(the_queue);
        control_workers(WORKERS_STOP, conn_params.workers, NULL);
        return;
    }

    // Allocate memory for request metadata
    req = (struct request_meta *)malloc(sizeof(struct request_meta));

    // Continuously process incoming requests
    do {
        // Receive a request from the client
        in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
        // Record the timestamp when the request was received
        clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

        // Process the request if data is received
        if (in_bytes > 0) {
            // Attempt to add the request to the queue
            res = add_to_queue(*req, the_queue);

            // If the queue is full, reject the request and notify the client
            if (res) {
                struct response resp;
                resp.req_id = req->request.req_id;
                resp.ack = RESP_REJECTED;
                send(conn_socket, &resp, sizeof(struct response), 0);

                // Log rejection information
                sync_printf("X%ld:%lf,%lf,%lf\n", req->request.req_id,
                            TSPEC_TO_DOUBLE(req->request.req_timestamp),
                            TSPEC_TO_DOUBLE(req->request.req_length),
                            TSPEC_TO_DOUBLE(req->receipt_timestamp));
            }
        }
    } while (in_bytes > 0); // Continue until the connection is closed or an error occurs

    // Stop all worker threads before exiting
    control_workers(WORKERS_STOP, conn_params.workers, NULL);

    // Clean up resources and close the connection socket
    free(req);
    shutdown(conn_socket, SHUT_RDWR);
    close(conn_socket);
    printf("INFO: Client disconnected.\n"); // Log the disconnection event
}



// Template implementation of the main function for the FIFO server
int main(int argc, char **argv) {
    // Declare variables for socket descriptor, return value, and accepted connection
    int sockfd, retval, accepted, optval, opt;

    // Define variables for port number and socket address structures
    in_port_t socket_port;
    struct sockaddr_in addr, client;
    struct in_addr any_address;
    socklen_t client_len;

    // Initialize connection parameters with default values
    struct connection_params conn_params;
    conn_params.queue_size = 0;
    conn_params.workers = 1;

    // Parse all command line arguments
    while ((opt = getopt(argc, argv, "q:w:")) != -1) {
        switch (opt) {
            case 'q': // For queue size
                conn_params.queue_size = strtol(optarg, NULL, 10);
                printf("INFO: setting queue size = %ld\n", conn_params.queue_size);
                break;
            case 'w': // For number of worker threads
                conn_params.workers = strtol(optarg, NULL, 10);
                printf("INFO: setting worker count = %ld\n", conn_params.workers);
                break;
            default: // In case of an unrecognized option
                fprintf(stderr, USAGE_STRING, argv[0]);
                break;
        }
    }

    // Check for valid queue size
    if (!conn_params.queue_size) {
        ERROR_INFO();
        fprintf(stderr, USAGE_STRING, argv[0]);
        return EXIT_FAILURE;
    }

    // Set server port number if provided
    if (optind < argc) {
        socket_port = strtol(argv[optind], NULL, 10);
        printf("INFO: setting server port as: %d\n", socket_port);
    } else {
        ERROR_INFO();
        fprintf(stderr, USAGE_STRING, argv[0]);
        return EXIT_FAILURE;
    }

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        ERROR_INFO();
        perror("Unable to create socket");
        return EXIT_FAILURE;
    }

    // Enable reuse of local addresses for this socket
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&optval, sizeof(optval));

    // Setup the socket address structure
    any_address.s_addr = htonl(INADDR_ANY);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(socket_port);
    addr.sin_addr = any_address;

    // Bind the socket to the specified port
    retval = bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
    if (retval < 0) {
        ERROR_INFO();
        perror("Unable to bind socket");
        return EXIT_FAILURE;
    }

    // Listen for incoming connections
    retval = listen(sockfd, BACKLOG_COUNT);
    if (retval < 0) {
        ERROR_INFO();
        perror("Unable to listen on socket");
        return EXIT_FAILURE;
    }

    // Ready to accept client connections
    printf("INFO: Waiting for incoming connection...\n");
    client_len = sizeof(struct sockaddr_in);
    accepted = accept(sockfd, (struct sockaddr *)&client, &client_len);
    if (accepted == -1) {
        ERROR_INFO();
        perror("Unable to accept connections");
        return EXIT_FAILURE;
    }

    // Initialize mutex for synchronized printf in a multi-threaded environment
    printf_mutex = (sem_t *)malloc(sizeof(sem_t));
    retval = sem_init(printf_mutex, 0, 1);
    if (retval < 0) {
        ERROR_INFO();
        perror("Unable to initialize printf mutex");
        return EXIT_FAILURE;
    }

    // Initialize semaphores for queue protection
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

    // Handle the new client connection
    handle_connection(accepted, conn_params);

    // Clean up and close resources
    free(queue_mutex);
    free(queue_notify);
    close(sockfd);

    return EXIT_SUCCESS; // Exit with successful execution status
}
