#pragma once

#include "platform.h"
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

void panic(const char *fmt, ...);

struct cmd_args
{
	std::string filename;
	uint prob_bits;
};

void read_args(int argc, char** argv, cmd_args& args);

template <typename T>
void read_file(const std::string& filename, std::vector<T>* tokens ){
	std::ifstream is (filename, std::ios_base::binary|std::ios_base::in);
	if (is) {
		// get length of file:
		is.seekg (0, is.end);
		size_t length = is.tellg();
		is.seekg (0, is.beg);//	    for(int i = 0; i<static_cast<int>(freqs.size()); i++){
		//	    	std::cout << i << ": " << i + min << " " << freqs[i] << " " << cum_freqs[i] << std::endl;
		//	    }
		//	    std::cout <<  cum_freqs.back() << std::endl;

		// reserve size of tokens
		if (!tokens){
			throw std::runtime_error("Cannot read file into nonexistent vector");
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
}
