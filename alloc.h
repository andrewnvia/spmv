
#pragma once

#include <stdlib.h>

/* malloc for arrays that are allowed to be empty -- a 0x0 matrix, or an ELL
 * conversion of a matrix with no nonzeros, where malloc(0) may return NULL and
 * that is indistinguishable from failure. Ask for one byte instead, so NULL
 * still means only "out of memory".
 *
 * Must stay `static inline` -- several translation units include this. */
static inline void * alloc_array(size_t count, size_t size)
{
    size_t bytes = count * size;
    return malloc(bytes > 0 ? bytes : 1);
}
