// ======================================================================
// \title  SeqPacketDriver.hpp
// \brief  A byte-stream driver over an inherited SOCK_SEQPACKET socket
// ======================================================================

#ifndef Mru_SeqPacketDriver_HPP
#define Mru_SeqPacketDriver_HPP

#include "Drv/Ip/SocketComponentHelper.hpp"
#include "Mru/Components/SeqPacketDriver/SeqPacketDriverComponentAc.hpp"
#include "Mru/Components/SeqPacketDriver/SeqPacketSocket.hpp"

namespace Mru {

class SeqPacketDriver final : public SeqPacketDriverComponentBase, public Drv::SocketComponentHelper {
  public:
    explicit SeqPacketDriver(const char* const compName);
    ~SeqPacketDriver() override;

    SeqPacketDriver(const SeqPacketDriver&) = delete;
    SeqPacketDriver& operator=(const SeqPacketDriver&) = delete;

    //! Use this end of a socket pair. `bufferSize` must hold the largest message.
    Drv::SocketIpStatus configure(int fd, FwSizeType bufferSize = 1024);

  private:
    Drv::IpSocket& getSocketHandler() override;
    Fw::Buffer getBuffer() override;
    void sendBuffer(Fw::Buffer buffer, Drv::SocketIpStatus status) override;
    void connected() override;

    Drv::ByteStreamStatus send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    SeqPacketSocket m_socket;
    FwSizeType m_allocationSize;
};

}  // namespace Mru

#endif
