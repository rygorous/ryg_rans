// Some common, OS-dependent, utility functions

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

double timer()
{
    LARGE_INTEGER ctr, freq;
    QueryPerformanceCounter(&ctr);
    QueryPerformanceFrequency(&freq);
    return 1.0 * ctr.QuadPart / freq.QuadPart;
}
#define rdtsc __rdtsc

#else
#include <sys/time.h>
typedef struct timeval MyClock;
double timer() {
  MyClock t;
  gettimeofday(&t, NULL);
  return t.tv_sec + t.tv_usec / 1000000.0;
}
uint64_t rdtsc() {
  uint64_t x;
  __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
  return x;
}

#endif

