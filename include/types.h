#pragma once

#include <cstdint>
#include <queue>

using HNetQueue = std::queue<void*>;

struct HNetBuffer
{
    void* data;
    size_t dataLength;
};
