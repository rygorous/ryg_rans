#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "rans_word_sse41.h"

// This is just the sample program. All the meat is in rans_byte.h.

static void panic(const char *fmt, ...)
{
    va_list arg;

    va_start(arg, fmt);
    fputs("Error: ", stderr);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    fputs("\n", stderr);

    exit(1);
}

static uint8_t* read_file(char const* filename, size_t* out_size)
{
    FILE* f = fopen(filename, "rb");
    if (!f)
        panic("file not found: %s\n", filename);

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = new uint8_t[size];
    if (fread(buf, size, 1, f) != 1)
        panic("read failed\n");

    fclose(f);
    if (out_size)
        *out_size = size;

    return buf;
}

// ---- Stats

struct SymbolStats
{
    uint32_t freqs[256];
    uint32_t cum_freqs[257];

    void count_freqs(uint8_t const* in, size_t nbytes);
    void calc_cum_freqs();
    void normalize_freqs(uint32_t target_total);
};

void SymbolStats::count_freqs(uint8_t const* in, size_t nbytes)
{
    for (int i=0; i < 256; i++)
        freqs[i] = 0;

    for (size_t i=0; i < nbytes; i++)
        freqs[in[i]]++;
}

void SymbolStats::calc_cum_freqs()
{
    cum_freqs[0] = 0;
    for (int i=0; i < 256; i++)
        cum_freqs[i+1] = cum_freqs[i] + freqs[i];
}

void SymbolStats::normalize_freqs(uint32_t target_total)
{
    assert(target_total >= 256);
    
    calc_cum_freqs();
    uint32_t cur_total = cum_freqs[256];
    
    // resample distribution based on cumulative freqs
    for (int i = 1; i <= 256; i++)
        cum_freqs[i] = ((uint64_t)target_total * cum_freqs[i])/cur_total;

    // if we nuked any non-0 frequency symbol to 0, we need to steal
    // the range to make the frequency nonzero from elsewhere.
    //
    // this is not at all optimal, i'm just doing the first thing that comes to mind.
    for (int i=0; i < 256; i++) {
        if (freqs[i] && cum_freqs[i+1] == cum_freqs[i]) {
            // symbol i was set to zero freq

            // find best symbol to steal frequency from (try to steal from low-freq ones)
            uint32_t best_freq = ~0u;
            int best_steal = -1;
            for (int j=0; j < 256; j++) {
                uint32_t freq = cum_freqs[j+1] - cum_freqs[j];
                if (freq > 1 && freq < best_freq) {
                    best_freq = freq;
                    best_steal = j;
                }
            }
            assert(best_steal != -1);

            // and steal from it!
            if (best_steal < i) {
                for (int j = best_steal + 1; j <= i; j++)
                    cum_freqs[j]--;
            } else {
                assert(best_steal > i);
                for (int j = i + 1; j <= best_steal; j++)
                    cum_freqs[j]++;
            }
        }
    }

    // calculate updated freqs and make sure we didn't screw anything up
    assert(cum_freqs[0] == 0 && cum_freqs[256] == target_total);
    for (int i=0; i < 256; i++) {
        if (freqs[i] == 0)
            assert(cum_freqs[i+1] == cum_freqs[i]);
        else
            assert(cum_freqs[i+1] > cum_freqs[i]);

        // calc updated freq
        freqs[i] = cum_freqs[i+1] - cum_freqs[i];
    }
}

