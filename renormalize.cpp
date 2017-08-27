// Copyright (c) 2017, Steinar H. Gunderson
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "renormalize.h"

#include <assert.h>
#include <math.h>

#include <unordered_map>
#include <map>
#include <memory>
#include <utility>

using std::equal_to;
using std::hash;
using std::max;
using std::min;
using std::make_pair;
using std::pair;
using std::unique_ptr;
using std::unordered_map;

namespace {

struct OptimalChoice {
	double cost;  // In bits.
	uint32_t chosen_freq;
};
struct CacheKey {
	int num_syms;
	int available_slots;

	bool operator== (const CacheKey &other) const
	{
		return num_syms == other.num_syms && available_slots == other.available_slots;
	}
};
struct HashCacheKey {
	size_t operator() (const CacheKey &key) const
	{
		return hash<int64_t>()((uint64_t(key.available_slots) << 32) | key.num_syms);
	}
};
using CacheMap = unordered_map<CacheKey, OptimalChoice, HashCacheKey>;

// Find, recursively, the optimal cost of encoding the symbols [0, num_syms),
// assuming an optimal distribution of those symbols to "available_slots".
// The cache is used for memoization, and also to remember the best choice.
// No frequency can be zero.
//
// Returns HUGE_VAL if there's no legal mapping.
double FindOptimalCost(uint32_t *cum_freqs, int num_syms, int available_slots, const double *log2cache, CacheMap *cache)
{
	static int k = 0;
	if (num_syms == 0) {
		// Encoding zero symbols needs zero bits.
		return 0.0;
	}
	if (num_syms > available_slots) {
		// Every (non-zero-frequency) symbol needs at least one slot.
		return HUGE_VAL;
	}
	if (num_syms == 1) {
		return cum_freqs[1] * log2cache[available_slots];
	}

	CacheKey cache_key{num_syms, available_slots};
	auto insert_result = cache->insert(make_pair(cache_key, OptimalChoice()));
	if (!insert_result.second) {
		// There was already an item in the cache, so return it.
		return insert_result.first->second.cost;
	}

	// Minimize the number of total bits spent as a function of how many slots
	// we assign to this symbol.
	//
	// The cost function is convex (at least in practice; I suppose also in
	// theory because it's the sum of an increasing and a decreasing function?).
	// Find a reasonable guess and see in what direction the function is decreasing,
	// then iterate until we either hit the end or we start increasing again.
	//
	// Since the function is a sum of log() terms, it is differentiable, and we
	// could in theory use this; however, it doesn't seem to be worth the complexity.
	uint32_t freq = cum_freqs[num_syms] - cum_freqs[num_syms - 1];
	assert(freq > 0);
	double guess = lrint(available_slots * double(freq) / cum_freqs[num_syms]);

	int x1 = max<int>(floor(guess), 1);
	int x2 = x1 + 1;

	double cost1 = freq * log2cache[x1] + FindOptimalCost(cum_freqs, num_syms - 1, available_slots - x1, log2cache, cache);
	double cost2 = freq * log2cache[x2] + FindOptimalCost(cum_freqs, num_syms - 1, available_slots - x2, log2cache, cache);

	int x;
	int direction;  // -1 or +1.
	double best_cost;
	if (isinf(cost1) && isinf(cost2)) {
		// The cost isn't infinite due to the first term, so we need to go downwards
		// to give the second term more room to breathe.
		x = x1;
		best_cost = cost1;
		direction = -1;
	} else if (cost1 < cost2) {
		x = x1;
		best_cost = cost1;
		direction = -1;
	} else {
		x = x2;
		best_cost = cost2;
		direction = 1;
	}
	int best_choice = x;

	for ( ;; ) {
		x += direction;
		if (x == 0 || x > available_slots) {
			// We hit the end; we can't assign zero slots to this symbol,
			// and we can't assign more slots than we have. This extreme
			// is the best choice.
			break;
		}
		double cost = freq * log2cache[x] + FindOptimalCost(cum_freqs, num_syms - 1, available_slots - x, log2cache, cache);
		if (cost > best_cost) {
			// The cost started increasing again, so we've found the optimal choice.
			break;
		}
		best_choice = x;
		best_cost = cost;
	}
	insert_result.first->second.cost = best_cost;
	insert_result.first->second.chosen_freq = best_choice;
	return best_cost;
}

}  // namespace

void OptimalRenormalize(uint32_t *cum_freqs, uint32_t num_syms, uint32_t target_total)
{
	// First remove all symbols that have a zero frequency; they tend to
	// complicate the analysis. We'll put them back afterwards.
	unique_ptr<uint32_t[]> remapped_cum_freqs(new uint32_t[num_syms + 1]);
	unique_ptr<uint32_t[]> mapping(new uint32_t[num_syms + 1]);

	uint32_t new_num_syms = 0;
	remapped_cum_freqs[0] = 0;
	for (uint32_t i = 0; i < num_syms; ++i) {
		if (cum_freqs[i + 1] == cum_freqs[i]) {
			continue;
		}
		mapping[new_num_syms] = i;
		remapped_cum_freqs[new_num_syms + 1] = cum_freqs[i + 1];
		new_num_syms++;
	}

	// Calculate the cost of encoding a symbol with frequency f/target_total.
	// We call log2() quite a lot, so it's best to cache it once at the start.
	unique_ptr<double[]> log2cache(new double[target_total + 1]);
	for (uint32_t i = 0; i <= target_total; ++i) {
		log2cache[i] = -log2(i * (1.0 / target_total));
	}

	CacheMap cache;
	FindOptimalCost(remapped_cum_freqs.get(), new_num_syms, target_total, log2cache.get(), &cache);

	for (uint32_t i = 0; i <= num_syms; ++i) {
		cum_freqs[i] = 0;
	}

	// Reconstruct the optimal choices from the cache. Note that during this,
	// cum_freq contains frequencies, _not_ cumulative frequencies.
	int available_slots = target_total;
	for (int symbol_idx = new_num_syms; symbol_idx --> 0; ) {  // :-)
		uint32_t freq;
		if (symbol_idx == 0) {
			// Last symbol isn't in the cache, but it's obvious what the answer is.
			freq = available_slots;
		} else {
			CacheKey cache_key{symbol_idx + 1, available_slots};
			assert(cache.count(cache_key));
			freq = cache[cache_key].chosen_freq;
		}
		cum_freqs[mapping[symbol_idx]] = freq;
		assert(available_slots >= freq);
		available_slots -= freq;
	}

	// Convert the frequencies back to cumulative frequencies.
	uint32_t total = 0;
	for (uint32_t i = 0; i <= num_syms; ++i) {
		uint32_t freq = cum_freqs[i];
		cum_freqs[i] = total;
		total += freq;
	}
}
