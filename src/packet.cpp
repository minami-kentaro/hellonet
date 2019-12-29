#include "allocator.h"
#include "packet.h"

void hnet_packet_destroy(HNetPacket* pPacket)
{
    if (pPacket == nullptr) {
        return;
    }
    if (pPacket->freeCallback != nullptr) {
        (*pPacket->freeCallback)(pPacket);
    }
    if (!(pPacket->flags & HNET_PACKET_FLAG_NO_ALLOCATE) && pPacket->data != nullptr) {
        hnet_free(pPacket->data);
    }
    hnet_free(pPacket);
}