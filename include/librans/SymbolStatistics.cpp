
#include <cmath>

#include "SymbolStatistics.h"
namespace rans {

void SymbolStatistics::rescaleFrequencyTable(uint32_t newCumulatedFrequency){
	assert(newCumulatedFrequency >= frequencyTable_.size());

	//	std:: cout << "min: " <<min_ << " max: " << max_ << std::endl;
	//	    for(int i = 0; i<static_cast<int>(freqs.size()); i++){
	//	    	std::cout << i << ": " << i + min << " " << freqs[i] << " " << cum_freqs[i] << std::endl;
	//	    }
	//	    std::cout <<  cummulatedFrequencies_.back() << std::endl;

	size_t cumulatedFrequencies = cumulativeFrequencyTable_.back();

	//	    assert(target_total >= cur_total);

	// resample distribution based on cumulative frequencies_
	for (size_t i = 1; i <= frequencyTable_.size(); i++)
		cumulativeFrequencyTable_[i] = (static_cast<uint64_t>(newCumulatedFrequency) * cumulativeFrequencyTable_[i])/cumulatedFrequencies;

	// if we nuked any non-0 frequency symbol to 0, we need to steal
	// the range to make the frequency nonzero from elsewhere.
	//
	// this is not at all optimal, i'm just doing the first thing that comes to mind.
	for (size_t i=0; i < frequencyTable_.size(); i++) {
		if (frequencyTable_[i] && cumulativeFrequencyTable_[i+1] == cumulativeFrequencyTable_[i]) {
			// symbol i was set to zero freq

			// find best symbol to steal frequency from (try to steal from low-freq ones)
			std::pair<size_t,size_t>stealFromEntry{frequencyTable_.size(),~0u};
			for (size_t j=0; j < frequencyTable_.size(); j++) {
				uint32_t frequency = cumulativeFrequencyTable_[j+1] - cumulativeFrequencyTable_[j];
				if (frequency > 1 && frequency < stealFromEntry.second) {
					stealFromEntry.second = frequency;
					stealFromEntry.first = j;
				}
			}
			assert(stealFromEntry.first != frequencyTable_.size());

			// and steal from it!
			if (stealFromEntry.first < i) {
				for (size_t j = stealFromEntry.first + 1; j <= i; j++)
					cumulativeFrequencyTable_[j]--;
			} else {
				assert(stealFromEntry.first >  i);
				for (size_t j = i + 1; j <= stealFromEntry.first; j++)
					cumulativeFrequencyTable_[j]++;
			}
		}
	}

	// calculate updated freqs and make sure we didn't screw anything up
	assert(cumulativeFrequencyTable_.front() == 0 && cumulativeFrequencyTable_.back() == newCumulatedFrequency);
	for (size_t i=0; i < frequencyTable_.size(); i++) {
		if (frequencyTable_[i] == 0)
			assert(cumulativeFrequencyTable_[i+1] == cumulativeFrequencyTable_[i]);
		else
			assert(cumulativeFrequencyTable_[i+1] > cumulativeFrequencyTable_[i]);

		// calc updated freq
		frequencyTable_[i] = cumulativeFrequencyTable_[i+1] - cumulativeFrequencyTable_[i];
	}
	//	    for(int i = 0; i<static_cast<int>(freqs.size()); i++){
	//	    	std::cout << i << ": " << i + min_ << " " << freqs[i] << " " << cummulatedFrequencies_[i] << std::endl;
	//	    }
	//	    std::cout <<  cummulatedFrequencies_.back() << std::endl;
}


int SymbolStatistics::minSymbol() const {return min_;}

int SymbolStatistics::maxSymbol() const {return max_;}

size_t SymbolStatistics::size() const {return frequencyTable_.size();}

size_t SymbolStatistics::getSymbolRangeBits() const{
	return std::max(std::ceil(std::log2(max_-min_)),1.0);
}

std::pair<size_t,size_t> SymbolStatistics::operator[](size_t index) const{
	return std::make_pair(frequencyTable_[index], cumulativeFrequencyTable_[index]);
}


void SymbolStatistics::buildCumulativeFrequencyTable(){
	cumulativeFrequencyTable_.resize(frequencyTable_.size()+1);
	std::partial_sum(frequencyTable_.begin(), frequencyTable_.end(), cumulativeFrequencyTable_.begin()+1);
}

}  // namespace rans