int main()
{
    size_t in_size;
    uint8_t* in_bytes = read_file("book1", &in_size);

    SymbolStats stats;
    stats.count_freqs(in_bytes, in_size);
    stats.normalize_freqs(RANS_WORD_M);

    // init decoding tables
    RansWordTables tab;
    for (int s=0; s < 256; s++)
        RansWordTablesInitSymbol(&tab, (uint8_t)s, stats.cum_freqs[s], stats.freqs[s]);

    size_t out_max_size = in_size + (in_size >> 3) + 128;
    uint8_t* out_buf = new uint8_t[out_max_size + 16]; // extra bytes at end
    uint8_t* dec_bytes = new uint8_t[in_size];

    // try rANS encode
    uint16_t *rans_begin;

    // ---- regular rANS encode/decode. Typical usage.

    memset(dec_bytes, 0xcc, in_size);

    printf("rANS encode:\n");
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        RansWordEnc rans = RansWordEncInit();

        uint16_t* ptr = (uint16_t *) (out_buf + out_max_size); // *end* of output buffer
        for (size_t i=in_size; i > 0; i--) { // NB: working in reverse!
            int s = in_bytes[i-1];
            RansWordEncPut(&rans, &ptr, stats.cum_freqs[s], stats.freqs[s]);
        }
        RansWordEncFlush(&rans, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%"PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", enc_clocks, 1.0 * enc_clocks / in_size, 1.0 * in_size / (enc_time * 1048576.0));
    }
    printf("rANS: %d bytes\n", (int) (out_buf + out_max_size - (uint8_t *)rans_begin));

    // try rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        RansWordDec rans;
        uint16_t* ptr = rans_begin;
        RansWordDecInit(&rans, &ptr);

        for (size_t i=0; i < in_size; i++) {
            uint8_t s = RansWordDecSym(&rans, &tab);
            dec_bytes[i] = (uint8_t) s;
            RansWordDecRenorm(&rans, &ptr);
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%"PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", dec_clocks, 1.0 * dec_clocks / in_size, 1.0 * in_size / (dec_time * 1048576.0));
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

        RansWordEnc rans0 = RansWordEncInit();
        RansWordEnc rans1 = RansWordEncInit();

        uint16_t* ptr = (uint16_t *)(out_buf + out_max_size); // *end* of output buffer

        // odd number of bytes?
        if (in_size & 1) {
            int s = in_bytes[in_size - 1];
            RansWordEncPut(&rans0, &ptr, stats.cum_freqs[s], stats.freqs[s]);
        }

        for (size_t i=(in_size & ~1); i > 0; i -= 2) { // NB: working in reverse!
            int s1 = in_bytes[i-1];
            int s0 = in_bytes[i-2];
            RansWordEncPut(&rans1, &ptr, stats.cum_freqs[s1], stats.freqs[s1]);
            RansWordEncPut(&rans0, &ptr, stats.cum_freqs[s0], stats.freqs[s0]);
        }
        RansWordEncFlush(&rans1, &ptr);
        RansWordEncFlush(&rans0, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%"PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", enc_clocks, 1.0 * enc_clocks / in_size, 1.0 * in_size / (enc_time * 1048576.0));
    }
    printf("interleaved rANS: %d bytes\n", (int) (out_buf + out_max_size - (uint8_t*)rans_begin));

    // try interleaved rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        RansWordDec rans0, rans1;
        uint16_t* ptr = rans_begin;
        RansWordDecInit(&rans0, &ptr);
        RansWordDecInit(&rans1, &ptr);

        for (size_t i=0; i < (in_size & ~1); i += 2) {
            uint8_t s0 = RansWordDecSym(&rans0, &tab);
            uint8_t s1 = RansWordDecSym(&rans1, &tab);
            dec_bytes[i+0] = (uint8_t) s0;
            dec_bytes[i+1] = (uint8_t) s1;
            RansWordDecRenorm(&rans0, &ptr);
            RansWordDecRenorm(&rans1, &ptr);
        }

        // last byte, if number of bytes was odd
        if (in_size & 1) {
            uint8_t s0 = RansWordDecSym(&rans0, &tab);
            dec_bytes[in_size - 1] = (uint8_t) s0;
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%"PRIu64" clocks, %.1f clocks/symbol (%5.1fMB/s)\n", dec_clocks, 1.0 * dec_clocks / in_size, 1.0 * in_size / (dec_time * 1048576.0));
    }

    // check decode results
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    // ---- SIMD interleaved rANS encode/decode.

    memset(dec_bytes, 0xcc, in_size);

    // try SIMD rANS encode
    // this is written for clarity not speed.
    printf("\ninterleaved SIMD rANS encode: (encode itself isn't SIMD)\n");
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        RansWordEnc rans[8];
        for (int i=0; i < 8; i++)
            rans[i] = RansWordEncInit();

        uint16_t* ptr = (uint16_t *)(out_buf + out_max_size); // *end* of output buffer

        // last few bytes
        for (size_t i=in_size; i > 0; i--) { // NB: working in reverse
            int s = in_bytes[i - 1];
            RansWordEncPut(&rans[(i - 1) & 7], &ptr, stats.cum_freqs[s], stats.freqs[s]);
        }
        for (int i=8; i > 0; i--)
            RansWordEncFlush(&rans[i - 1], &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%"PRIu64" clocks, %.1f clocks/symbol (%5.1fMiB/s)\n", enc_clocks, 1.0 * enc_clocks / in_size, 1.0 * in_size / (enc_time * 1048576.0));
    }
    printf("SIMD rANS: %d bytes\n", (int) (out_buf + out_max_size - (uint8_t*)rans_begin));

    // try SIMD rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        RansSimdDec rans0, rans1;
        uint16_t* ptr = rans_begin;
        RansSimdDecInit(&rans0, &ptr);
        RansSimdDecInit(&rans1, &ptr);

        for (size_t i=0; i < (in_size & ~7); i += 8) {
            uint32_t s03 = RansSimdDecSym(&rans0, &tab);
            uint32_t s47 = RansSimdDecSym(&rans1, &tab);
            *(uint32_t *)(dec_bytes + i) = s03;
            *(uint32_t *)(dec_bytes + i + 4) = s47;
            RansSimdDecRenorm(&rans0, &ptr);
            RansSimdDecRenorm(&rans1, &ptr);
        }

        // last few bytes
        for (size_t i=(in_size & ~7); i < in_size; i++) {
            RansSimdDec* which = (i & 4) != 0 ? &rans1 : &rans0;
            uint8_t s = RansWordDecSym(&which->lane[i & 3], &tab);
            dec_bytes[i] = s;
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%"PRIu64" clocks, %.1f clocks/symbol (%5.1fMB/s)\n", dec_clocks, 1.0 * dec_clocks / in_size, 1.0 * in_size / (dec_time * 1048576.0));
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
