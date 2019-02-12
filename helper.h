#ifndef HELPER_H_
#define HELPER_H_

#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>

void panic(const char *fmt, ...);

template <typename T>
void read_file(const std::string& filename, std::vector<T>* tokens ){
	std::ifstream is (filename, std::ios_base::binary|std::ios_base::in);
	if (is) {
		// get length of file:
		is.seekg (0, is.end);
		size_t length = is.tellg();
		is.seekg (0, is.beg);

		// reserve size of tokens
		if (!tokens){
			throw std::runtime_error("Passed empty vector to read file into");
		}

		if (length % sizeof(T)){
			throw std::runtime_error("Filesize is not a multiple of datatype.");
		}
		// size the vector appropriately
		size_t num_elems = length / sizeof(T);
		tokens->resize(num_elems);

		// read data as a block:
		is.read (reinterpret_cast<char*>(tokens->data()),length);
		is.close();
	}
};
template <typename T>
class SymbolStats
{
public:
	SymbolStats():min(0),max(0),freqs(),cum_freqs(){};

	void count_freqs(const std::vector<T>& tokens){
		// find min and max
		const auto minmax = std::minmax_element (tokens.begin(),tokens.end());
		min = *minmax.first;
		max = *minmax.second;

		freqs.resize(std::abs( max - min )+1,0);

		for (auto token: tokens){
			freqs[token-min]++;
		}
	};

	void normalize_freqs(uint32_t target_total)
	{
		assert(target_total >= freqs.size());

		calc_cum_freqs();

	    std:: cout << "min: " <<min << " max: " << max << std::endl;
	    for(int i = 0; i<static_cast<int>(freqs.size()); i++){
	    	std::cout << i << ": " << i + min << " " << freqs[i] << " " << cum_freqs[i] << std::endl;
	    }
	    std::cout <<  cum_freqs.back() << std::endl;

		size_t cur_total = cum_freqs.back();

		// resample distribution based on cumulative freqs
		for (size_t i = 1; i <= freqs.size(); i++)
			cum_freqs[i] = ((uint64_t)target_total * cum_freqs[i])/cur_total;

		// if we nuked any non-0 frequency symbol to 0, we need to steal
		// the range to make the frequency nonzero from elsewhere.
		//
		// this is not at all optimal, i'm just doing the first thing that comes to mind.
		for (int i=0; i < static_cast<int>(freqs.size()); i++) {
			if (freqs[i] && cum_freqs[i+1] == cum_freqs[i]) {
				// symbol i was set to zero freq

				// find best symbol to steal frequency from (try to steal from low-freq ones)
				uint32_t best_freq = ~0u;
				int best_steal = -1;
				for (int j=0; j < static_cast<int>(freqs.size()); j++) {
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
		assert(cum_freqs.front() == 0 && cum_freqs.back() == target_total);
		for (int i=0; i < static_cast<int>(freqs.size()); i++) {
			if (freqs[i] == 0)
				assert(cum_freqs[i+1] == cum_freqs[i]);
			else
				assert(cum_freqs[i+1] > cum_freqs[i]);

			// calc updated freq
			freqs[i] = cum_freqs[i+1] - cum_freqs[i];
		}


	    std:: cout << "min: " <<min << " max: " << max << std::endl;
	    for(int i = 0; i<static_cast<int>(freqs.size()); i++){
	    	std::cout << i << ": " << i + min << " " << freqs[i] << " " << cum_freqs[i] << std::endl;
	    }
	    std::cout <<  cum_freqs.back() << std::endl;
	}

	int min = 0;
	int max = 0;
	std::vector<size_t> freqs;
	std::vector<size_t> cum_freqs;

private:
	void calc_cum_freqs(){
		cum_freqs.resize(freqs.size()+1);
		std::partial_sum(freqs.begin(), freqs.end(), cum_freqs.begin()+1);
	};


};

#endif /* HELPER_H_ */
