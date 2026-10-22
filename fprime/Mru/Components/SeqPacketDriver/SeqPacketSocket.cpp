// ======================================================================
// \title  SeqPacketSocket.cpp
// \brief  F´'s socket helper over an inherited SOCK_SEQPACKET descriptor
// ======================================================================

#include "Mru/Components/SeqPacketDriver/SeqPacketSocket.hpp"

#include <sys/socket.h>

namespace Mru {

SeqPacketSocket::SeqPacketSocket() : Drv::IpSocket(), m_fd(-1), m_used(false) {}

Drv::SocketIpStatus SeqPacketSocket::configureDescriptor(int fd, U32 sendTimeoutSeconds, U32 sendTimeoutMicroseconds) {
    if (fd < 0 || sendTimeoutMicroseconds >= 1000000) {
        return Drv::SOCK_INVALID_CALL;
    }
    this->m_fd = fd;
    this->m_used = false;
    this->m_timeoutSeconds = sendTimeoutSeconds;
    this->m_timeoutMicroseconds = sendTimeoutMicroseconds;
    return Drv::SOCK_SUCCESS;
}

Drv::SocketIpStatus SeqPacketSocket::openProtocol(Drv::SocketDescriptor& socketDescriptor) {
    // the descriptor is opened once; after the peer is gone there is nothing to reopen
    if (this->m_fd < 0 || this->m_used) {
        return Drv::SOCK_FAILED_TO_CONNECT;
    }
    if (this->setupTimeouts(this->m_fd) != Drv::SOCK_SUCCESS) {
        return Drv::SOCK_FAILED_TO_SET_SOCKET_OPTIONS;
    }
    this->m_used = true;
    socketDescriptor.fd = this->m_fd;
    return Drv::SOCK_SUCCESS;
}

FwSignedSizeType SeqPacketSocket::sendProtocol(const Drv::SocketDescriptor& socketDescriptor,
                                               const U8* const data,
                                               const FwSizeType size) {
    // a send to a peer that is gone returns an error, never SIGPIPE
    return static_cast<FwSignedSizeType>(
        ::send(socketDescriptor.fd, data, static_cast<size_t>(size), SEND_NO_SIGNAL_FLAGS));
}

FwSignedSizeType SeqPacketSocket::recvProtocol(const Drv::SocketDescriptor& socketDescriptor,
                                               U8* const data,
                                               const FwSizeType size) {
    // one call returns one whole message; 0 when the peer is gone
    return static_cast<FwSignedSizeType>(::recv(socketDescriptor.fd, data, static_cast<size_t>(size), 0));
}

}  // namespace Mru
