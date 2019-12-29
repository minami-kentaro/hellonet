#include "allocator.h"

static HNetAllocator allocator = {malloc, free, abort};

void* hnet_malloc(size_t size)
{
    void* ptr = allocator.malloc(size);
    if (ptr == nullptr) {
        allocator.no_memory();
    }
    return ptr;
}

void hnet_free(void* ptr)
{
    allocator.free(ptr);
}
