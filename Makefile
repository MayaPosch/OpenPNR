# Makefile for NextPNR.
##
# Author: Maya Posch

# Require ARCH to be set.
ifndef ARCH
$(error ARCH has not been set.)
endif

export TOP := $(CURDIR)

GPP = g++
GCC = gcc
MAKEDIR = mkdir -p
RM = rm
AR = ar
MAKE = make

# Use ARCH to include the architecture-specific file.
include ./Makefile.$(ARCH)

BIN_OUTPUT := nextpnr
INCLUDE = -I common/ -I json/ $(ARCH_INCLUDE)
FLAGS += -DNO_PYTHON=1 -DNO_GUI=1 -DMAIN_EXECUTABLE=1 $(ARCH_FLAGS)
CPPFLAGS := $(FLAGS) -g3 -std=c++14 $(INCLUDE)
ifdef OS
	LIBS += -lboost_filesystem-mt -lboost_program_options-mt -lboost_system-mt
else
	LIBS += -lboost_filesystems -lboost_program_options -lboost_system
endif

CPP_SOURCES := $(wildcard common/*.cc) $(wildcard json/*.cc)
CPP_OBJECTS := $(addprefix obj/,$(notdir) $(CPP_SOURCES:.cc=.o))

all: trellis import pnr_build

trellis:
	$(MAKE) -C trellis/libtrellis/
	
import: import_tool bbasm db_import db_convert
	
import_tool:
	$(MAKE) -C ecp5/tools/
	
bbasm:
	$(GPP) -o bba/bbasm bba/main.cc $(LIBS)
	
db_import:
	cd ecp5/tools/ && ./trellis_import -p ../constids.inc 25k
	cd ecp5/tools/ && ./trellis_import -p ../constids.inc 45k
	cd ecp5/tools/ && ./trellis_import -p ../constids.inc 85k
	
db_convert:
	bba/bbasm --c ecp5/chipdbs/chipdb-25k.bba ecp5/chipdbs/chipdb-25k.cc
	bba/bbasm --c ecp5/chipdbs/chipdb-45k.bba ecp5/chipdbs/chipdb-45k.cc
	bba/bbasm --c ecp5/chipdbs/chipdb-85k.bba ecp5/chipdbs/chipdb-85k.cc
	
pnr_build: makedir $(CPP_OBJECTS) $(ARCH_OBJECTS) $(BIN_OUTPUT)
	
obj/%.o: %.cc
	$(GPP) -c -o $@ $< $(CPPFLAGS)
	
obj/%.o: %.cpp
	$(GPP) -c -o $@ $< $(CPPFLAGS)
	
$(BIN_OUTPUT):
	$(GPP) -o bin/$@-$(ARCH) $(CPPFLAGS) $(CPP_OBJECTS) $(ARCH_OBJECTS) $(LIBS)
	
makedir:
	$(MAKEDIR) bin
	$(MAKEDIR) obj/common/
	$(MAKEDIR) obj/json/
	$(MAKEDIR) obj/ecp5/chipdbs/
	$(MAKEDIR) obj/$(ARCH)/resource

clean:
	$(RM) $(CPP_OBJECTS) $(ARCH_OBJECTS)
	
.PHONY: trellis clean makedir import bbasm
	