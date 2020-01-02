#include "allocator.h"
#include "packet.h"

HNetPacket* hnet_packet_create(uint8_t* pData, size_t dataLength, uint32_t flags)
{
    HNetPacket* pPacket = static_cast<HNetPacket*>(hnet_malloc(sizeof(HNetPacket)));
    if (pPacket == nullptr) {
        return nullptr;
    }

    if (flags & HNET_PACKET_FLAG_NO_ALLOCATE) {
        pPacket->data = pData;
    } else if (dataLength <= 0) {
        pPacket->data = nullptr;
    } else {
        pPacket->data = static_cast<uint8_t*>(hnet_malloc(dataLength));
        if (pPacket->data == nullptr) {
            hnet_free(pPacket);
            return nullptr;
        }
        if (pData != nullptr) {
            memcpy(pPacket->data, pData, dataLength);
        }
    }

    pPacket->refCount = 0;
    pPacket->flags = flags;
    pPacket->dataLength = dataLength;
    pPacket->freeCallback = nullptr;
    pPacket->userData = nullptr;
    return pPacket;
}

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