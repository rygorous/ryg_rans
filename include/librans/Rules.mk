# Standard things

sp 		:= $(sp).x
dirstack_$(sp)	:= $(d)
d		:= $(dir_)

SRC_$(d) :=  $(d)/SymbolStatistics.cpp
OBJS_$(d) := $(subst .cpp,.o,$(SRC_$(d)))
DEPS_$(d) := $(subst .cpp,.d,$(SRC_$(d)))

DIR_$(d) := lib
TGT_NAME_$(d) := $(DIR_$(d))/librans.a

CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(TGT_LIB) $(TGT_NAME_$(d))

$(OBJS_$(d)): CXXFLAGS_TGT:= -I$(d) 
	LDFLAGS:= $(LDFLAGS) -L$(DIR_$(d)) 

TGT_LIB := $(TGT_LIB) $(TGT_NAME_$(d))

$(TGT_NAME_$(d)): $(OBJS_$(d))
	mkdir -p lib
	$(STATIC)

	
# Standard things

-include $(DEPS_$(d))

d		:= $(dirstack_$(sp))
sp		:= $(basename $(sp))