/*******************************************************************************
* Time Functions Library (implementation)
*
* Description:
*     A library to handle various time-related functions and operations.
*
* Author:
*     Renato Mancuso <rmancuso@bu.edu>
*
* Affiliation:
*     Boston University
*
* Creation Date:
*     September 10, 2023
*
* Notes:
*     Ensure to link against the necessary dependencies when compiling and
*     using this library. Modifications or improvements are welcome. Please
*     refer to the accompanying documentation for detailed usage instructions.
*
*******************************************************************************/

#include "timelib.h"

/* Return the number of clock cycles elapsed when waiting for
 * wait_time seconds using sleeping functions */

uint64_t get_elapsed_sleep(long sec, long nsec) {
    uint64_t start, end;
    struct timespec sleep_time, remaining_time;
    // sleep_time: how long the system should sleep
    // remaining_time: the remaining time if the nanosleep function gets interrupted.
    sleep_time.tv_sec = sec;
    sleep_time.tv_nsec = nsec;
    get_clocks(start);  // snapshot the starting TSC value that measures cpu clocks
    nanosleep(&sleep_time, &remaining_time); // sleep for specified secs
    get_clocks(end);  // snapshot the ending TSC value
    return end-start;
}


/* Return the number of clock cycles elapsed when waiting for
 * wait_time seconds using busy-waiting functions */
uint64_t get_elapsed_busywait(long sec, long nsec) {
    uint64_t start, end;
    struct timespec begin_timestamp, current_timestamp, sleep_timestamp;
    sleep_timestamp.tv_sec = sec;
    sleep_timestamp.tv_nsec = nsec;

    uint64_t init_time = clock_gettime(CLOCK_MONOTONIC, &begin_timestamp); // the starting timestamp provided by os clock service
    // The clock_gettime function retrieves the current time from the OS's monotonic clock and stores it in begin_timestamp. 
    // This function also returns an integer status value (0 for success and -1 for failure), which is stored in init_time
    // init_time is not used later on so it is not necessary to assign the integer status to a variable.

    get_clocks(start);  // snapshot the starting TSC value that measures cpu clocks

    struct timespec final_timestamp = {0, 0}; // the ending time that breaks out of the loop when reached
    timespec_add(&final_timestamp, &begin_timestamp);
    timespec_add(&final_timestamp, &sleep_timestamp);

    // busy-wait loop: 
    // keeps polling the current time until it reaches or surpasses the final_timestamp
    current_timestamp = begin_timestamp;
    while(timespec_cmp(&current_timestamp, &final_timestamp)<0) {
        clock_gettime(CLOCK_MONOTONIC, &current_timestamp);
    }

    get_clocks(end);
    return end-start;
}








/* Utility function to add two timespec structures together. The input
 * parameter a is updated with the result of the sum. */
void timespec_add (struct timespec * a, struct timespec * b)
{
	/* Try to add up the nsec and see if we spill over into the
	 * seconds */
	time_t addl_seconds = b->tv_sec;
	a->tv_nsec += b->tv_nsec;
	if (a->tv_nsec > NANO_IN_SEC) {
		addl_seconds += a->tv_nsec / NANO_IN_SEC;
		a->tv_nsec = a->tv_nsec % NANO_IN_SEC;
	}
	a->tv_sec += addl_seconds;
}

/* Utility function to compare two timespec structures. It returns 1
 * if a is in the future compared to b; -1 if b is in the future
 * compared to a; 0 if they are identical. */
int timespec_cmp(struct timespec *a, struct timespec *b)
{
	if(a->tv_sec == b->tv_sec && a->tv_nsec == b->tv_nsec) {
		return 0;
	} else if((a->tv_sec > b->tv_sec) ||
		  (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec)) {
		return 1;
	} else {
		return -1;
	}
}

/* Busywait for the amount of time described via the delay
 * parameter */
uint64_t busywait_timespec(struct timespec delay)
{
	/* IMPLEMENT ME! (Optional but useful) */
}
