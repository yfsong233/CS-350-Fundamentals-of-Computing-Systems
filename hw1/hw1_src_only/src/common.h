#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <math.h>

/* Includes that are specific for TCP/IP */
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */

/* Includes that are specific for TCP/IP */
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */

/* Include our own timelib */
#include "timelib.h"

/* This is a handy definition to print out runtime errors that report
 * the file and line number where the error was encountered. */
#define ERROR_INFO()							\
	fprintf(stderr, "Runtime error at %s:%d\n", __FILE__, __LINE__)

/* A simple macro to convert between a struct timespec and a double
 * representation of a timestamp. */
#define TSPEC_TO_DOUBLE(spec)				\
    ((double)(spec.tv_sec) + (double)(spec.tv_nsec)/NANO_IN_SEC)

/* Request payload as sent by the client and received by the
 * server. */
struct request {
	/* ADD REQUEST FIELDS. */
	uint64_t request_id;  // 64-bit request id
    struct timespec sent_timestamp;  // a timestamp{sec, nsec} of when a request is sent from the client side
    struct timespec request_length;  // a timestamp storing the length of request
};

/* Response payload as sent by the server and received by the
 * client. */
struct response {
	/* ADD RESPONSE FIELDS. */
	uint64_t request_id;
};
