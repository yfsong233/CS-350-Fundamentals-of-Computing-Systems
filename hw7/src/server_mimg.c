/*******************************************************************************
* Single-Threaded FIFO Image Server Implementation w/ Queue Limit
*
* Description:
*     A server implementation designed to process client
*     requests for image processing in First In, First Out (FIFO)
*     order. The server binds to the specified port number provided as
*     a parameter upon launch. It launches a secondary thread to
*     process incoming requests and allows to specify a maximum queue
*     size.
*
* Usage:
*     <build directory>/server -q <queue_size> -w <workers> -p <policy> <port_number>
*
* Parameters:
*     port_number - The port number to bind the server to.
*     queue_size  - The maximum number of queued requests.
*     workers     - The number of parallel threads to process requests.
*     policy      - The queue policy to use for request dispatching.
*
* Author:
*     Renato Mancuso
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     October 31, 2023
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
#include <assert.h>

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Needed for semaphores */
#include <semaphore.h>
#include <stdbool.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s -q <queue size> "		\
	"-w <workers: 1> "			\
	"-p <policy: FIFO> "			\
	"<port_number>\n"

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

sem_t ** image_semaphores = NULL;
uint64_t sem_count = 0;

sem_t * images_array_sem;
sem_t * socket_sem;

sem_t * std_out;
sem_t * register_sem;


/* Global array of registered images and its length -- reallocated as we go! */
struct img_sem ** images = NULL;
uint64_t image_count = 0;

struct img_sem {
	struct image *img;
	sem_t *sem;
	int curr_op; 
	int latest_op;
	sem_t *count_mutex;
};


struct request_meta {
	struct request request;
	struct timespec receipt_timestamp;
	struct timespec start_timestamp;
	struct timespec completion_timestamp;
};

enum queue_policy {
	QUEUE_FIFO,
	QUEUE_SJN
};

struct queue {
	size_t wr_pos;
	size_t rd_pos;
	size_t max_size;
	size_t available;
	enum queue_policy policy;
	struct request_meta * requests;
};

struct connection_params {
	size_t queue_size;
	size_t workers;
	enum queue_policy queue_policy;
};

struct worker_params {
	int conn_socket;
	int worker_done;
	struct queue * the_queue;
	int worker_id;
};

enum worker_command {
	WORKERS_START,
	WORKERS_STOP
};




void queue_init(struct queue * the_queue, size_t queue_size, enum queue_policy policy)
{
	the_queue->rd_pos = 0;
	the_queue->wr_pos = 0;
	the_queue->max_size = queue_size;
	the_queue->requests = (struct request_meta *)malloc(sizeof(struct request_meta)
						     * the_queue->max_size);
	the_queue->available = queue_size;
	the_queue->policy = policy;
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
	sem_wait(printf_mutex);
	printf("Q:[");

	for (i = the_queue->rd_pos, j = 0; j < the_queue->max_size - the_queue->available;
	     i = (i + 1) % the_queue->max_size, ++j)
	{
		printf("R%ld%s", the_queue->requests[i].request.req_id,
		       ((j+1 != the_queue->max_size - the_queue->available)?",":""));
	}

	printf("]\n");
	sem_post(printf_mutex);
	/* QUEUE PROTECTION OUTRO START --- DO NOT TOUCH */
	sem_post(queue_mutex);
	/* QUEUE PROTECTION OUTRO END --- DO NOT TOUCH */
}

