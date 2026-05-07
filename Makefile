CXX ?= clang++
CXXFLAGS ?= -O2

.PHONY: all clean

all: alloc threaded string-ref-test safe-read parallel-copy

clean:
	rm -f alloc threaded string-ref-test *.o

alloc: alloc.o blackbox.o pool.o
	$(CXX) -D_POSIX_C_SOURCE=202405L -std=c++20 $(CXXFLAGS) $^ -o $@

threaded: threaded.o blackbox.o pool.o
	$(CXX) -D_POSIX_C_SOURCE=202405L -std=c++20 $(CXXFLAGS) -pthread $^ -o $@

string-ref-test: string-ref-test.o
	$(CXX) -std=c++20 $(CXXFLAGS) $^ -o $@

safe-read: safe-read.o blackbox.o
	$(CXX) -D_POSIX_C_SOURCE=202405L -std=c++20 $(CXXFLAGS) $^ -o $@

parallel-copy: parallel-copy.o blackbox.o
	$(CXX) -std=c++20 $(CXXFLAGS) $^ -o $@

%.o: %.cpp
	$(CXX) -D_POSIX_C_SOURCE=202405L -std=c++20 $(CXXFLAGS) -c $< -o $@

threaded.o: blackbox.hpp pool.hpp
alloc.o: blackbox.hpp pool.hpp
pool.o: pool.hpp
string-ref-test.o: string-ref.hpp
parallel-copy.o: blackbox.hpp
