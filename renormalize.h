#ifndef RENORMALIZE_H_INCLUDED
#define RENORMALIZE_H_INCLUDED

// Optimal renormalization of a frequency table down to a more coarse-grained
// table; through a combination of heuristics and memoization, finds the
// rounding that produces the fewest amount of bits needed to encode, while
// making sure no symbol gets a frequency of zero. Primarily useful for when
// your precision is low enough that loss is a real problem; compared to direct
// rounding, it tends to cut the overhead about in half.
//
// This operation is cheap but not free; it seems to use 1–2 ms on a Haswell 2.1 GHz
// for 256 symbols (the speed is mostly dependent on number of symbols, although
// number of bits also matters some).

#include <stdint.h>

void OptimalRenormalize(uint32_t *cum_freqs, uint32_t num_syms, uint32_t target_total);

#endif // RENORMALIZE_H_INCLUDED
