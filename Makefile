CXX ?= g++
CXXFLAGS := -O3 $(CXXFLAGS)
LIBS = -lm -lrt

all: rygrans-exam rygrans-exam64 rygrans-exam_alias rygrans-exam_simd_sse41

rygrans-exam: main.cpp platform.h rans_byte.h
	$(CXX) -o $@ $< $(CXXFLAGS) $(LIBS)

rygrans-exam64: main64.cpp platform.h rans64.h
	$(CXX) -o $@ $< $(CXXFLAGS) $(LIBS)

rygrans-exam_alias: main_alias.cpp platform.h rans_byte.h
	$(CXX) -o $@ $< $(CXXFLAGS) $(LIBS)

rygrans-exam_simd_sse41: main_simd.cpp platform.h rans_word_sse41.h
	$(CXX) -o $@ $< $(CXXFLAGS) -msse4.1 $(LIBS)
