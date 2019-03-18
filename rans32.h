// 64-bit rANS encoder/decoder - public domain - Fabian 'ryg' Giesen 2014
//
// This uses 64-bit states (63-bit actually) which allows renormalizing
// by writing out a whole 32 bits at a time (b=2^32) while still
// retaining good precision and allowing for high probability resolution.
//
// The only caveat is that this version requires 64-bit arithmetic; in
// particular, the encoder approximation in the bottom half requires a
// fast way to obtain the top 64 bits of an unsigned 64*64 bit product.
// 
// In short, as written, this code works on 64-bit targets only!

#ifndef Rans32_HEADER
#define Rans32_HEADER

#include <stdint.h>

#ifdef assert
#define Rans32Assert assert
#else
#define Rans32Assert(x)
#endif

// --------------------------------------------------------------------------

// This code needs support for 64-bit long multiplies with 128-bit result
// (or more precisely, the top 64 bits of a 128-bit result). This is not
// really portable functionality, so we need some compiler-specific hacks
// here.

#if defined(__GNUC__)

static inline uint32_t Rans32MulHi(uint32_t a, uint32_t b)
{
    return (uint32_t) (((uint64_t)a * b) >> 32);
}

#else

#error Unknown/unsupported compiler!

#endif

// --------------------------------------------------------------------------

// L ('l' in the paper) is the lower bound of our normalization interval.
// Between this and our 32-bit-aligned emission, we use 63 (not 64!) bits.
// This is done intentionally because exact reciprocals for 63-bit uints
// fit in 64-bit uints: this permits some optimizations during encoding.
#define RANS32_L (1ull << 16)  // lower bound of our normalization interval

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
static inline void Rans32EncPut(Rans32State* r, uint32_t** pptr, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    Rans32Assert(freq != 0);

    // renormalize (never needs to loop)
    uint32_t x = *r;
    uint32_t x_max = ((RANS32_L >> scale_bits) << 16) * freq; // this turns into a shift.
    if (x >= x_max) {
        *pptr -= 1;
        **pptr = (uint32_t) x;
        x >>= 16;
        Rans32Assert(x < x_max);
    }

    // x = C(s,x)
    *r = ((x / freq) << scale_bits) + (x % freq) + start;
}

// Flushes the rANS encoder.
static inline void Rans32EncFlush(Rans32State* r, uint32_t** pptr)
{
    uint32_t x = *r;

    *pptr -= 2;
    (*pptr)[0] = (uint32_t) (x >> 0);
    (*pptr)[1] = (uint32_t) (x >> 16);
}

