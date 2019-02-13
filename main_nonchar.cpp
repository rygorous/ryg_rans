#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <iostream>
#include <cmath>

#include "rans64.h"
#include "helper.h"
//#include "SymbolStats.h"
// This is just the sample program. All the meat is in rans_byte.h.

using source_t = int32_t;

int main(int argc, char* argv[])
{
    std::string filename;
    if (argc > 1)
    {
        filename = argv[1];
    }else{
    	throw "no filename provided";
    }

    printf("Compressing file: %s\n", filename.c_str());

    std::vector<source_t> tokens;
    read_file(filename,&tokens);
    std::cout << "read " << tokens.size() << " integers." << std::endl;
//    for(auto token: tokens){
//    	std::cout << token << std::endl;
//    }

//    static const uint32_t prob_bits = 14;
    static const uint32_t prob_bits = 20;
    static const uint32_t prob_scale = 1 << prob_bits;

    SymbolStats<source_t> stats;
    stats.count_freqs(tokens);

    stats.normalize_freqs(prob_scale);

    // cumlative->symbol table
    // this is super brute force
    source_t cum2sym[prob_scale];
    for (size_t s=0; s < stats.freqs.size(); s++)
        for (uint32_t i=stats.cum_freqs[s]; i < stats.cum_freqs[s+1]; i++)
            cum2sym[i] = (s + stats.min);

    static size_t out_max_size = 32<<20; // 32MB
    static size_t out_max_elems = out_max_size / sizeof(uint32_t);
    uint32_t* out_buf = new uint32_t[out_max_elems];
    uint32_t* out_end = out_buf + out_max_elems;
    std::vector<source_t> dec_bytes(tokens.size(),0xcc);

    // try rANS encode
    uint32_t *rans_begin;
    std::vector<Rans64EncSymbol> esyms(stats.freqs.size());
    std::vector<Rans64DecSymbol> dsyms(stats.freqs.size());

    for (size_t i=0; i < stats.freqs.size(); i++) {
//        std::cout << "esyns[" << i << "]: " << stats.freqs[i] << ", " << stats.cum_freqs[i] << ", "<< prob_bits <<  std::endl;
        Rans64EncSymbolInit(&esyms[i], stats.cum_freqs[i], stats.freqs[i], prob_bits);
        Rans64DecSymbolInit(&dsyms[i], stats.cum_freqs[i], stats.freqs[i]);
    }

    // ---- regular rANS encode/decode. Typical usage.
    printf("rANS encode:\n");
    auto enc_range = std::max(std::ceil(std::log2(stats.max-stats.min)),1.0);
    std::cout << "Symbol Range: " << enc_range  << "Bit" << std::endl;
    enc_range /= 8;
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        Rans64State rans;
        Rans64EncInit(&rans);

        uint32_t* ptr = out_end; // *end* of output buffer
        for (size_t i=tokens.size(); i > 0; i--) { // NB: working in reverse!
            source_t s = tokens[i-1];
            size_t normalized = s - stats.min;

//            std::cout << "s: " << s << ", esyns[" << normalized << "]: " << esyms[normalized].freq << std::endl;
            Rans64EncPutSymbol(&rans, &ptr, &esyms[normalized], prob_bits);
        }
        Rans64EncFlush(&rans, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", enc_clocks, 1.0 * enc_clocks / tokens.size()*enc_range, 1.0 * (tokens.size()*enc_range)  / (enc_time * 1048576.0));
    }
    printf("rANS: %d bytes\n", (int) ((out_end - rans_begin) * sizeof(uint32_t)));

    // try rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        Rans64State rans;
        uint32_t* ptr = rans_begin;
        Rans64DecInit(&rans, &ptr);

        for (size_t i=0; i < tokens.size(); i++) {
            source_t s = cum2sym[Rans64DecGet(&rans, prob_bits)];
            dec_bytes[i] = s;
            const size_t normalized = s - stats.min;
//            std::cout << "s: " << s << ", dsyms[" << normalized << "]: " << dsyms[normalized].freq << std::endl;
            Rans64DecAdvanceSymbol(&rans, &ptr, &dsyms[normalized], prob_bits);
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", dec_clocks, 1.0 * dec_clocks / tokens.size()*enc_range, 1.0 * tokens.size()*enc_range / (dec_time * 1048576.0));
    }

    // check decode results
    if (memcmp(tokens.data(), dec_bytes.data(), tokens.size()*sizeof(source_t)) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    // ---- interleaved rANS encode/decode. This is the kind of thing you might do to optimize critical paths.

    memset(dec_bytes.data(), 0xcc,tokens.size()*sizeof(source_t));

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
        if (tokens.size() & 1) {
            const int s = tokens.back();
            const size_t normalized = s - stats.min;
            Rans64EncPutSymbol(&rans0, &ptr, &esyms[normalized], prob_bits);
        }

        for (size_t i=(tokens.size() & ~1); i > 0; i -= 2) { // NB: working in reverse!
            const int s1 = tokens[i-1];
            const int s0 = tokens[i-2];
            const size_t normalized1 = s1 - stats.min;
            const size_t normalized0 = s0 - stats.min;

            Rans64EncPutSymbol(&rans1, &ptr, &esyms[normalized1], prob_bits);
            Rans64EncPutSymbol(&rans0, &ptr, &esyms[normalized0], prob_bits);
        }
        Rans64EncFlush(&rans1, &ptr);
        Rans64EncFlush(&rans0, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", enc_clocks, 1.0 * enc_clocks / tokens.size()*enc_range, 1.0 * (tokens.size()*enc_range)  / (enc_time * 1048576.0));

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

        for (size_t i=0; i < (tokens.size() & ~1); i += 2) {
            const uint32_t s0 = cum2sym[Rans64DecGet(&rans0, prob_bits)];
            const uint32_t s1 = cum2sym[Rans64DecGet(&rans1, prob_bits)];
            dec_bytes[i+0] = s0;
            dec_bytes[i+1] = s1;
            const size_t normalized0 = s0 - stats.min;
            const size_t normalized1 = s1 - stats.min;
            Rans64DecAdvanceSymbolStep(&rans0, &dsyms[normalized0], prob_bits);
            Rans64DecAdvanceSymbolStep(&rans1, &dsyms[normalized1], prob_bits);
            Rans64DecRenorm(&rans0, &ptr);
            Rans64DecRenorm(&rans1, &ptr);
        }

        // last byte, if number of bytes was odd
        if (tokens.size() & 1) {
        	const uint32_t s0 = cum2sym[Rans64DecGet(&rans0, prob_bits)];
            dec_bytes[tokens.size() - 1] = s0;
            const size_t normalized = s0 - stats.min;
            Rans64DecAdvanceSymbol(&rans0, &ptr, &dsyms[normalized], prob_bits);
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", dec_clocks, 1.0 * dec_clocks / tokens.size()*enc_range, 1.0 * tokens.size()*enc_range / (dec_time * 1048576.0));

    }

    // check decode results
    if (memcmp(tokens.data(), dec_bytes.data(), tokens.size()*sizeof(source_t)) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    delete[] out_buf;
    return 0;
}
