///*
// * Dictionary.h
// *
// *  Created on: May 21, 2019
// *      Author: Michael Lettrich (michael.lettrich@cern.ch)
// */
//
//#pragma once
//
//#include <vector>
//
//#include "helper.h"
//#include "EncSymbol.h"
//#include "DecSymbol.h"
//#include "SymbolStatistics.h"
//
//namespace rans {
//
//template <typename T,typename Source_t>
//class Dictionary {
//
//public:
//	Dictionary(const std::vector<Source_t>& tokens, size_t probabilityBits): probabilityBits_(probabilityBits)
//{
//		const size_t probabilityScale = 1ul << probabilityBits;
//
//	    SymbolStats<Source_t> stats;
//	    stats.count_freqs(tokens);
//	    stats.normalize_freqs(probabilityScale);
//
//	    // cumlative->symbol table
//	    // this is super brute force
//	    Source_t cum2sym[probabilityScale];
//	    for (size_t s=0; s < stats.freqs.size(); s++)
//	        for (uint32_t i=stats.cum_freqs[s]; i < stats.cum_freqs[s+1]; i++)
//	            cum2sym[i] = (s + stats.min);
//
//	    for (size_t i=0; i < stats.freqs.size(); i++) {
//	    	encodeSymbols_.emplace_back(stats.cum_freqs[i], stats.freqs[i], probabilityBits_);
//	    	decodeSymbols_.emplace_back(stats.cum_freqs[i], stats.freqs[i]);
//	    }
//}
//
//	size_t getSize(){return encodeSymbols_.size();};
//private:
//
//
//	size_t probabilityBits_;
//    std::vector<EncSymbol<T>> encodeSymbols_;
//    std::vector<DecSymbol> decodeSymbols_;
//};
//
//}  // namespace rans
//
//
