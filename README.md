# Memory allocation experiments
`alloc.cpp` allocates a linked list of 10 million elements (each node being `sizeof(void *) + sizeof(size_t)` in size) and measures resource usage (via `getrusage`).

The allocation is performed in one of the two ways (selected with a command-line argument):
- `malloc` uses C++'s builtin `new`/`delete` operators, which essentially amount to `malloc`/`free`.
- `mmap` allocates memory pages (with the `MAP_GROWSDOWN` flag) as well as guard pages at either end protected against any access to catch memory errors.

To build the program, run `make`.

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
