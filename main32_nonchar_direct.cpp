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

#include "rans32_direct.h"
#include "helper.h"
#include "SymbolStats.h"
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
    static const uint32_t prob_bits = 16;
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

    static const size_t out_max_size = 32<<20; // 32MB
    std::vector<uint16_t> out_buf(out_max_size/sizeof(uint16_t));
    std::vector<source_t> dec_bytes(tokens.size(),0xcc);

    // try rANS encode
    uint16_t *rans_begin;
    std::vector<Rans32EncSymbol> esyms(stats.freqs.size());
    std::vector<Rans32DecSymbol> dsyms(stats.freqs.size());

    for (size_t i=0; i < stats.freqs.size(); i++) {
//        std::cout << "esyns[" << i << "]: " << stats.freqs[i] << ", " << stats.cum_freqs[i] << ", "<< prob_bits <<  std::endl;
//        Rans32EncSymbolInit(&esyms[i], stats.cum_freqs[i], stats.freqs[i], prob_bits);
        Rans32DecSymbolInit(&dsyms[i], stats.cum_freqs[i], stats.freqs[i]);
    }

    // ---- regular rANS encode/decode. Typical usage.
    printf("rANS encode:\n");
    auto enc_range = std::max(std::ceil(std::log2(stats.max-stats.min)),1.0);
    std::cout << "Symbol Range: " << enc_range  << "Bit" << std::endl;
    enc_range /= 8;
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        [&](){
        Rans32State rans;
        Rans32EncInit(&rans);

        uint16_t* ptr = &out_buf.back(); // *end* of output buffer
        for (size_t i=tokens.size(); i > 0; i--) { // NB: working in reverse!
            source_t s = tokens[i-1];
            size_t normalized = s - stats.min;

//            std::cout << "s: " << s << ", esyns[" << normalized << "]: " << esyms[normalized].freq << std::endl;
            Rans32EncPut(&rans, &ptr, stats.cum_freqs[normalized], stats.freqs[normalized], prob_bits);
//            Rans32EncPutSymbol(&rans, &ptr, &esyms[normalized]);
        }
        Rans32EncFlush(&rans, &ptr);
        rans_begin = ptr;
        }();

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", enc_clocks, 1.0 * enc_clocks / tokens.size()*enc_range, 1.0 * (tokens.size()*enc_range)  / (enc_time * 1048576.0));
    }
    printf("rANS: %d bytes\n", (int) ((&out_buf.back() - rans_begin)* sizeof(uint16_t)));

    // try rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        [&](){
        Rans32State rans;
        uint16_t* ptr = rans_begin;
        Rans32DecInit(&rans, &ptr);

        for (size_t i=0; i < tokens.size(); i++) {
            source_t s = cum2sym[Rans32DecGet(&rans, prob_bits)];
            dec_bytes[i] = s;
            const size_t normalized = s - stats.min;
//            std::cout << "s: " << s << ", dsyms[" << normalized << "]: " << dsyms[normalized].freq << std::endl;
            Rans32DecAdvanceSymbol(&rans, &ptr, &dsyms[normalized], prob_bits);
        }
        }();

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

        Rans32State rans0, rans1;
        Rans32EncInit(&rans0);
        Rans32EncInit(&rans1);

        uint16_t* ptr = &out_buf.back();

        // odd number of bytes?
        if (tokens.size() & 1) {
            const int s = tokens.back();
            const size_t normalized = s - stats.min;
//            Rans32EncPutSymbol(&rans0, &ptr, &esyms[normalized]);
            Rans32EncPut(&rans0, &ptr, stats.cum_freqs[normalized], stats.freqs[normalized], prob_bits);
        }

        for (size_t i=(tokens.size() & ~1); i > 0; i -= 2) { // NB: working in reverse!
            const int s1 = tokens[i-1];
            const int s0 = tokens[i-2];
            const size_t normalized1 = s1 - stats.min;
            const size_t normalized0 = s0 - stats.min;

//            Rans32EncPutSymbol(&rans1, &ptr, &esyms[normalized1]);
//            Rans32EncPutSymbol(&rans0, &ptr, &esyms[normalized0]);
            Rans32EncPut(&rans1, &ptr, stats.cum_freqs[normalized1], stats.freqs[normalized1], prob_bits);
            Rans32EncPut(&rans0, &ptr, stats.cum_freqs[normalized0], stats.freqs[normalized0], prob_bits);
        }
        Rans32EncFlush(&rans1, &ptr);
        Rans32EncFlush(&rans0, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", enc_clocks, 1.0 * enc_clocks / tokens.size()*enc_range, 1.0 * (tokens.size()*enc_range)  / (enc_time * 1048576.0));

    }
    printf("interleaved rANS: %d bytes\n", (int) ((&out_buf.back() - rans_begin)* sizeof(uint16_t)));

    // try interleaved rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        Rans32State rans0, rans1;
        uint16_t* ptr = rans_begin;
        Rans32DecInit(&rans0, &ptr);
        Rans32DecInit(&rans1, &ptr);

        for (size_t i=0; i < (tokens.size() & ~1); i += 2) {
            const uint32_t s0 = cum2sym[Rans32DecGet(&rans0, prob_bits)];
            const uint32_t s1 = cum2sym[Rans32DecGet(&rans1, prob_bits)];
            dec_bytes[i+0] = s0;
            dec_bytes[i+1] = s1;
            const size_t normalized0 = s0 - stats.min;
            const size_t normalized1 = s1 - stats.min;
            Rans32DecAdvanceSymbolStep(&rans0, &dsyms[normalized0], prob_bits);
            Rans32DecAdvanceSymbolStep(&rans1, &dsyms[normalized1], prob_bits);
            Rans32DecRenorm(&rans0, &ptr);
            Rans32DecRenorm(&rans1, &ptr);
        }

        // last byte, if number of bytes was odd
        if (tokens.size() & 1) {
        	const uint32_t s0 = cum2sym[Rans32DecGet(&rans0, prob_bits)];
            dec_bytes[tokens.size() - 1] = s0;
            const size_t normalized = s0 - stats.min;
            Rans32DecAdvanceSymbol(&rans0, &ptr, &dsyms[normalized], prob_bits);
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

    return 0;
}