void register_new_image(int conn_socket, struct request * req)
{
	/* Increase the count of registered images */
	image_count++;

	/* Reallocate array of image pointers */
	images = realloc(images, image_count * sizeof(struct img_sem *));
	

	/* Store its pointer at the end of the global array */
	struct image *new_img = recvImage(conn_socket);
	struct img_sem *img_sem = (struct img_sem *)malloc(sizeof(struct img_sem));
	img_sem->img = new_img;
	img_sem->sem = (sem_t *)malloc(sizeof(sem_t));
	img_sem->count_mutex = (sem_t *)malloc(sizeof(sem_t));
	img_sem->curr_op = 0;
	img_sem->latest_op = 0; 
	sem_init(img_sem->sem, 0, 1);
	sem_init(img_sem->count_mutex, 0, 1);

	images[image_count - 1] = img_sem;

	struct response resp;
	resp.req_id = req->req_id;
	resp.img_id = image_count - 1;
	resp.ack = RESP_COMPLETED;
	sem_wait(socket_sem);
	send(conn_socket, &resp, sizeof(struct response), 0);
	sem_post(socket_sem);
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
		struct image * img = NULL;
		uint64_t img_id;
		int turn;
		req = get_from_queue(params->the_queue);

		/* Detect wakeup after termination asserted */
		if (params->worker_done)
			break;

		img_id = req.request.img_id;
		sem_wait(images[img_id]->count_mutex);
		turn = images[img_id]->latest_op;
		images[img_id]->latest_op++;
		sem_post(images[img_id]->count_mutex);
		sem_post(queue_mutex);
		sem_wait(images[img_id]->sem);
		while (turn != images[img_id]->curr_op){
			sem_post(images[img_id]->sem);
			sem_wait(images[img_id]->sem);
			
		}
		/* Find the image to work on */
		img = images[img_id]->img;
		clock_gettime(CLOCK_MONOTONIC, &req.start_timestamp);
		
		assert(img != NULL);

		switch (req.request.img_op) {
		case IMG_ROT90CLKW:
			img = rotate90Clockwise(img, NULL);
			break;
		case IMG_BLUR:
			img = blurImage(img);
			break;
		case IMG_SHARPEN:
			img = sharpenImage(img);
			break;
		case IMG_VERTEDGES:
			img = detectVerticalEdges(img);
			break;
		case IMG_HORIZEDGES:
			img = detectHorizontalEdges(img);
			break;
		}

		

		if (req.request.img_op != IMG_RETRIEVE) {
			

			if (req.request.overwrite) {
				/* Deallocate the previous image */
				free(images[img_id]->img);
				/* Overwrite the pointer with the one to the new image */
				images[img_id]->img = img;
			} else {
				/* Generate new ID and Increase the # of registered images */
				sem_wait(register_sem);
				img_id = image_count++;
				/* Reallocate array of image pointers */
				images = realloc(images, image_count * sizeof(struct image_sem *));
				/* Store its pointer at the end of the global array */
				images[img_id]->img = img;
				images[img_id]->sem = (sem_t *)malloc(sizeof(sem_t));
				sem_init(images[img_id]->sem, 0, 1);
				sem_post(register_sem);
			}
			
		}
		
		clock_gettime(CLOCK_MONOTONIC, &req.completion_timestamp);
		
		/* Now provide a response! */
		
		resp.req_id = req.request.req_id;
		resp.ack = RESP_COMPLETED;
		resp.img_id = img_id;
		
		sem_wait(socket_sem);
		send(params->conn_socket, &resp, sizeof(struct response), 0);

		/* In case of IMG_RETRIEVE, we need to send out the
		 * actual image payload! */
		if (req.request.img_op == IMG_RETRIEVE) {
			
			uint8_t err = sendImage(img, params->conn_socket);

			if(err) {
				ERROR_INFO();
				perror("Unable to send image payload to client.");
			}
			
		}
		sem_post(socket_sem);
		
		sync_printf("T%d R%ld:%lf,%s,%d,%ld,%ld,%lf,%lf,%lf\n",
		       params->worker_id, req.request.req_id,
		       TSPEC_TO_DOUBLE(req.request.req_timestamp),
		       OPCODE_TO_STRING(req.request.img_op),
		       req.request.overwrite, req.request.img_id, img_id,
		       TSPEC_TO_DOUBLE(req.receipt_timestamp),
		       TSPEC_TO_DOUBLE(req.start_timestamp),
		       TSPEC_TO_DOUBLE(req.completion_timestamp));

		// dump_queue_status(params->the_queue);
		images[img_id]->curr_op++;
		sem_post(images[img_id]->sem); 
	}
	

	return EXIT_SUCCESS;
}

/* This function will start/stop all the worker threads wrapping
 * around the clone()/waitpid() system calls */
int control_workers(enum worker_command cmd, size_t worker_count,
		    struct worker_params * common_params)
{
	/* Anything we allocate should we kept as static for easy
	 * deallocation when the STOP command is issued */
	static char ** worker_stacks = NULL;
	static struct worker_params ** worker_params = NULL;
	static int * worker_ids = NULL;

