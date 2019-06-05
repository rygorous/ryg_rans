#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>

#include <nlohmann/json.hpp>
#include "librans/rans.h"

#include "platform.h"

#include "helper.h"

// This is just the sample program. All the meat is in rans_byte.h.
using json = nlohmann::json;
using source_t = uint8_t;
static const uint PROB_BITS = 14;
using Rans32 = rans::Coder<uint32_t,uint8_t>;
using Rans32EncSymbol = rans::EncSymbol<uint32_t>;


int main(int argc, char* argv[])
{
	json run_summary;

	cmd_args parameters;
	read_args(argc, argv,parameters);
    static const uint32_t prob_bits = (parameters.prob_bits >0) ? parameters.prob_bits : PROB_BITS;
    static const uint32_t prob_scale = 1 << prob_bits;

    std::cout << "Filename: " << parameters.filename << std::endl;
    std::cout << "Probability Bits: " << prob_bits << std::endl;

    run_summary["Filename"] = parameters.filename;
    run_summary["ProbabilityBits"] = prob_bits;

    std::vector<source_t> tokens;
    read_file(parameters.filename,&tokens);
    std::cout << "Read symbols:" << tokens.size() << std::endl;

    rans::SymbolStatistics stats(tokens);
    stats.rescaleFrequencyTable(prob_scale);

    // cumlative->symbol table
    // this is super brute force
    std::vector<source_t>cum2sym(prob_scale);
    for (size_t s=0; s < stats.size(); s++)
        for (uint32_t i=stats[s].second; i < stats[s+1].second; i++)
            cum2sym[i] = (s + stats.minSymbol());

    static const size_t out_max_size = 32<<20; // 32MB
    static const size_t out_max_elems = out_max_size / sizeof(uint8_t);
    std::vector<uint8_t>out_buf(out_max_elems);
//    uint8_t* out_end = &out_buf.back();
    std::vector<source_t> dec_bytes(tokens.size(),0xcc);

    // try rANS encode
    uint8_t *rans_begin;
    std::vector<Rans32EncSymbol> esyms;
    std::vector<rans::DecSymbol> dsyms;

    for (size_t i=0; i < stats.size(); i++) {
//        std::cout << "esyns[" << i << "]: " << stats.freqs[i] << ", " << stats.cum_freqs[i] << ", "<< prob_bits <<  std::endl;
        const auto symbolStats = stats[i];
    	esyms.emplace_back(symbolStats.second, symbolStats.first, prob_bits);
        dsyms.emplace_back(symbolStats.second, symbolStats.first);
    }

    // ---- regular rANS encode/decode. Typical usage.
    printf("rANS encode:\n");
    auto enc_range = stats.getSymbolRangeBits();
    std::cout << "Symbol Range: " << enc_range  << "Bit" << std::endl;
    run_summary["SymbolRange"] = enc_range;
    run_summary["NonInterleaved"]["Encode"] = json::array();
    run_summary["NonInterleaved"]["Decode"] = json::array();
    enc_range /= 8;
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        [&](){
        rans::State<uint32_t> rans;
        Rans32::encInit(&rans);

        uint8_t* ptr = &out_buf.back(); // *end* of output buffer
        for (size_t i=tokens.size(); i > 0; i--) { // NB: working in reverse!
            source_t s = tokens[i-1];
            size_t normalized = s - stats.minSymbol();

//            std::cout << "s: " << s << ", esyns[" << normalized << "]: " << esyms[normalized].freq << std::endl;
//            Rans32::encPut(&rans, &ptr, stats.cum_freqs[normalized], stats.freqs[normalized], prob_bits);
            Rans32::encPutSymbol(&rans, &ptr, &esyms[normalized], prob_bits);
        }
        Rans32::encFlush(&rans, &ptr);
        rans_begin = ptr;
        }();

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        const double bandwidth = 1.0 * (tokens.size()*enc_range)  / (enc_time * 1048576.0);
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", enc_clocks, 1.0 * enc_clocks / tokens.size()*enc_range, bandwidth);
        run_summary["NonInterleaved"]["Encode"].push_back(bandwidth);
    }
    {
    const int size = static_cast<int>(&out_buf.back() - rans_begin);
    printf("rANS: %d bytes\n",  size);
    run_summary["NonInterleaved"]["Size"] = size;
    }
    // try rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        [&](){
        rans::State<uint32_t> rans;
        uint8_t* ptr = rans_begin;
        Rans32::decInit(&rans, &ptr);

        for (size_t i=0; i < tokens.size(); i++) {
            source_t s = cum2sym[Rans32::decGet(&rans, prob_bits)];
            dec_bytes[i] = s;
            const size_t normalized = s - stats.minSymbol();
//            std::cout << "s: " << s << ", dsyms[" << normalized << "]: " << dsyms[normalized].freq << std::endl;
            Rans32::decAdvanceSymbol(&rans, &ptr, &dsyms[normalized], prob_bits);
        }
        }();

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        const double bandwidth = 1.0 * tokens.size()*enc_range / (dec_time * 1048576.0);
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", dec_clocks, 1.0 * dec_clocks / tokens.size()*enc_range, bandwidth);
        run_summary["NonInterleaved"]["Decode"].push_back(bandwidth);
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
    run_summary["Interleaved"]["Encode"] = json::array();
    run_summary["Interleaved"]["Decode"] = json::array();

    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t enc_start_time = __rdtsc();

        rans::State<uint32_t> rans0, rans1;
        Rans32::encInit(&rans0);
        Rans32::encInit(&rans1);

        uint8_t* ptr = &out_buf.back();

        // odd number of bytes?
        if (tokens.size() & 1) {
            const int s = tokens.back();
            const size_t normalized = s - stats.minSymbol();
            Rans32::encPutSymbol(&rans0, &ptr, &esyms[normalized], prob_bits);
//            Rans32::encPut(&rans0, &ptr, stats.cum_freqs[normalized], stats.freqs[normalized], prob_bits);
        }

        for (size_t i=(tokens.size() & ~1); i > 0; i -= 2) { // NB: working in reverse!
            const int s1 = tokens[i-1];
            const int s0 = tokens[i-2];
            const size_t normalized1 = s1 - stats.minSymbol();
            const size_t normalized0 = s0 - stats.minSymbol();

            Rans32::encPutSymbol(&rans1, &ptr, &esyms[normalized1],prob_bits);
            Rans32::encPutSymbol(&rans0, &ptr, &esyms[normalized0],prob_bits);
//            Rans32::encPut(&rans1, &ptr, stats.cum_freqs[normalized1], stats.freqs[normalized1], prob_bits);
//            Rans32::encPut(&rans0, &ptr, stats.cum_freqs[normalized0], stats.freqs[normalized0], prob_bits);
        }
        Rans32::encFlush(&rans1, &ptr);
        Rans32::encFlush(&rans0, &ptr);
        rans_begin = ptr;

        uint64_t enc_clocks = __rdtsc() - enc_start_time;
        double enc_time = timer() - start_time;
        const double bandwidth = 1.0 * (tokens.size()*enc_range)  / (enc_time * 1048576.0);
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", enc_clocks, 1.0 * enc_clocks / tokens.size()*enc_range, bandwidth);
        run_summary["Interleaved"]["Encode"].push_back(bandwidth);

    }
    {
    const int size = static_cast<int>(&out_buf.back() - rans_begin);
    printf("interleaved rANS: %d bytes\n", size);
    run_summary["Interleaved"]["Size"] = size;
    }
    // try interleaved rANS decode
    for (int run=0; run < 5; run++) {
        double start_time = timer();
        uint64_t dec_start_time = __rdtsc();

        rans::State<uint32_t> rans0, rans1;
        uint8_t* ptr = rans_begin;
        Rans32::decInit(&rans0, &ptr);
        Rans32::decInit(&rans1, &ptr);

        for (size_t i=0; i < (tokens.size() & ~1); i += 2) {
            const uint32_t s0 = cum2sym[Rans32::decGet(&rans0, prob_bits)];
            const uint32_t s1 = cum2sym[Rans32::decGet(&rans1, prob_bits)];
            dec_bytes[i+0] = s0;
            dec_bytes[i+1] = s1;
            const size_t normalized0 = s0 - stats.minSymbol();
            const size_t normalized1 = s1 - stats.minSymbol();
            Rans32::decAdvanceSymbolStep(&rans0, &dsyms[normalized0], prob_bits);
            Rans32::decAdvanceSymbolStep(&rans1, &dsyms[normalized1], prob_bits);
            Rans32::decRenorm(&rans0, &ptr);
            Rans32::decRenorm(&rans1, &ptr);
        }

        // last byte, if number of bytes was odd
        if (tokens.size() & 1) {
        	const uint32_t s0 = cum2sym[Rans32::decGet(&rans0, prob_bits)];
            dec_bytes[tokens.size() - 1] = s0;
            const size_t normalized = s0 - stats.minSymbol();
            Rans32::decAdvanceSymbol(&rans0, &ptr, &dsyms[normalized], prob_bits);
        }

        uint64_t dec_clocks = __rdtsc() - dec_start_time;
        double dec_time = timer() - start_time;
        const double bandwidth = 1.0 * tokens.size()*enc_range / (dec_time * 1048576.0);
        printf("%" PRIu64" clocks, %.1f clocks/symbol (%5.1f MiB/s)\n", dec_clocks, 1.0 * dec_clocks / tokens.size()*enc_range, bandwidth);
        run_summary["Interleaved"]["Decode"].push_back(bandwidth);
    }

    // check decode results
    if (memcmp(tokens.data(), dec_bytes.data(), tokens.size()*sizeof(source_t)) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    std::ofstream f("summary.json");
    f << std::setw(4) << run_summary << std::endl;
    f.close();
    return 0;
}
