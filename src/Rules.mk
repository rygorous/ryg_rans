# Standard things

sp 		:= $(sp).x
dirstack_$(sp)	:= $(d)
d		:= $(dir_)

SRC_$(d) :=  $(d)/helper.cpp $(d)/rans32benchmark.cpp $(d)/rans64benchmark.cpp
OBJS_$(d) := $(subst .cpp,.o,$(SRC_$(d)))
DEPS_$(d) := $(subst .cpp,.d,$(SRC_$(d)))


$(OBJS_$(d)): CXXFLAGS_TGT:= -I$(d) -Iinclude

TGT_BIN:= $(TGT_BIN) rans32benchmark.exe rans64benchmark.exe

rans32benchmark.exe: $(d)/rans32benchmark.o $(d)/helper.o $(TGT_LIB)
	$(LINK)
	
rans64benchmark.exe: $(d)/rans64benchmark.o $(d)/helper.o $(TGT_LIB)
	$(LINK)

CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(TGT_LIB) $(TGT_BIN) 
	
# Standard things

-include $(DEPS_$(d))

d		:= $(dirstack_$(sp))
sp		:= $(basename $(sp))