// Initializes a rANS decoder.
// Unlike the encoder, the decoder works forwards as you'd expect.
static inline void Rans32DecInit(Rans32State* r, uint32_t** pptr)
{
    uint32_t x;

    x  = (uint32_t) ((*pptr)[0]) << 0;
    x |= (uint32_t) ((*pptr)[1]) << 16;
    *pptr += 2;
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
static inline void Rans32DecAdvance(Rans32State* r, uint32_t** pptr, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    uint32_t mask = (1ull << scale_bits) - 1;

    // s, x = D(x)
    uint32_t x = *r;
    x = freq * (x >> scale_bits) + (x & mask) - start;

    // renormalize
    if (x < RANS32_L) {
        x = (x << 16) | **pptr;
        *pptr += 1;
        Rans32Assert(x >= RANS32_L);
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
    uint32_t rcp_freq;  // Fixed-point reciprocal frequency
    uint32_t freq;      // Symbol frequency
    uint32_t bias;      // Bias
    uint32_t cmpl_freq; // Complement of frequency: (1 << scale_bits) - freq
    uint32_t rcp_shift; // Reciprocal shift
} Rans32EncSymbol;

// Decoder symbols are straightforward.
typedef struct {
    uint32_t start;     // Start of range.
    uint32_t freq;      // Symbol frequency.
} Rans32DecSymbol;

// Initializes an encoder symbol to start "start" and frequency "freq"
static inline void Rans32EncSymbolInit(Rans32EncSymbol* s, uint32_t start, uint32_t freq, uint32_t scale_bits)
{
    Rans32Assert(scale_bits <= 15);
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

    s->freq = freq;
    s->cmpl_freq = ((1 << scale_bits) - freq);
    if (freq < 2) {
        // freq=0 symbols are never valid to encode, so it doesn't matter what
        // we set our values to.
        //
        // freq=1 is tricky, since the reciprocal of 1 is 1; unfortunately,
        // our fixed-point reciprocal approximation can only multiply by values
        // smaller than 1.
        //
        // So we use the "next best thing": rcp_freq=~0, rcp_shift=0.
        // This gives:
        //   q = mul_hi(x, rcp_freq) >> rcp_shift
        //     = mul_hi(x, (1<<64) - 1)) >> 0
        //     = floor(x - x/(2^64))
        //     = x - 1 if 1 <= x < 2^64
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
        uint32_t x0, x1, t0, t1;
        while (freq > (1u << shift))
            shift++;

        // long divide ((uint128) (1 << (shift + 63)) + freq-1) / freq
        // by splitting it into two 64:64 bit divides (this works because
        // the dividend has a simple form.)
        x0 = freq - 1;
        x1 = 1ull << (shift + 15);

        t1 = x1 / freq;
        x0 += (x1 % freq) << 16;
        t0 = x0 / freq;

        s->rcp_freq = t0 + (t1 << 16);
        s->rcp_shift = shift - 1;

        // With these values, 'q' is the correct quotient, so we
        // have bias=start.
        s->bias = start;
    }
}

// Initialize a decoder symbol to start "start" and frequency "freq"
static inline void Rans32DecSymbolInit(Rans32DecSymbol* s, uint32_t start, uint32_t freq)
{
    Rans32Assert(start <= static_cast<uint32_t>((1 << 15)));
    Rans32Assert(freq <= (1 << 15) - start);
    s->start = start;
    s->freq = freq;
}

// Encodes a given symbol. This is faster than straight RansEnc since we can do
// multiplications instead of a divide.
//
// See RansEncSymbolInit for a description of how this works.
static inline void Rans32EncPutSymbol(Rans32State* r, uint32_t** pptr, Rans32EncSymbol const* sym, uint32_t scale_bits)
{
    Rans32Assert(sym->freq != 0); // can't encode symbol with freq=0

    // renormalize
    uint32_t x = *r;
    uint32_t x_max = ((RANS32_L >> scale_bits) << 16) * sym->freq; // turns into a shift
    if (x >= x_max) {
        *pptr -= 1;
        **pptr = (uint32_t) x;
        x >>= 16;
    }

    // x = C(s,x)
    uint32_t q = Rans32MulHi(x, sym->rcp_freq) >> sym->rcp_shift;
    *r = x + sym->bias + q * sym->cmpl_freq;
}

// Equivalent to RansDecAdvance that takes a symbol.
static inline void Rans32DecAdvanceSymbol(Rans32State* r, uint32_t** pptr, Rans32DecSymbol const* sym, uint32_t scale_bits)
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

// Equivalent to RansDecAdvanceStep that takes a symbol.
static inline void Rans32DecAdvanceSymbolStep(Rans32State* r, Rans32DecSymbol const* sym, uint32_t scale_bits)
{
    Rans32DecAdvanceStep(r, sym->start, sym->freq, scale_bits);
}

// Renormalize.
static inline void Rans32DecRenorm(Rans32State* r, uint32_t** pptr)
{
    // renormalize
    uint32_t x = *r;
    if (x < RANS32_L) {
        x = (x << 16) | **pptr;
        *pptr += 1;
        Rans32Assert(x >= RANS32_L);
    }

    *r = x;
}

#endif // Rans32_HEADER
