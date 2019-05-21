/*
 * EncSymbol.h
 *
 *  Created on: May 21, 2019
 *      Author: Michael Lettrich (michael.lettrich@cern.ch)
 */

#pragma once

#include <cstdint>
#include <cassert>

#include "helper.h"

namespace rans{

// Encoder symbol description
// This (admittedly odd) selection of parameters was chosen to make
// RansEncPutSymbol as cheap as possible.
template <typename T>
struct EncSymbol
{
	EncSymbol(uint32_t start, uint32_t freq, uint32_t scale_bits)
	{
		//TODO(lettrich): a check should be definitely done here.
		//		RansAssert(scale_bits <= 16);
		assert(start <= (1u << scale_bits));
		assert(freq <= (1u << scale_bits) - start);

		// Say M := 1 << scale_bits.
		//
		// The original encoder does:
		//   x_new = (x/freq)*M + start + (x%freq)
		//
		// The fast encoder does (schematically):
		//   q     = mul_hi(x, rcp_freq) >> rcp_shift   (division)
		//   r     = x - q*freq                         (remainder)
		//   x_new = q*M + bias + r                     (new x)
		// plugging in r into x_new yields:
		//   x_new = bias + x + q*(M - freq)
		//        =: bias + x + q*cmpl_freq             (*)
		//
		// and we can just precompute cmpl_freq. Now we just need to
		// set up our parameters such that the original encoder and
		// the fast encoder agree.

		this->freq = freq;
		this->cmpl_freq = static_cast<T>((1 << scale_bits) - freq);
		if (freq < 2) {
			// freq=0 symbols are never valid to encode, so it doesn't matter what
			// we set our values to.
			//
			// freq=1 is tricky, since the reciprocal of 1 is 1; unfortunately,
			// our fixed-point reciprocal approximation can only multiply by values
			// smaller than 1.
			//
			// So we use the "next best thing": rcp_freq=0xffffffff, rcp_shift=0.
			// This gives:
			//   q = mul_hi(x, rcp_freq) >> rcp_shift
			//     = mul_hi(x, (1<<32) - 1)) >> 0
			//     = floor(x - x/(2^32))
			//     = x - 1 if 1 <= x < 2^32
			// and we know that x>0 (x=0 is never in a valid normalization interval).
			//
			// So we now need to choose the other parameters such that
			//   x_new = x*M + start
			// plug it in:
			//     x*M + start                   (desired result)
			//   = bias + x + q*cmpl_freq        (*)
			//   = bias + x + (x - 1)*(M - 1)    (plug in q=x-1, cmpl_freq)
			//   = bias + 1 + (x - 1)*M
			//   = x*M + (bias + 1 - M)
			//
			// so we have start = bias + 1 - M, or equivalently
			//   bias = start + M - 1.
			this->rcp_freq = static_cast<T>(~0ul);
			this->rcp_shift = 0;
			this->bias = start + (1 << scale_bits) - 1;
		} else {
			// Alverson, "Integer Division using reciprocals"
			// shift=ceil(log2(freq))
			uint32_t shift = 0;
			while (freq > (1u << shift))
				shift++;

			if constexpr (needs64Bit<T>()){
				uint64_t x0, x1, t0, t1;
				// long divide ((uint128) (1 << (shift + 63)) + freq-1) / freq
				// by splitting it into two 64:64 bit divides (this works because
				// the dividend has a simple form.)
				x0 = freq - 1;
				x1 = 1ull << (shift + 31);

				t1 = x1 / freq;
				x0 += (x1 % freq) << 32;
				t0 = x0 / freq;

				this->rcp_freq = t0 + (t1 << 32);
			}else{
				this->rcp_freq = static_cast<uint32_t>(((1ull << (shift + 31)) + freq-1) / freq);
			}
			this->rcp_shift = shift - 1;

			// With these values, 'q' is the correct quotient, so we
			// have bias=start.
			this->bias = start;
		}
	};


	T rcp_freq;  		// Fixed-point reciprocal frequency
	uint32_t freq;     	// (Exclusive) upper bound of pre-normalization interval
	uint32_t bias;      // Bias
	uint32_t cmpl_freq; // Complement of frequency: (1 << scale_bits) - freq
	uint32_t rcp_shift; // Reciprocal shift
};

}//namespace rans


