/*******************************************************************************
* CPU Clock Measurement Using RDTSC
*
* Description:
*     This C file provides functions to compute and measure the CPU clock using
*     the `rdtsc` instruction. The `rdtsc` instruction returns the Time Stamp
*     Counter, which can be used to measure CPU clock cycles.
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
*     Ensure that the platform supports the `rdtsc` instruction before using
*     these functions. Depending on the CPU architecture and power-saving
*     modes, the results might vary. Always refer to the CPU's official
*     documentation for accurate interpretations.
*
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "timelib.h"

int main(int argc, char **argv) 
// argc: the number of command-line arguments, including the program's name.
// argv: an array of strings representing the command-line arguments.
{
	/* IMPLEMENT ME! */

    long sec = atol(argv[1]);  // 2nd arg is sec
    long nsec = atol(argv[2]);  // 3rd arg is nsec
    char proc_to_measure = argv[3][0];  // 4th arg is procedure, which should only be a char
    char *proc_to_print; // a string of procedure to print at the end

    uint64_t time_elapsed;  // number of clock cycles elapsed

    if (proc_to_measure == 's') {   // sleep-based waiting
        time_elapsed = get_elapsed_sleep(sec, nsec);
        proc_to_print = "SLEEP";
    } else if (proc_to_measure == 'b') {    // busy-waiting
        time_elapsed = get_elapsed_busywait(sec, nsec);
        proc_to_print = "BUSYWAIT";
    }

    double clock_speed = (double)time_elapsed / (sec * 1000000 + nsec / 1000.0); // convert to MHz

    printf("WaitMethod:%s\n", proc_to_print);
    printf("WaitTime:%ld %ld\n", sec, nsec);
    printf("ClocksElapsed:%lu\n", time_elapsed);  // number of clock cycles that have elapsed.
    printf("ClockSpeed:%.2f\n", clock_speed);  // the calculated clock speed in MHz.

    return EXIT_SUCCESS;
}
