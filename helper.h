#ifndef HELPER_H_
#define HELPER_H_

#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <string>

void panic(const char *fmt, ...);

uint8_t* read_file(char const* filename, size_t* out_size);

struct SymbolStats
{
    uint32_t freqs[256];
    uint32_t cum_freqs[257];

    void count_freqs(uint8_t const* in, size_t nbytes);
    void calc_cum_freqs();
    void normalize_freqs(uint32_t target_total);
};

#endif /* HELPER_H_ */
