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

/* Needed for wait(...) */
#include <sys/types.h>
#include <sys/wait.h>

/* Include struct definitions and other libraries that need to be
 * included by both client and server */
#include "common.h"

#define BACKLOG_COUNT 100
#define USAGE_STRING				\
	"Missing parameter. Exiting.\n"		\
	"Usage: %s <port_number>\n"

/* 4KB of stack for the worker thread */
#define STACK_SIZE (4096)

/* Main logic of the worker thread */
/* IMPLEMENT HERE THE MAIN FUNCTION OF THE WORKER */

int worker_main(void *arg) {
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
	printf("[#WORKER#] %f Worker Thread Alive!\n", TSPEC_TO_DOUBLE(current_time));

	while(1){
        get_elapsed_busywait(1, 0); // busywait for 1s
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        printf("[#WORKER#] %f Still Alive!\n", TSPEC_TO_DOUBLE(current_time));
        get_elapsed_sleep(1, 0); // sleep for 1s
    } 

    return 0;
}

/* Main function to handle connection with the client. This function
 * takes in input conn_socket and returns only when the connection
 * with the client is interrupted. */
void handle_connection(int conn_socket)
{
	/* The connection with the client is alive here, 
	   i.e., already gone through socker(), bind(), listen(), accept(). 
	 * Let's start the worker thread. */
	/* IMPLEMENT HERE THE LOGIC TO START THE WORKER THREAD. */
	void *worker_stack = malloc(STACK_SIZE);  // allocate stack for worker thread
    if (!worker_stack) {
        perror("failed to allocate stack for worker thread");
        exit(EXIT_FAILURE);
    }
	int flags = CLONE_THREAD | CLONE_VM | CLONE_SIGHAND | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM;
    int worker = clone(worker_main, worker_stack + STACK_SIZE, 
                          flags, 
                          NULL);
	// CLONE_VM: shares the same memory space with the parent
	// CLONE_SIGHAND: shares signal handlers
	// CLONE_FS: shares the same filesystem info
	// CLONE_FILES: shares file descriptors
	// CLONE_SYSVSEM: shares System V IPC namespace
    if (worker == -1) {
        perror("failed to create worker thread");
        free(worker_stack);
        exit(EXIT_FAILURE);
    }

	/* We are ready to proceed with the rest of the request
	 * handling logic. */
	while (1) {
		// receive the request from the client
    	struct request received;  // a request struct to hold the incoming request
    	ssize_t bytes_rcved = recv(conn_socket, &received, sizeof(struct request), 0);
    	// a socket function to receive data from a connected socket

    	// conditions to break out of the infinite loop
    	if (bytes_rcved < 0) { 
        	perror("received bytes of invalid size");
        	return;
    	} else if (bytes_rcved == 0) {  // client disconnected
        	printf("client disconnected\n");
        	return;
    	}

        // timestamp when the request is received
        struct timespec receipt_timestamp;
        clock_gettime(CLOCK_MONOTONIC, &receipt_timestamp); // get the time from OS 

        // measure elapsed time using busy-wait
        get_elapsed_busywait(received.req_length.tv_sec, received.req_length.tv_nsec);

        // send the request id back to the client
        ssize_t bytes_sent = send(conn_socket, &received.req_id, sizeof(uint64_t), 0);

        // the timestamp after response id is sent
        struct timespec completion_timestamp;
        clock_gettime(CLOCK_MONOTONIC, &completion_timestamp);

        if (bytes_sent < 0) {
            perror("response id of invalid size");
            return;
        }

        printf("R%lu:%lf,%lf,%lf,%lf\n",
               received.req_id,
               TSPEC_TO_DOUBLE(received.req_timestamp),
               TSPEC_TO_DOUBLE(received.req_length),
               TSPEC_TO_DOUBLE(receipt_timestamp),
               TSPEC_TO_DOUBLE(completion_timestamp));
    }
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

	/* Ready to handle the new connection with the client. */
	handle_connection(accepted);

	close(sockfd);
	return EXIT_SUCCESS;

}
