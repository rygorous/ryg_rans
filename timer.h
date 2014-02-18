#ifndef TIMER_HEADER
#define TIMER_HEADER

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static inline double timer()
{
    LARGE_INTEGER ctr, freq;
    QueryPerformanceCounter(&ctr);
    QueryPerformanceFrequency(&freq);
    return 1.0 * ctr.QuadPart / freq.QuadPart;
}
#elif defined(__linux__)
#include <time.h>
static inline double timer()
{
    timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    int status = clock_gettime(CLOCK_MONOTONIC, &ts);
    assert(status == 0);
    return double(ts.tv_sec) + 1.0e-9 * double(ts.tv_nsec);
}
#else
#error OS-specific code needed
#endif

#endif // TIMER_HEADER
