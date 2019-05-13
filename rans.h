/*
 * rans.h
 *
 *  Created on: May 10, 2019
 *      Author: Michael Lettrich (michael.lettrich@cern.ch)
 */

#pragma once
#include <vector>
#include <stdint.h>

#ifdef assert
#define RansAssert assert
#else
#define RansAssert(x)
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
#define RANS32_L (1u << 23)  // lower bound of our normalization interval

// State for a rANS encoder. Yep, that's all there is to it.
template<typename T>
using RansState = T;

// Initialize a rANS encoder.
template<typename T>
void Rans32EncInit(RansState<T>* r);

// Renormalize the encoder. Internal function.
template<typename T, typename P>
RansState<T> Rans32EncRenorm(RansState<T> x, P** pptr, uint32_t freq, uint32_t scale_bits);


// Encodes a single symbol with range start "start" and frequency "freq".
// All frequencies are assumed to sum to "1 << scale_bits", and the
// resulting bytes get written to ptr (which is updated).
//
// NOTE: With rANS, you need to encode symbols in *reverse order*, i.e. from
// beginning to end! Likewise, the output bytestream is written *backwards*:
// ptr starts pointing at the end of the output buffer and keeps decrementing.
template<typename T, typename P>
void RansEncPut(RansState<T>* r, P** pptr, uint32_t start, uint32_t freq, uint32_t scale_bits);


// Flushes the rANS encoder.
template<typename T, typename P>
void RansEncFlush(RansState<T>* r, P** pptr);

// Initializes a rANS decoder.
// Unlike the encoder, the decoder works forwards as you'd expect.
template<typename T, typename P>
void RansDecInit(RansState<T>* r, P** pptr);


// Returns the current cumulative frequency (map it to a symbol yourself!)
template<typename T>
uint32_t RansDecGet(RansState<T>* r, uint32_t scale_bits);

// Advances in the bit stream by "popping" a single symbol with range start
// "start" and frequency "freq". All frequencies are assumed to sum to "1 << scale_bits",
// and the resulting bytes get written to ptr (which is updated).
template<typename T, typename P>
void RansDecAdvance(RansState<T>* r, P** pptr, uint32_t start, uint32_t freq, uint32_t scale_bits);

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
} RansEncSymbol;

// Decoder symbols are straightforward.
typedef struct {
    uint16_t start;     // Start of range.
    uint16_t freq;      // Symbol frequency.
} RansDecSymbol;

// Initializes an encoder symbol to start "start" and frequency "freq"
template<typename T>
void RansEncSymbolInit(RansState<T>* s, uint32_t start, uint32_t freq, uint32_t scale_bits);

// Initialize a decoder symbol to start "start" and frequency "freq"
template<typename T>
void RansDecSymbolInit(RansState<T>* s, uint32_t start, uint32_t freq);

// Encodes a given symbol. This is faster than straight RansEnc since we can do
// multiplications instead of a divide.
//
// See Rans32EncSymbolInit for a description of how this works.
template<typename T, typename P>
void RansEncPutSymbol(RansState<T>* r, P** pptr, RansEncSymbol const* sym, uint32_t scale_bits);

// Equivalent to Rans32DecAdvance that takes a symbol.
template<typename T, typename P>
void RansDecAdvanceSymbol(RansState<T>* r, P** pptr, RansDecSymbol const* sym, uint32_t scale_bits);

// Advances in the bit stream by "popping" a single symbol with range start
// "start" and frequency "freq". All frequencies are assumed to sum to "1 << scale_bits".
// No renormalization or output happens.
template<typename T>
void RansDecAdvanceStep(RansState<T>* r, uint32_t start, uint32_t freq, uint32_t scale_bits);

// Equivalent to Rans32DecAdvanceStep that takes a symbol.
template<typename T>
void RansDecAdvanceSymbolStep(RansState<T>* r, RansDecSymbol const* sym, uint32_t scale_bits);

// Renormalize.
template<typename T, typename P>
void RansDecRenorm(RansState<T>* r, P** pptr);


