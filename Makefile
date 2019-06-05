CXXFLAGS_ALL = -MD -MP -Wall -Wswitch -Werror -pedantic -g -std=c++17 -march=native -fno-strict-aliasing
LDFLAGS =  
LDLIBS = 

CXX = g++
COMP = $(CXX) $(CXXFLAGS_ALL) $(CXXFLAGS_TGT) -o $@ -c $<
LINK = $(CXX) $(LDFLAGS) $(LDFLAGS_TGT) -o $@ $^ $(LDLIBS_TGT) $(LDLIBS)
COMPLINK = $(CXX) $(CXXFLAGS_ALL) $(CXXFLAGS_TGT) $(LDFLAGS) $(LDFLAGS_TGT) -o $@ $< $(LDLIBS_TGT) $(LDLIBS)
STATIC = ar rcs $@ $^

include Rules.mk
