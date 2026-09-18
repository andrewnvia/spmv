#pragma once

/* Timed loop: run for about TIME_LIMIT seconds, but never fewer than MIN_ITER
 * or more than MAX_ITER iterations. See pick_iterations() in timer.h. */
#define TIME_LIMIT 3.0
#define MIN_ITER   10
#define MAX_ITER   800

// #define TESTING
