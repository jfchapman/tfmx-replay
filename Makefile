# A plain makefile for GNU make with gcc or clang (Linux, macOS, MinGW).
# CMake is the main build (see README.md); this is for when CMake isn't wanted.
#
#   make              static library + tools
#   make shared       shared library (libtfmxreplay.so)
#   make CC=clang CXX=clang++

CC ?= cc
CXX ?= c++
CFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += -std=c++17 -Iinclude -Isrc
CFLAGS += -std=c99 -Iinclude
AR ?= ar

SOURCES = src/module.cpp src/paula.cpp src/replayer.cpp src/tfmxreplay.cpp src/variant.cpp
OBJECTS = $(SOURCES:.cpp=.o)

all: libtfmxreplay.a tfmxrender tfmxtrace

libtfmxreplay.a: $(OBJECTS)
	$(AR) rcs $@ $^

shared: CXXFLAGS += -fPIC -fvisibility=hidden -DTFMX_SHARED -DTFMX_BUILDING
shared: $(SOURCES)
	$(CXX) $(CXXFLAGS) -shared -o libtfmxreplay.so $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# tfmxrender is C; linking with the C++ compiler brings in the C++ runtime the library needs.
tools/tfmxrender.o: tools/tfmxrender.c include/tfmxreplay.h
	$(CC) $(CFLAGS) -c -o $@ $<

tfmxrender: tools/tfmxrender.o libtfmxreplay.a
	$(CXX) -o $@ $^ -lm

tfmxtrace: tools/tfmxtrace.cpp libtfmxreplay.a
	$(CXX) $(CXXFLAGS) -o $@ tools/tfmxtrace.cpp libtfmxreplay.a

clean:
	rm -f $(OBJECTS) tools/tfmxrender.o libtfmxreplay.a libtfmxreplay.so tfmxrender tfmxtrace

.PHONY: all shared clean
