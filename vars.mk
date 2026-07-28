###
# CROSS variable
# Use for cross-compilation.
# Uncommenting and setting this variable to the prefix of
# your cross compiler will allow you to cross compile this library.
###
#CROSS=powerpc-linux-gnu

###
# LANG variable
# Set this value according to the language
# you wish to compile against.
# Possible (legal) values:
#   - C [c]
#   - C++ [c++]
###
LANG := "C"

###
# STD variables.
# Do NOT set the STD variable.
# Instead, set the C/C++ STD variables 
# according to the standard you wish to use.
###
CSTD := "gnu99"
CPPSTD := "c++0x"

###
# OUTPUT_NAME variable.
# This variable contains the name of the output file (the .so).
###
LIB_NAME := "libisotp.so"
MAJOR_VER := "1"
MINOR_VER := "8"
REVISION := "0"
OUTPUT_NAME := $(LIB_NAME).$(MAJOR_VER).$(MINOR_VER).$(REVISION)

###
# INSTALL_DIR variable.
# Set this variable to the location where the lib should be installed.
###
INSTALL_DIR := /usr/lib

###
# MAX_CAN_FRAME_SIZE variable.
# The maximum amount of data bytes a single CAN frame may carry.
# Set this to 8 for Classical CAN, or to one of the CAN FD frame lengths
# (12, 16, 20, 24, 32, 48, 64) to enable CAN FD support.
###
MAX_CAN_FRAME_SIZE ?= 8

###
# Compute compiler and language standard to use
# This section determines which compiler to use.
###
ifeq ($(LANG), "C++")
STD := "-std=$(CPPSTD)"
ifeq ($(strip $(CROSS)),)
COMP := g++
else
COMP := $(CROSS)g++
endif
endif
ifeq ($(LANG), "C")
STD := "-std=$(CSTD)"
ifeq ($(strip $(CROSS)),)
COMP := gcc
else
COMP := $(CROSS)gcc
endif
endif
