	CXXFLAGS = -MD -MP -Wall -Wswitch -Werror -g -std=c++17 -march=native
LDFLAGS =  -fPIC
LDLIBS = 

# Includes
INCLUDES := -I. -Iinclude

CXX = g++
COMP = $(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ -c $<
LINK = $(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
COMPLINK = $(CXX) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $< $(LDLIBS)


OBJS := helper.o main_alias.o main_simd.o main.o main64.o main_nonchar.o main64_nonchar.o main32_nonchar.o main32_nonchar_direct.o
DEPS := $(subst .o,.d,$(OBJS))
TARGET := main_alias.exe main_simd.exe main.exe main64.exe main32_nonchar.exe main64_nonchar.exe main32_nonchar_direct.exe

.SUFFIXES:	.cpp .o	

ifdef DEBUG
CXXFLAGS += -O0 -ggdb3 -DDEBUG
else
CXXFLAGS += -O3
endif


# General directory independent rules
%.o:		%.cpp
			$(COMP)

%:			%.o
			$(LINK)

%:			%.cpp
			$(COMPLINK)			

all: $(TARGET)

main_alias.exe: main_alias.o
	$(LINK)
	
main_simd.exe: main_simd.o
	$(LINK)
	
main.exe: main.o
	$(LINK)
	
main64.exe: main64.o
	$(LINK)
	
main32_nonchar.exe: main32_nonchar.o helper.o
	$(LINK)
	
main64_nonchar.exe: main64_nonchar.o helper.o
	$(LINK)
	
main32_nonchar_direct.exe: main32_nonchar_direct.o helper.o
	$(LINK)
		

#$(TARGET): $(OBJS) Makefile
#	$(LINK)

.PHONY:		clean
clean: 
	rm -rf $(TARGET) $(OBJS) $(DEPS) 
	
-include $(DEPS)
