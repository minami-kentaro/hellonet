#pragma once

#include "types.h"

// http://lists.cubik.org/pipermail/enet-discuss/2004-February/000175.html
#define HNET_TIME_OVERFLOW 86400000

#define HNET_TIME_LT(a, b) ((a) - (b) >= HNET_TIME_OVERFLOW)
#define HNET_TIME_GE(a, b) (!HNET_TIME_LT(a, b))
#define HNET_TIME_DIFF(a, b) ((a) - (b) >= HNET_TIME_OVERFLOW ? (b) - (a) : (a) - (b))

uint64_t hnet_time_now_msec();
uint64_t hnet_time_now_sec();
