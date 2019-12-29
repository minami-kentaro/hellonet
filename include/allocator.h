#pragma once

#include <stdlib.h>
#include "types.h"

struct HNetAllocator
{
    void*(*malloc)(size_t);
    void(*free)(void*);
    void(*no_memory)();
};

void* hnet_malloc(size_t size);
void hnet_free(void* ptr);