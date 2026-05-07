# Memory experiments
This repository houses various memory experiments.
To build them, run `make`.
You'll need a C++20-capable compiler.

## Single-threaded allocation (`alloc`)
`alloc.cpp` allocates a linked list of 10 million elements (each node being `sizeof(void *) + sizeof(size_t)` in size) and measures resource usage (via `getrusage`).

The allocation is performed in one of the two ways (selected with a command-line argument):
- `malloc` uses C++'s builtin `new`/`delete` operators, which essentially amount to `malloc`/`free`.
- `mmap` allocates memory pages (with the `MAP_GROWSDOWN` flag) as well as guard pages at either end protected against any access to catch memory errors.

The results on my machine:

```
$ ./alloc malloc
Time used: 103961 usec
Memory used: 314888192 bytes
Overhead: 49.2%

$ ./alloc mmap
Time used: 3314 usec
Memory used: 156770304 bytes
Overhead: -2.1%
```

The memory measurements are clearly suspect.
For `mmap`, the actual breakdown is as follows:
- 160002048 bytes allocated for the elements
  - the extra 2048 B (0.001%) are there to align at a page boundary
- 4096 bytes protected but aren't read or written to, only taking up space in the page table

(Confirmed by running GDB and inspecting `info proc mappings`.)

## Multi-threaded allocation (`threaded`)
`threaded.cpp` is basically `alloc.cpp` but multi-threaded.
It spawns 16 threads and allocates a 10-million-element list in each.
It takes a command-line argument that chooses the particular allocation strategy:

- `malloc` uses C++'s builtin `new`/`delete` operators, which essentially amount to `malloc`/`free`.
- `mutex` allocates a shared pool protected with a mutex.
- `atomic` replaces the mutex with atomic operations.
- `thread-local` does away with the shared pool altogether and instead creates one per thread.

The results on my machine:

```
$ ./threaded malloc
Running with 16 threads
Time used: 2328241 usec
Memory used: 5113348096 bytes
Overhead: 49.9%

$ ./threaded mutex
Running with 16 threads
Time used: 22862510 usec
Memory used: 2553077760 bytes
Overhead: -0.3%

$ ./threaded atomic
Running with 16 threads
Time used: 88821150 usec
Memory used: 2553204736 bytes
Overhead: -0.3%

$ ./threaded thread-local
Running with 16 threads
Time used: 406772 usec
Memory used: 2554327040 bytes
Overhead: -0.2%
```

Or, in a table:

Strategy | Time (utime, μs) | Time (wall, s) | Memory (B) | Reported overhead (%)
:--------|:-----------------|:---------------|:-----------|:---------------------
`malloc` | 2328241 | 2.02 | 5113348096 | 49.9
`mutex` | 22862510 | 9.06 | 2553077760 | -0.3
`atomic` | 88821150 | 6.63 | 2553204736 | -0.3
`thread-local` | 406772 | 0.21 | 2554327040 | -0.2

## String reference (`string-ref.hpp`)
A small reference-counted string implementation, with one caveat: the counter takes only 1 bit.
In other words, the reference wrapper only tracks uniqueness (conservatively).
As long as the reference is not copied, it's guaranteed to be unique, allowing to free the backing storage on destruction.
If it *is* copied, however, the string's contents won't ever be freed, causing a memory leak.

A small test suite is provided in `string-ref-test.cpp`.
Run `string-ref-test` after building with Make.

## Protected memory load (`safe-read.cpp`)
An implementation of a protected memory load routine, which allows reading a byte at an arbitrary memory location.
If the byte is inaccessible, returns `std::nullopt`.

The program is only valid provided it's run single-threaded.

## Parallel memory copy (`parallel-copy.cpp`)
Measures how long it takes to copy a chunk of memory (256 MB) in parallel.
The number of worker threads is specified via a command-line argument, with the default being the number of available threads minus one.
This is because the thread initiating the copy does a part of the copying itself in addition to the worker threads.

On my machine, the results are as follows:

No. of worker threads | Time (ms)
:--------------------:|:---------
0 | 14.9
1 | 12.2
2 | 10.2
3 | 8.7
4 | 9.4
5 | 8.4
6 | 8.8
7 | 8.4
8 | 8.6
