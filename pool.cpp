#include "pool.hpp"

size_t page_size = ([] {
    auto result = sysconf(_SC_PAGE_SIZE);

    if (result < 0) {
        perror("Cannot determine the page size");
        exit(EXIT_FAILURE);
    }

    // for peace of mind.
    assert((result & result - 1) == 0);

    return result;
})();
