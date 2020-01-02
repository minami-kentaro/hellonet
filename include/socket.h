#pragma once

#include <arpa/inet.h>
#include "types.h"

using HNetSocket = int32_t;

enum class HNetSocketType : uint8_t
{
    Stream,
    DataGram,
};

enum class HNetSocketOption : uint8_t
{
    NONBLOCK,
    BROADCAST,
    RCVBUF,
    SNDBUF,
    REUSEADDR,
    RCVTIMEO,
    SNDTIMEO,
    ERROR,
    NODELAY,
};

#define HNET_SOCKET_WAIT_NONE 0
#define HNET_SOCKET_WAIT_SEND (1 << 0)
#define HNET_SOCKET_WAIT_RECV (1 << 1)
#define HNET_SOCKET_WAIT_INTR (1 << 2)

#define HNET_HOST_TO_NET_16(value) (htons(value))
#define HNET_HOST_TO_NET_32(value) (htonl(value))
#define HNET_NET_TO_HOST_16(value) (ntohs(value))
#define HNET_NET_TO_HOST_32(value) (ntohl(value))
#define HNET_SOCKET_NULL -1

bool hnet_address_set_host(HNetAddr& addr, const char* pHostName);
bool hnet_address_set_host_ip(HNetAddr& addr, const char* pHostName);

HNetSocket hnet_socket_create(HNetSocketType type);
bool hnet_socket_bind(HNetSocket socket, HNetAddr& addr);
void hnet_socket_destroy(HNetSocket socket);
bool hnet_socket_set_option(HNetSocket socket, HNetSocketOption option, int32_t val);
bool hnet_socket_get_addr(HNetSocket socket, HNetAddr& addr);
bool hnet_socket_wait(HNetSocket socket, uint32_t& cond, uint32_t timeout);
int32_t hnet_socket_send(HNetSocket socket, HNetAddr& addr, HNetBuffer* pBuffers, size_t bufferCount);
int32_t hnet_socket_recv(HNetSocket socket, HNetAddr& addr, HNetBuffer* pBuffers, size_t bufferCount);
