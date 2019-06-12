# Standard things

sp 		:= $(sp).x
dirstack_$(sp)	:= $(d)
d		:= $(dir_)

SRC_$(d) :=  $(d)/helper.cpp
OBJS_$(d) := $(subst .cpp,.o,$(SRC_$(d)))
DEPS_$(d) := $(subst .cpp,.d,$(SRC_$(d)))


$(OBJS_$(d)): CXXFLAGS_TGT:= $(CXXFLAGS_TGT)-I$(d) -Iinclude
$(OBJS_$(d)): $(SRC_$(d))
	$(COMP)

TGT_BIN:= $(TGT_BIN) rans32benchmark.exe rans64benchmark.exe

rans64benchmark.exe: CXXFLAGS_TGT:= $(CXXFLAGS_TGT) -I$(d) -Iinclude
rans64benchmark.exe: $(d)/ransBenchmark.cpp $(OBJS_$(d)) $(TGT_LIB)
	$(COMPLINK)

rans32benchmark.exe: CXXFLAGS_TGT:= $(CXXFLAGS_TGT) -I$(d) -Iinclude -Drans32
rans32benchmark.exe: $(d)/ransBenchmark.cpp  $(OBJS_$(d)) $(TGT_LIB)
	$(COMPLINK)

CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(TGT_LIB) $(TGT_BIN) 
	
# Standard things

-include $(DEPS_$(d))

d		:= $(dirstack_$(sp))
sp		:= $(basename $(sp))