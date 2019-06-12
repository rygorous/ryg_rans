#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>

#include <nlohmann/json.hpp>
#include "librans/rans.h"

#include "platform.h"

#include "helper.h"

// This is just the sample program. All the meat is in rans_byte.h.
using json = nlohmann::json;
using source_t = uint8_t;
static const uint PROB_BITS = 14;
using coder_t = uint32_t;
using stream_t = uint8_t;
using Rans = rans::Coder<coder_t,stream_t>;
using RansEncSymbol = rans::EncoderSymbol<coder_t>;

static const uint REPETITIONS = 5;

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
	std::cout << "Symbols:" << tokens.size() << std::endl;
	run_summary["NumberOfSymbols"] = tokens.size();

	rans::SymbolStatistics stats(tokens);
	stats.rescaleFrequencyTable(prob_scale);
	auto symbolRangeBits = stats.getSymbolRangeBits();
	std::cout << "Min: "<< stats.minSymbol() <<" Max: " << stats.maxSymbol() << " Range: " << symbolRangeBits  << "Bit" << std::endl;
	run_summary["SymbolRange"] = symbolRangeBits;

	// cumlative->symbol table
	// this is super brute force
	std::vector<source_t>cum2sym(prob_scale);
	for (size_t s=0; s < stats.size(); s++)
		for (uint32_t i=stats[s].second; i < stats[s+1].second; i++)
			cum2sym[i] = (s + stats.minSymbol());

	const size_t out_max_size = 32<<20; // 32MB
	const size_t out_max_elems = out_max_size / sizeof(stream_t);
	std::vector<stream_t>out_buf(out_max_elems);
	const stream_t* out_end = &out_buf.back();
	std::vector<source_t> dec_bytes(tokens.size(),0xcc);

	stream_t *rans_begin;
	std::vector<RansEncSymbol> esyms;
	std::vector<rans::DecoderSymbol> dsyms;

	for (size_t i=0; i < stats.size(); i++) {
		//        std::cout << "esyns[" << i << "]: " << stats.freqs[i] << ", " << stats.cum_freqs[i] << ", "<< prob_bits <<  std::endl;
		const auto symbolStats = stats[i];
		esyms.emplace_back(symbolStats.second, symbolStats.first, prob_bits);
		dsyms.emplace_back(symbolStats.second, symbolStats.first);
	}

	std::cout << "Source Size :" << tokens.size()*symbolRangeBits * BIT_TO_BITES << " Bytes"<< std::endl;

	// ---- regular rANS encode/decode. Typical usage.
	std::cout << std::endl <<"Non-Interleaved:" << std::endl;
	timedRun(run_summary,ExecutionMode::NonInterleaved,CodingMode::Encode,REPETITIONS,
			[&](){
		rans::State<coder_t> rans;
		Rans::encInit(&rans);

		stream_t* ptr = out_end; // *end* of output buffer
		for (size_t i=tokens.size(); i > 0; i--) { // NB: working in reverse!
			source_t s = tokens[i-1];
			size_t normalized = s - stats.minSymbol();

			//            std::cout << "s: " << s << ", esyns[" << normalized << "]: " << esyms[normalized].freq << std::endl;
			//            Rans32::encPut(&rans, &ptr, stats.cum_freqs[normalized], stats.freqs[normalized], prob_bits);
			Rans::encPutSymbol(&rans, &ptr, &esyms[normalized], prob_bits);
		}
		Rans::encFlush(&rans, &ptr);
		rans_begin = ptr;
	});

	timedRun(run_summary,ExecutionMode::NonInterleaved,CodingMode::Decode,REPETITIONS,[&](){
		rans::State<coder_t> rans;
		stream_t* ptr = rans_begin;
		Rans::decInit(&rans, &ptr);

		for (size_t i=0; i < tokens.size(); i++) {
			source_t s = cum2sym[Rans::decGet(&rans, prob_bits)];
			dec_bytes[i] = s;
			const size_t normalized = s - stats.minSymbol();
			//            std::cout << "s: " << s << ", dsyms[" << normalized << "]: " << dsyms[normalized].freq << std::endl;
			Rans::decAdvanceSymbol(&rans, &ptr, &dsyms[normalized], prob_bits);
		}
	});

	unsigned int encodeSize = static_cast<unsigned int>(&out_buf.back() - rans_begin) * sizeof(stream_t);
	std::cout << "Encode Size :" << encodeSize << " Bytes"<< std::endl;
	run_summary["NonInterleaved"]["Size"].push_back(encodeSize);

	// check decode results
	if (memcmp(tokens.data(), dec_bytes.data(), tokens.size()*sizeof(source_t)) == 0)
		printf("Decoder passed tests.\n");
	else
		printf("ERROR: Decoder failed tests.\n");

	// ---- interleaved rANS encode/decode. This is the kind of thing you might do to optimize critical paths.

	memset(dec_bytes.data(), 0xcc,tokens.size()*sizeof(source_t));

	// try interleaved rANS encode
	std::cout << std::endl <<"Interleaved:" << std::endl;

	timedRun(run_summary,ExecutionMode::Interleaved,CodingMode::Encode,REPETITIONS,
			[&](){
		rans::State<coder_t> rans0, rans1;
		Rans::encInit(&rans0);
		Rans::encInit(&rans1);

		stream_t* ptr = out_end;

		// odd number of bytes?
		if (tokens.size() & 1) {
			const int s = tokens.back();
			const size_t normalized = s - stats.minSymbol();
			Rans::encPutSymbol(&rans0, &ptr, &esyms[normalized], prob_bits);
			//            Rans::encPut(&rans0, &ptr, stats.cum_freqs[normalized], stats.freqs[normalized], prob_bits);
		}

		for (size_t i=(tokens.size() & ~1); i > 0; i -= 2) { // NB: working in reverse!
			const int s1 = tokens[i-1];
			const int s0 = tokens[i-2];
			const size_t normalized1 = s1 - stats.minSymbol();
			const size_t normalized0 = s0 - stats.minSymbol();

			Rans::encPutSymbol(&rans1, &ptr, &esyms[normalized1], prob_bits);
			Rans::encPutSymbol(&rans0, &ptr, &esyms[normalized0], prob_bits);
			//            Rans::encPut(&rans1, &ptr, stats.cum_freqs[normalized1], stats.freqs[normalized1], prob_bits);
			//            Rans::encPut(&rans0, &ptr, stats.cum_freqs[normalized0], stats.freqs[normalized0], prob_bits);
		}
		Rans::encFlush(&rans1, &ptr);
		Rans::encFlush(&rans0, &ptr);
		rans_begin = ptr;
	});

	timedRun(run_summary,ExecutionMode::Interleaved,CodingMode::Decode,REPETITIONS,
			[&](){
		rans::State<coder_t> rans0, rans1;
		stream_t* ptr = rans_begin;
		Rans::decInit(&rans0, &ptr);
		Rans::decInit(&rans1, &ptr);

		for (size_t i=0; i < (tokens.size() & ~1); i += 2) {
			const uint32_t s0 = cum2sym[Rans::decGet(&rans0, prob_bits)];
			const uint32_t s1 = cum2sym[Rans::decGet(&rans1, prob_bits)];
			dec_bytes[i+0] = s0;
			dec_bytes[i+1] = s1;
			const size_t normalized0 = s0 - stats.minSymbol();
			const size_t normalized1 = s1 - stats.minSymbol();
			Rans::decAdvanceSymbolStep(&rans0, &dsyms[normalized0], prob_bits);
			Rans::decAdvanceSymbolStep(&rans1, &dsyms[normalized1], prob_bits);
			Rans::decRenorm(&rans0, &ptr);
			Rans::decRenorm(&rans1, &ptr);
		}

		// last byte, if number of bytes was odd
		if (tokens.size() & 1) {
			const uint32_t s0 = cum2sym[Rans::decGet(&rans0, prob_bits)];
			dec_bytes[tokens.size() - 1] = s0;
			const size_t normalized = s0 - stats.minSymbol();
			Rans::decAdvanceSymbol(&rans0, &ptr, &dsyms[normalized], prob_bits);
		}
	});

	encodeSize = static_cast<unsigned int>(&out_buf.back() - rans_begin) * sizeof(stream_t);
	std::cout << "Encode Size :" << encodeSize << " Bytes"<< std::endl;
	run_summary["Interleaved"]["Size"].push_back(encodeSize);

	// check decode results
	if (memcmp(tokens.data(), dec_bytes.data(), tokens.size()*sizeof(source_t)) == 0)
		printf("Decoder passed tests.\n");
	else
		printf("ERROR: Decoder failed tests.\n");

	std::ofstream f("summary.json");
	f << std::setw(4) << run_summary << std::endl;
	f.close();
	return 0;
}
