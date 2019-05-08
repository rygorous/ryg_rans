// Simple byte-aligned rANS encoder/decoder - public domain - Fabian 'ryg' Giesen 2014
//
// Not intended to be "industrial strength"; just meant to illustrate the general
// idea.

#pragma once

#include <stdint.h>

#ifdef assert
#define Rans32Assert assert
#else
#define Rans32Assert(x)
#endif

// READ ME FIRST:
//
// This is designed like a typical arithmetic coder API, but there's three
// twists you absolutely should be aware of before you start hacking:
//
// 1. You need to encode data in *reverse* - last symbol first. rANS works
//    like a stack: last in, first out.
// 2. Likewise, the encoder outputs bytes *in reverse* - that is, you give
//    it a pointer to the *end* of your buffer (exclusive), and it will
//    slowly move towards the beginning as more bytes are emitted.
// 3. Unlike basically any other entropy coder implementation you might
//    have used, you can interleave data from multiple independent rANS
//    encoders into the same bytestream without any extra signaling;
//    you can also just write some bytes by yourself in the middle if
//    you want to. This is in addition to the usual arithmetic encoder
//    property of being able to switch models on the fly. Writing raw
//    bytes can be useful when you have some data that you know is
//    incompressible, and is cheaper than going through the rANS encode
//    function. Using multiple rANS coders on the same byte stream wastes
//    a few bytes compared to using just one, but execution of two
//    independent encoders can happen in parallel on superscalar and
//    Out-of-Order CPUs, so this can be *much* faster in tight decoding
//    loops.
//
//    This is why all the rANS functions take the write pointer as an
//    argument instead of just storing it in some context struct.

// --------------------------------------------------------------------------

// L ('l' in the paper) is the lower bound of our normalization interval.
// Between this and our byte-aligned emission, we use 31 (not 32!) bits.
// This is done intentionally because exact reciprocals for 31-bit uints
// fit in 32-bit uints: this permits some optimizations during encoding.
#define RANS32_L (1u << 16)  // lower bound of our normalization interval

// State for a rANS encoder. Yep, that's all there is to it.
typedef uint32_t Rans32State;

// Initialize a rANS encoder.
static inline void Rans32EncInit(Rans32State* r)
{
    *r = RANS32_L;
}

// Encodes a single symbol with range start "start" and frequency "freq".
// All frequencies are assumed to sum to "1 << scale_bits", and the
// resulting bytes get written to ptr (which is updated).
//
// NOTE: With rANS, you need to encode symbols in *reverse order*, i.e. from
// beginning to end! Likewise, the output bytestream is written *backwards*:
// ptr starts pointing at the end of the output buffer and keeps decrementing.
static inline void Rans32EncPut(Rans32State* r, uint16_t** pptr, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    uint32_t x = *r;
    if (x >= ((RANS32_L >> scale_bits) << 16) * freq) {
        *pptr -= 1;
        **pptr = (uint16_t) (x & 0xffff);
        x >>= 16;
    }

    // x = C(s,x)
    *r = ((x / freq) << scale_bits) + (x % freq) + start;
}

// Flushes the rANS encoder.
static inline void Rans32EncFlush(Rans32State* r, uint16_t** pptr)
{
    uint32_t x = *r;
    uint16_t* ptr = *pptr;

    ptr -= 2;
    ptr[0] = (uint16_t) (x >> 0);
    ptr[1] = (uint16_t) (x >> 16);

    *pptr = ptr;
}

// Initializes a rANS decoder.
// Unlike the encoder, the decoder works forwards as you'd expect.
static inline void Rans32DecInit(Rans32State* r, uint16_t** pptr)
{
    uint32_t x;
    uint16_t* ptr = *pptr;

    x  = ptr[0] << 0;
    x |= ptr[1] << 16;
    ptr += 2;

    *pptr = ptr;
    *r = x;
}

// Returns the current cumulative frequency (map it to a symbol yourself!)
static inline uint32_t Rans32DecGet(Rans32State* r, uint32_t scale_bits)
{
    return *r & ((1u << scale_bits) - 1);
}

// Advances in the bit stream by "popping" a single symbol with range start
// "start" and frequency "freq". All frequencies are assumed to sum to "1 << scale_bits",
// and the resulting bytes get written to ptr (which is updated).
static inline void Rans32DecAdvance(Rans32State* r, uint16_t** pptr, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    uint32_t mask = (1u << scale_bits) - 1;

    // s, x = D(x)
    uint32_t x = *r;
    x = freq * (x >> scale_bits) + (x & mask) - start;

    // renormalize
    if (x < RANS32_L) {
    	uint16_t* ptr = *pptr;
        do x = (x << 16) | *ptr++; while (x < RANS32_L);
        *pptr = ptr;
    }

    *r = x;
}

