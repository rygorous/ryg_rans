LIBS=-lm -lrt

all: exam exam64 exam_simd_sse41 exam_alias

exam: main.cpp platform.h rans_byte.h
	g++ -o $@ $< -O3 $(LIBS)

exam64: main64.cpp platform.h rans64.h
	g++ -o $@ $< -O3 $(LIBS)

exam_simd_sse41: main_simd.cpp platform.h rans_word_sse41.h
	g++ -o $@ $< -O3 -msse4.1 $(LIBS)

exam_alias: main_alias.cpp platform.h rans_byte.h
	g++ -o $@ $< -O3 $(LIBS)
