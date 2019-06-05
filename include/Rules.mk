# Standard things

sp 		:= $(sp).x
dirstack_$(sp)	:= $(d)
d		:= $(dir_)

dir_ := $(d)/librans
include $(dir_)/Rules.mk

d		:= $(dirstack_$(sp))
sp		:= $(basename $(sp))
