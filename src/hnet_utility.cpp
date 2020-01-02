#include "hnet_utility.h"
#include "peer.h"

uint16_t hnet_calc_reliable_window(uint16_t seqNumber)
{
    return seqNumber / HNET_PEER_RELIABLE_WINDOW_SIZE;
}