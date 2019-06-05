# Standard things

.SUFFIXES:
.SUFFIXES:	.cpp .o

all: tgts	

ifdef DEBUG
CXXFLAGS_ALL += -O0 -ggdb3 -DDEBUG
else
CXXFLAGS_ALL += -O3
endif

# include all modules

dir_ := include
include $(dir_)/Rules.mk
dir_ := src
include $(dir_)/Rules.mk

# General directory independent rules
%.o:		%.cpp
			$(COMP)

%:			%.o
			$(LINK)

%:			%.cpp
			$(COMPLINK)
			
.PHONY:		tgts
tgts:		$(TGT_LIB) $(TGT_BIN)

.PHONY:		lib
lib:		$(TGT_LIB)

.PHONY:		clean
clean:
			rm -rf bin $(CLEAN)

# Prevent make from removing any build targets, including intermediate ones
.SECONDARY:	$(CLEAN)
