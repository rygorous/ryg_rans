// Just some platform utilities.
#pragma once

// x86 intrinsics (__rdtsc etc.)
#include <x86intrin.h>
#define ALIGNSPEC(type,name,alignment) type name __attribute__((aligned(alignment)))

// Timer
#define __STDC_FORMAT_MACROS
#include <time.h>
#include <inttypes.h>
#include <assert.h>

static inline double timer()
{
    timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    int status = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(status == 0);
    return double(ts.tv_sec) + 1.0e-9 * double(ts.tv_nsec);
}
