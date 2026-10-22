// ======================================================================
// \title  SeqPacketDriver.cpp
// \brief  A byte-stream driver over an inherited SOCK_SEQPACKET socket.
//         The shape of F´'s TcpClient; only the socket differs.
// ======================================================================

#include "Mru/Components/SeqPacketDriver/SeqPacketDriver.hpp"

#include "Fw/Types/Assert.hpp"

namespace Mru {

SeqPacketDriver ::SeqPacketDriver(const char* const compName)
    : SeqPacketDriverComponentBase(compName), Drv::SocketComponentHelper(), m_socket(), m_allocationSize(1024) {}

SeqPacketDriver ::~SeqPacketDriver() {}

Drv::SocketIpStatus SeqPacketDriver ::configure(int fd, FwSizeType bufferSize) {
    this->m_allocationSize = bufferSize;
    return this->m_socket.configureDescriptor(fd, SOCKET_SEND_TIMEOUT_SECONDS, SOCKET_SEND_TIMEOUT_MICROSECONDS);
}

Drv::IpSocket& SeqPacketDriver ::getSocketHandler() {
    return this->m_socket;
}

Fw::Buffer SeqPacketDriver ::getBuffer() {
    return this->allocate_out(0, this->m_allocationSize);
}

void SeqPacketDriver ::sendBuffer(Fw::Buffer buffer, Drv::SocketIpStatus status) {
    FW_ASSERT((status != Drv::SOCK_SUCCESS) || (buffer.getData() != nullptr));
    Drv::ByteStreamStatus recvStatus = Drv::ByteStreamStatus::OTHER_ERROR;
    if (status == Drv::SOCK_SUCCESS) {
        recvStatus = Drv::ByteStreamStatus::OP_OK;
    } else if (status == Drv::SOCK_NO_DATA_AVAILABLE) {
        recvStatus = Drv::ByteStreamStatus::RECV_NO_DATA;
    }
    this->recv_out(0, buffer, recvStatus);
}

void SeqPacketDriver ::connected() {
    // a socket pair cannot be reopened: once the peer is gone, a send must
    // fail at once, not wait for a reconnect on the caller's thread (the
    // manager's, for a replica that was killed)
    this->setAutomaticOpen(false);
    // and there is nothing for the reconnect thread to do: let it end,
    // rather than wake every 50 ms for the rest of the run
    this->stopReconnect();
    if (this->isConnected_ready_OutputPort(0)) {
        this->ready_out(0);
    }
}

Drv::ByteStreamStatus SeqPacketDriver ::send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) {
    (void)portNum;
    const Drv::SocketIpStatus status = this->send(fwBuffer.getData(), fwBuffer.getSize());
    if (status == Drv::SOCK_SUCCESS) {
        return Drv::ByteStreamStatus::OP_OK;
    }
    if (status == Drv::SOCK_INTERRUPTED_TRY_AGAIN) {
        return Drv::ByteStreamStatus::SEND_RETRY;
    }
    return Drv::ByteStreamStatus::OTHER_ERROR;
}

void SeqPacketDriver ::recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    (void)portNum;
    this->deallocate_out(0, fwBuffer);
}

}  // namespace Mru