	/* Start all the workers */
	if (cmd == WORKERS_START) {
		size_t i;
		/* Allocate all stacks and parameters */
		worker_stacks = (char **)malloc(worker_count * sizeof(char *));
		worker_params = (struct worker_params **)
			malloc(worker_count * sizeof(struct worker_params *));
		worker_ids = (int *)malloc(worker_count * sizeof(int));

		if (!worker_stacks || !worker_params) {
			ERROR_INFO();
			perror("Unable to allocate descriptor arrays for threads.");
			return EXIT_FAILURE;
		}

		/* Allocate and initialize as needed */
		for (i = 0; i < worker_count; ++i) {
			worker_ids[i] = -1;

			worker_stacks[i] = malloc(STACK_SIZE);
			worker_params[i] = (struct worker_params *)
				malloc(sizeof(struct worker_params));

			if (!worker_stacks[i] || !worker_params[i]) {
				ERROR_INFO();
				perror("Unable to allocate memory for thread.");
				return EXIT_FAILURE;
			}

			worker_params[i]->conn_socket = common_params->conn_socket;
			worker_params[i]->the_queue = common_params->the_queue;
			worker_params[i]->worker_done = 0;
			worker_params[i]->worker_id = i;
		}

		/* All the allocations and initialization seem okay,
		 * let's start the threads */
		for (i = 0; i < worker_count; ++i) {
			worker_ids[i] = clone(worker_main, worker_stacks[i] + STACK_SIZE,
					      CLONE_THREAD | CLONE_VM | CLONE_SIGHAND |
					      CLONE_FS | CLONE_FILES | CLONE_SYSVSEM,
					      worker_params[i]);

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

	else if (cmd == WORKERS_STOP) {
		size_t i;

		/* Command to stop the threads issues without a start
		 * command? */
		if (!worker_stacks || !worker_params || !worker_ids) {
			return EXIT_FAILURE;
		}

		/* First, assert all the termination flags */
		for (i = 0; i < worker_count; ++i) {
			if (worker_ids[i] < 0) {
				continue;
			}

			/* Request thread termination */
			worker_params[i]->worker_done = 1;
		}

		/* Next, unblock threads and wait for completion */
		for (i = 0; i < worker_count; ++i) {
			if (worker_ids[i] < 0) {
				continue;
			}

			sem_post(queue_notify);
			waitpid(-1, NULL, __WCLONE);
			sync_printf("INFO: Worker thread exited.\n");
		}

		/* Finally, do a round of deallocations */
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

	else {
		ERROR_INFO();
		perror("Invalid thread control command.");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket, struct connection_params conn_params)
{
	struct request_meta * req;
	struct queue * the_queue;
	size_t in_bytes;

	/* The connection with the client is alive here. Let's start
	 * the worker thread. */
	struct worker_params common_worker_params;
	int res;

	/* Now handle queue allocation and initialization */
	the_queue = (struct queue *)malloc(sizeof(struct queue));
	queue_init(the_queue, conn_params.queue_size, conn_params.queue_policy);

	common_worker_params.conn_socket = conn_socket;
	common_worker_params.the_queue = the_queue;
	res = control_workers(WORKERS_START, conn_params.workers, &common_worker_params);

	/* Do not continue if there has been a problem while starting
	 * the workers. */
	if (res != EXIT_SUCCESS) {
		free(the_queue);

		/* Stop any worker that was successfully started */
		control_workers(WORKERS_STOP, conn_params.workers, NULL);
		return;
	}

	/* We are ready to proceed with the rest of the request
	 * handling logic. */

	req = (struct request_meta *)malloc(sizeof(struct request_meta));

	do {
		in_bytes = recv(conn_socket, &req->request, sizeof(struct request), 0);
		clock_gettime(CLOCK_MONOTONIC, &req->receipt_timestamp);

		/* Don't just return if in_bytes is 0 or -1. Instead
		 * skip the response and break out of the loop in an
		 * orderly fashion so that we can de-allocate the req
		 * and resp varaibles, and shutdown the socket. */
		if (in_bytes > 0) {

			/* Handle image registration right away! */
			if(req->request.img_op == IMG_REGISTER) {
				sem_wait(register_sem); 
				clock_gettime(CLOCK_MONOTONIC, &req->start_timestamp);

				register_new_image(conn_socket, &req->request);

				clock_gettime(CLOCK_MONOTONIC, &req->completion_timestamp);
				sem_post(register_sem);
				// sem_wait(std_out);
				sync_printf("T%ld R%ld:%lf,%s,%d,%ld,%ld,%lf,%lf,%lf\n",
				       conn_params.workers, req->request.req_id,
				       TSPEC_TO_DOUBLE(req->request.req_timestamp),
				       OPCODE_TO_STRING(req->request.img_op),
				       req->request.overwrite, req->request.img_id,
				       image_count - 1, /* Registered ID on server side */
				       TSPEC_TO_DOUBLE(req->receipt_timestamp),
				       TSPEC_TO_DOUBLE(req->start_timestamp),
				       TSPEC_TO_DOUBLE(req->completion_timestamp));
				
				dump_queue_status(the_queue);
				// sem_post(std_out);
				// sem_post(socket_sem);
				continue;
			}

			res = add_to_queue(*req, the_queue);

			/* The queue is full if the return value is 1 */
			if (res) {
				struct response resp;
				/* Now provide a response! */
				resp.req_id = req->request.req_id;
				resp.ack = RESP_REJECTED;
				sem_wait(socket_sem);
				send(conn_socket, &resp, sizeof(struct response), 0);
				sem_post(socket_sem);
				// sem_wait(std_out);
				sync_printf("X%ld:%lf,%lf,%lf\n", req->request.req_id,
				       TSPEC_TO_DOUBLE(req->request.req_timestamp),
				       TSPEC_TO_DOUBLE(req->request.req_length),
				       TSPEC_TO_DOUBLE(req->receipt_timestamp)
					);

				// sem_post(std_out);
			}
		}
	} while (in_bytes > 0);


	/* Stop all the worker threads. */
	control_workers(WORKERS_STOP, conn_params.workers, NULL);

	free(req);
	shutdown(conn_socket, SHUT_RDWR);
	close(conn_socket);
	printf("INFO: Client disconnected.\n");
}


/* Template implementation of the main function for the FIFO
 * server. The server must accept in input a command line parameter
 * with the <port number> to bind the server to. */
int main (int argc, char ** argv) {
	int sockfd, retval, accepted, optval, opt;
	in_port_t socket_port;
	struct sockaddr_in addr, client;
	struct in_addr any_address;
	socklen_t client_len;
	struct connection_params conn_params;
	conn_params.queue_size = 0;
	conn_params.queue_policy = QUEUE_FIFO;
	conn_params.workers = 1;

	/* Parse all the command line arguments */
	while((opt = getopt(argc, argv, "q:w:p:")) != -1) {
		switch (opt) {
		case 'q':
			conn_params.queue_size = strtol(optarg, NULL, 10);
			printf("INFO: setting queue size = %ld\n", conn_params.queue_size);
			break;
		case 'w':
			conn_params.workers = strtol(optarg, NULL, 10);
			printf("INFO: setting worker count = %ld\n", conn_params.workers);
			// if (conn_params.workers != 1) {
			// 	ERROR_INFO();
			// 	fprintf(stderr, "Only 1 worker is supported in this implementation!\n" USAGE_STRING, argv[0]);
			// 	return EXIT_FAILURE;
			// }
			break;
		case 'p':
			if (!strcmp(optarg, "FIFO")) {
				conn_params.queue_policy = QUEUE_FIFO;
			} else {
				ERROR_INFO();
				fprintf(stderr, "Invalid queue policy.\n" USAGE_STRING, argv[0]);
				return EXIT_FAILURE;
			}
			printf("INFO: setting queue policy = %s\n", optarg);
			break;
		default: /* '?' */
			fprintf(stderr, USAGE_STRING, argv[0]);
		}
	}

	if (!conn_params.queue_size) {
		ERROR_INFO();
		fprintf(stderr, USAGE_STRING, argv[0]);
		return EXIT_FAILURE;
	}

	if (optind < argc) {
		socket_port = strtol(argv[optind], NULL, 10);
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

	// /* Initilize threaded img sem */
	// img_sem = (sem_t *)malloc(sizeof(sem_t));
	// retval = sem_init(img_sem, 0, 1);
	// if (retval < 0) {
	// 	ERROR_INFO();
	// 	perror("Unable to initialize img sem ");
	// 	return EXIT_FAILURE;
	// }

	/* Initilize threaded printf mutex */
	printf_mutex = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(printf_mutex, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize printf mutex");
		return EXIT_FAILURE;
	}

	images_array_sem = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(images_array_sem, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize images_array_sem");
			return EXIT_FAILURE;
	}

	register_sem = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(register_sem, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize register_sem");
		return EXIT_FAILURE;
	}

	std_out = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(std_out, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize images_array_sem");
		return EXIT_FAILURE;
	}

	socket_sem = (sem_t *)malloc(sizeof(sem_t));
	retval = sem_init(socket_sem, 0, 1);
	if (retval < 0) {
		ERROR_INFO();
		perror("Unable to initialize images_array_sem");
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
	/* DONE - Initialize queue protection variables */

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted, conn_params);

	free(queue_mutex);
	free(queue_notify);

	close(sockfd);
	return EXIT_SUCCESS;
}