// --------------------------------------------------------------------------

// That's all you need for a full encoder; below here are some utility
// functions with extra convenience or optimizations.

// Encoder symbol description
// This (admittedly odd) selection of parameters was chosen to make
// RansEncPutSymbol as cheap as possible.
typedef struct {
    uint32_t x_max;     // (Exclusive) upper bound of pre-normalization interval
    uint32_t rcp_freq;  // Fixed-point reciprocal frequency
    uint32_t bias;      // Bias
    uint16_t cmpl_freq; // Complement of frequency: (1 << scale_bits) - freq
    uint16_t rcp_shift; // Reciprocal shift
} Rans32EncSymbol;

// Decoder symbols are straightforward.
typedef struct {
    uint32_t start;     // Start of range.
    uint32_t freq;      // Symbol frequency.
} Rans32DecSymbol;

// Initializes an encoder symbol to start "start" and frequency "freq"
static inline void Rans32EncSymbolInit(Rans32EncSymbol* s, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    Rans32Assert(scale_bits <= 16);
    Rans32Assert(start <= (1u << scale_bits));
    Rans32Assert(freq <= (1u << scale_bits) - start);

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

    s->x_max = ((RANS32_L >> scale_bits) << 16) * freq;
    s->cmpl_freq = (uint16_t) ((1 << scale_bits) - freq);
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
        s->rcp_freq = ~0u;
        s->rcp_shift = 0;
        s->bias = start + (1 << scale_bits) - 1;
    } else {
        // Alverson, "Integer Division using reciprocals"
        // shift=ceil(log2(freq))
        uint32_t shift = 0;
        while (freq > (1u << shift))
            shift++;

        s->rcp_freq = (uint32_t) (((1ull << (shift + 31)) + freq-1) / freq);
        s->rcp_shift = shift - 1;

        // With these values, 'q' is the correct quotient, so we
        // have bias=start.
        s->bias = start;
    }
}

// Initialize a decoder symbol to start "start" and frequency "freq"
static inline void Rans32DecSymbolInit(Rans32DecSymbol* s, uint32_t start, uint32_t freq)
{
//    Rans32Assert(start <= (1 << 16));
//    Rans32Assert(freq <= (1 << 16) - start);
    s->start = (uint32_t) start;
    s->freq = (uint32_t) freq;
}

// Encodes a given symbol. This is faster than straight RansEnc since we can do
// multiplications instead of a divide.
//
// See Rans32EncSymbolInit for a description of how this works.
static inline void Rans32EncPutSymbol(Rans32State* r, uint16_t** pptr, Rans32EncSymbol const* sym)
{
    Rans32Assert(sym->x_max != 0); // can't encode symbol with freq=0

    // renormalize
    uint32_t x = *r;
    uint32_t x_max = sym->x_max;
    if (x >= x_max) {
        *pptr -= 1;
        **pptr = (uint16_t) (x & 0xffff);
        x >>= 16;
    }

    // x = C(s,x)
    // NOTE: written this way so we get a 32-bit "multiply high" when
    // available. If you're on a 64-bit platform with cheap multiplies
    // (e.g. x64), just bake the +32 into rcp_shift.
    uint32_t q = (uint32_t) (((uint64_t)x * sym->rcp_freq) >> 32) >> sym->rcp_shift;
    *r = x + sym->bias + q * sym->cmpl_freq;
}

// Equivalent to Rans32DecAdvance that takes a symbol.
static inline void Rans32DecAdvanceSymbol(Rans32State* r, uint16_t** pptr, Rans32DecSymbol const* sym, uint32_t scale_bits)
{
    Rans32DecAdvance(r, pptr, sym->start, sym->freq, scale_bits);
}

// Advances in the bit stream by "popping" a single symbol with range start
// "start" and frequency "freq". All frequencies are assumed to sum to "1 << scale_bits".
// No renormalization or output happens.
static inline void Rans32DecAdvanceStep(Rans32State* r, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    uint32_t mask = (1u << scale_bits) - 1;

    // s, x = D(x)
    uint32_t x = *r;
    *r = freq * (x >> scale_bits) + (x & mask) - start;
}

// Equivalent to Rans32DecAdvanceStep that takes a symbol.
static inline void Rans32DecAdvanceSymbolStep(Rans32State* r, Rans32DecSymbol const* sym, uint32_t scale_bits)
{
    Rans32DecAdvanceStep(r, sym->start, sym->freq, scale_bits);
}

// Renormalize.
static inline void Rans32DecRenorm(Rans32State* r, uint16_t** pptr)
{
    uint32_t x = *r;
    if (x < RANS32_L) {
        *r = (x << 16) | **pptr;
        *pptr += 1;
    }
}

#pragma once
