CXX ?= clang++
CXXFLAGS ?= -O2

.PHONY: all clean

all: alloc

clean:
	rm -f alloc *.o

alloc: blackbox.o alloc.o
	$(CXX) -std=c++11 $(CXXFLAGS) $^ -o $@

%.o: %.c
	$(CXX) -std=c++11 $(CXXFLAGS) $< -o $@

alloc.cpp: blackbox.cpp
