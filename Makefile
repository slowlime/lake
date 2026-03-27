CXX ?= clang++
CXXFLAGS ?= -O2

.PHONY: all clean

all: alloc threaded

clean:
	rm -f alloc threaded *.o

alloc: alloc.o blackbox.o pool.o
	$(CXX) -std=c++20 $(CXXFLAGS) $^ -o $@

threaded: threaded.o blackbox.o pool.o
	$(CXX) -std=c++20 $(CXXFLAGS) -pthread $^ -o $@

%.o: %.cpp
	$(CXX) -std=c++20 $(CXXFLAGS) -c $< -o $@

threaded.o: blackbox.hpp pool.hpp
alloc.o: blackbox.hpp pool.hpp
pool.o: pool.hpp
