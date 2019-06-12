#include "helper.h"

#include <cstdarg>
#include <cstdlib>

void panic(const char *fmt, ...)
{
    va_list arg;

    va_start(arg, fmt);
    fputs("Error: ", stderr);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    fputs("\n", stderr);

    exit(1);
}

void read_args(int argc, char** argv, cmd_args& args){

    if (argc > 1)
    {
        args.filename = argv[1];
        args.prob_bits = (argc>2) ? std::stoi(argv[2]):0;
    }else{
    	throw std::runtime_error("syntax main.exe <filename> [<probability_bits>]");
    }
}

std::string toString(ExecutionMode mode)
{

	switch (mode) {
		case ExecutionMode::NonInterleaved:
			return "NonInterleaved";
			break;
		case ExecutionMode::Interleaved:
			return "Interleaved";
			break;
		default:
			throw std::runtime_error("unknown ExecutionMode");
			break;
	}
}

std::string toString(CodingMode mode){
	switch (mode) {
		case CodingMode::Encode:
			return "Encode";
			break;
		case CodingMode::Decode:
			return "Decode";
			break;
		default:
			throw std::runtime_error("unknown CodingMode");
			break;
	}
}
