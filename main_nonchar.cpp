#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <string>

#include "rans64.h"
#include "helper.h"
//#include "SymbolStats.h"
// This is just the sample program. All the meat is in rans_byte.h.

int main(int argc, char* argv[])
{
    std::string filename{"book1"};
    if (argc > 1)
    {
        filename = argv[1];
    }

    printf("Compressing file: %s\n", filename.c_str());

    size_t in_size;
    uint8_t* in_bytes = read_file(filename.c_str(), &in_size);

    static const uint32_t prob_bits = 14;
    static const uint32_t prob_scale = 1 << prob_bits;

    SymbolStats stats;
    stats.count_freqs(in_bytes, in_size);
    stats.normalize_freqs(prob_scale);

    // cumlative->symbol table
    // this is super brute force
    uint8_t cum2sym[prob_scale];
    for (int s=0; s < 256; s++)
        for (uint32_t i=stats.cum_freqs[s]; i < stats.cum_freqs[s+1]; i++)
            cum2sym[i] = s;

    static size_t out_max_size = 32<<20; // 32MB
    static size_t out_max_elems = out_max_size / sizeof(uint32_t);
    uint32_t* out_buf = new uint32_t[out_max_elems];
    uint32_t* out_end = out_buf + out_max_elems;
    uint8_t* dec_bytes = new uint8_t[in_size];

    // try rANS encode
    uint32_t *rans_begin;
    Rans64EncSymbol esyms[256];
    Rans64DecSymbol dsyms[256];

    for (int i=0; i < 256; i++) {
        Rans64EncSymbolInit(&esyms[i], stats.cum_freqs[i], stats.freqs[i], prob_bits);
        Rans64DecSymbolInit(&dsyms[i], stats.cum_freqs[i], stats.freqs[i]);
    }

    // ---- regular rANS encode/decode. Typical usage.

    memset(dec_bytes, 0xcc, in_size);

    printf("rANS encode:\n");
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        Rans64State rans;
        Rans64EncInit(&rans);

        uint32_t* ptr = out_end; // *end* of output buffer
        for (size_t i=in_size; i > 0; i--) { // NB: working in reverse!
            int s = in_bytes[i-1];
            Rans64EncPutSymbol(&rans, &ptr, &esyms[s], prob_bits);
        }
        Rans64EncFlush(&rans, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", enc_clocks, 1.0 * enc_clocks / in_size, 1.0 * in_size / (enc_time * 1048576.0));
    }
    printf("rANS: %d bytes\n", (int) ((out_end - rans_begin) * sizeof(uint32_t)));

    // try rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        Rans64State rans;
        uint32_t* ptr = rans_begin;
        Rans64DecInit(&rans, &ptr);

        for (size_t i=0; i < in_size; i++) {
            uint32_t s = cum2sym[Rans64DecGet(&rans, prob_bits)];
            dec_bytes[i] = (uint8_t) s;
            Rans64DecAdvanceSymbol(&rans, &ptr, &dsyms[s], prob_bits);
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", dec_clocks, 1.0 * dec_clocks / in_size, 1.0 * in_size / (dec_time * 1048576.0));
    }

    // check decode results
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    // ---- interleaved rANS encode/decode. This is the kind of thing you might do to optimize critical paths.

    memset(dec_bytes, 0xcc, in_size);

    // try interleaved rANS encode
    printf("\ninterleaved rANS encode:\n");
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        Rans64State rans0, rans1;
        Rans64EncInit(&rans0);
        Rans64EncInit(&rans1);

        uint32_t* ptr = out_end;

        // odd number of bytes?
        if (in_size & 1) {
            int s = in_bytes[in_size - 1];
            Rans64EncPutSymbol(&rans0, &ptr, &esyms[s], prob_bits);
        }

        for (size_t i=(in_size & ~1); i > 0; i -= 2) { // NB: working in reverse!
            int s1 = in_bytes[i-1];
            int s0 = in_bytes[i-2];
            Rans64EncPutSymbol(&rans1, &ptr, &esyms[s1], prob_bits);
            Rans64EncPutSymbol(&rans0, &ptr, &esyms[s0], prob_bits);
        }
        Rans64EncFlush(&rans1, &ptr);
        Rans64EncFlush(&rans0, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", enc_clocks, 1.0 * enc_clocks / in_size, 1.0 * in_size / (enc_time * 1048576.0));
    }
    printf("interleaved rANS: %d bytes\n", (int) ((out_end - rans_begin) * sizeof(uint32_t)));

    // try interleaved rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        Rans64State rans0, rans1;
        uint32_t* ptr = rans_begin;
        Rans64DecInit(&rans0, &ptr);
        Rans64DecInit(&rans1, &ptr);

        for (size_t i=0; i < (in_size & ~1); i += 2) {
            uint32_t s0 = cum2sym[Rans64DecGet(&rans0, prob_bits)];
            uint32_t s1 = cum2sym[Rans64DecGet(&rans1, prob_bits)];
            dec_bytes[i+0] = (uint8_t) s0;
            dec_bytes[i+1] = (uint8_t) s1;
            Rans64DecAdvanceSymbolStep(&rans0, &dsyms[s0], prob_bits);
            Rans64DecAdvanceSymbolStep(&rans1, &dsyms[s1], prob_bits);
            Rans64DecRenorm(&rans0, &ptr);
            Rans64DecRenorm(&rans1, &ptr);
        }

        // last byte, if number of bytes was odd
        if (in_size & 1) {
            uint32_t s0 = cum2sym[Rans64DecGet(&rans0, prob_bits)];
            dec_bytes[in_size - 1] = (uint8_t) s0;
            Rans64DecAdvanceSymbol(&rans0, &ptr, &dsyms[s0], prob_bits);
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1fMB/s)\n", dec_clocks, 1.0 * dec_clocks / in_size, 1.0 * in_size / (dec_time * 1048576.0));
    }

    // check decode results
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    delete[] out_buf;
    delete[] dec_bytes;
    delete[] in_bytes;
    return 0;
}
