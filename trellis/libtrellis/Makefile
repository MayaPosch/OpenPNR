# Makefile for Trellis.
##
# Author: Maya Posch

export TOP := $(CURDIR)

GPP = g++
GCC = gcc
MAKEDIR = mkdir -p
RM = rm -f
AR = ar

LIB_OUTPUT = libtrellis.a
#PYMOD_OUTPUT = pytrellis
BIN_OUTPUT = ecppack ecppll ecpunpack ecpmulti
INCLUDE = -I include/ 
FLAGS += -DTRELLIS_PREFIX=\"/usr\"
CPPFLAGS := $(FLAGS) -g3 -std=c++14 $(INCLUDE)
#PY_FLAGS := -DINCLUDE_PYTHON=1 -D__MINGW32__=1
LIBS += -Llib/ -ltrellis 
CPP_SOURCES := $(wildcard src/*.cpp)
CPP_OBJECTS := $(addprefix obj/,$(notdir) $(CPP_SOURCES:.cpp=.o))
#PY_OBJECTS := $(addprefix obj_py/,$(notdir) $(CPP_SOURCES:.cpp=.o))
BIN_OBJECTS := $(addprefix bin/,$(notdir) $(BIN_OUTPUT))

ifdef OS
	LIBS := $(LIBS) -lboost_filesystem-mt -lboost_thread-mt -lboost_program_options-mt -lboost_system-mt
	EXT = .exe
else
	LIBS := $(LIBS) -lboost_filesystem -lboost_thread -lboost_program_options -lboost_system
endif

all: makedir $(CPP_OBJECTS) lib/$(LIB_OUTPUT) $(BIN_OUTPUT)
	
obj/%.o: %.cpp
	$(GPP) -c -o $@ $< $(CPPFLAGS)
	
# obj_py/%.o: %.cpp
	# $(GPP) -c -o $@ $< $(CPPFLAGS) $(PY_FLAGS)
	
lib/$(LIB_OUTPUT): $(CPP_OBJECTS)
	-rm -f $@
	$(AR) rcs $@ $^
	
# lib/$(PYMOD_OUTPUT): $(CPP_OBJECTS)
	# rm -f $@
	# $(AR) rcs $@ $^
	
$(BIN_OUTPUT):
	$(RM) bin/$@$(EXT)
	$(GPP) -o bin/$@ tools/$@.cpp $(CPPFLAGS) $(LIBS)
	
makedir:
	$(MAKEDIR) lib
	$(MAKEDIR) bin
	$(MAKEDIR) obj/src/
	#$(MAKEDIR) obj_py/src/

clean:
	$(RM) $(CPP_OBJECTS)
	