#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include "socket.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

bool hnet_address_set_host(HNetAddr& addr, const char* pHostName)
{
    addrinfo* pResultList = nullptr;

    if (getaddrinfo(pHostName, nullptr, nullptr, &pResultList) != 0) {
        return false;
    }

    for (addrinfo* pResult = pResultList; pResult != nullptr; pResult = pResult->ai_next) {
        if (pResult->ai_family == AF_INET && pResult->ai_addr != nullptr && pResult->ai_addrlen >= sizeof(sockaddr_in)) {
            sockaddr_in* pSin = reinterpret_cast<sockaddr_in*>(pResult->ai_addr);
            addr.host = pSin->sin_addr.s_addr;
            freeaddrinfo(pResultList);
            return true;
        }
    }

    if (pResultList != nullptr) {
        freeaddrinfo(pResultList);
    }

    return hnet_address_set_host_ip(addr, pHostName);
}

bool hnet_address_set_host_ip(HNetAddr& addr, const char* pHostName)
{
    return inet_pton(AF_INET, pHostName, &addr.host) == 1;
}

HNetSocket hnet_socket_create(HNetSocketType type)
{
    return socket(PF_INET, type == HNetSocketType::DataGram ? SOCK_DGRAM : SOCK_STREAM, 0);
}

bool hnet_socket_bind(HNetSocket socket, HNetAddr& addr)
{
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = HNET_HOST_TO_NET_16(addr.port);
    sin.sin_addr.s_addr = addr.host;
    return bind(socket, reinterpret_cast<sockaddr*>(&sin), sizeof(sockaddr_in)) == 0;
}

void hnet_socket_destroy(HNetSocket socket)
{
    if (socket != HNET_SOCKET_NULL) {
        close(socket);
    }
}

bool hnet_socket_set_option(HNetSocket socket, HNetSocketOption option, int32_t val)
{
    int32_t result = -1;

    switch (option) {
    case HNetSocketOption::NONBLOCK:
        result = fcntl(socket, F_SETFL, (val ? O_NONBLOCK : 0) | (fcntl(socket, F_GETFL) & ~O_NONBLOCK));
        break;
    case HNetSocketOption::BROADCAST:
        result = setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&val), sizeof(int32_t));
        break;
    case HNetSocketOption::REUSEADDR:
        result = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&val), sizeof(int32_t));
        break;
    case HNetSocketOption::RCVBUF:
        result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&val), sizeof(int32_t));
        break;
    case HNetSocketOption::SNDBUF:
        result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&val), sizeof(int32_t));
        break;
    case HNetSocketOption::RCVTIMEO:
        {
            timeval timeVal;
            timeVal.tv_sec = val / 1000;
            timeVal.tv_usec = (val % 1000) * 1000;
            result = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeVal), sizeof(timeval));
        }
        break;
    case HNetSocketOption::SNDTIMEO:
        {
            timeval timeVal;
            timeVal.tv_sec = val / 1000;
            timeVal.tv_usec = (val % 1000) * 1000;
            result = setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeVal), sizeof(timeval));
        }
        break;
    case HNetSocketOption::NODELAY:
        result = setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&val), sizeof(int32_t));
        break;
    default:
        break;
    }

    return result == 0;
}

bool hnet_socket_get_addr(HNetSocket socket, HNetAddr& addr)
{
    sockaddr_in sin{};
    socklen_t sinLength = sizeof(sockaddr_in);

    if (getsockname(socket, reinterpret_cast<sockaddr*>(&sin), &sinLength) == -1) {
        return false;
    }

    addr.host = static_cast<uint32_t>(sin.sin_addr.s_addr);
    addr.port = HNET_NET_TO_HOST_16(sin.sin_port);
    return true;
}

bool hnet_socket_wait(HNetSocket socket, uint32_t& cond, uint32_t timeout)
{
    pollfd pollSocket{socket, 0, 0};

    if (cond & HNET_SOCKET_WAIT_SEND) {
        pollSocket.events |= POLLOUT;
    }
    if (cond & HNET_SOCKET_WAIT_RECV) {
        pollSocket.events |= POLLIN;
    }

    int32_t pollCount = poll(&pollSocket, 1, timeout);
    if (pollCount < 0) {
        if (errno == EINTR && (cond & HNET_SOCKET_WAIT_INTR)) {
            cond = HNET_SOCKET_WAIT_INTR;
            return true;
        }
        return false;
    }

    cond = HNET_SOCKET_WAIT_NONE;
    if (pollCount > 0) {
        if (pollSocket.revents & POLLOUT) {
            cond |= HNET_SOCKET_WAIT_SEND;
        }
        if (pollSocket.revents & POLLIN) {
            cond |= HNET_SOCKET_WAIT_RECV;
        }
    }

    return true;
}

int32_t hnet_socket_recv(HNetSocket socket, HNetAddr& addr, HNetBuffer* pBuffers, size_t bufferCount)
{
    msghdr msgHdr{};
    sockaddr_in sin{};

    msgHdr.msg_name = &sin;
    msgHdr.msg_namelen = sizeof(sockaddr_in);
    msgHdr.msg_iov = reinterpret_cast<iovec*>(pBuffers);
    msgHdr.msg_iovlen = bufferCount;

    int32_t recvLength = recvmsg(socket, &msgHdr, MSG_NOSIGNAL);
    if (recvLength == -1) {
        if (errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    } else if (msgHdr.msg_flags & MSG_TRUNC) {
        return -1;
    }

    addr.host = static_cast<uint32_t>(sin.sin_addr.s_addr);
    addr.port = HNET_NET_TO_HOST_16(sin.sin_port);
    return recvLength;
}
