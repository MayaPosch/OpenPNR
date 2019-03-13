# Makefile for the Trellis import tool.
#
# (c) 2019 Nyanko.ws

GCC = g++
MAKEDIR = mkdir -p
RM = rm -f

OUTPUT = trellis_import
INCLUDE = -I . -I ../../trellis/libtrellis/include
LIBS := -L ../../trellis/libtrellis/lib -ltrellis
CFLAGS := $(INCLUDE) -g3 -std=c++17 -O0

# Check for MinGW and patch up POCO
# The OS variable is only set on Windows.
ifdef OS
	CFLAGS := $(CFLAGS) -U__STRICT_ANSI__
	LIBS := $(LIBS) -lboost_system-mt -lPocoFoundation -lPocoJSON
	EXT = .exe
else
	LIBS := $(LIBS) -lboost_system -lboost_thread -lPocoFoundation -lPocoJSON
endif

SOURCES := $(wildcard *.cpp)
OBJECTS := $(addprefix obj/,$(notdir) $(SOURCES:.cpp=.o))

all: makedir $(OBJECTS) $(OUTPUT)

makedir:
	$(MAKEDIR) obj

obj/%.o: %.cpp
	$(GCC) -c -o $@ $< $(CFLAGS)
	
$(OUTPUT):
	$(RM) $@$(EXT)
	$(GCC) -o $@ $(OBJECTS) $(LIBS)

clean:
	$(RM) $(OBJECTS)
	