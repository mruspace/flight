// ======================================================================
// \title  SeqPacketDriver.hpp
// \brief  A byte-stream driver over an inherited SOCK_SEQPACKET socket
// ======================================================================

#ifndef Mru_SeqPacketDriver_HPP
#define Mru_SeqPacketDriver_HPP

#include <atomic>

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

    //! A check on every message received, before it goes on: true lets it
    //! through, false drops it here. The manager process checks what its
    //! replicas send, so a replica whose memory is corrupted cannot pass a
    //! malformed message to F´'s generated code, which asserts on one, or
    //! flood the manager's queue. Called on the link's own thread.
    using Filter = bool (*)(void* context, const U8* data, FwSizeType size);

    //! Check every received message with `filter` (nullptr: none, the default)
    void setFilter(Filter filter, void* context);

    //! Messages dropped by the filter so far
    U32 dropped() const;

  private:
    Drv::IpSocket& getSocketHandler() override;
    Fw::Buffer getBuffer() override;
    void sendBuffer(Fw::Buffer buffer, Drv::SocketIpStatus status) override;
    void connected() override;

    Drv::ByteStreamStatus send_handler(const FwIndexType portNum, Fw::Buffer& fwBuffer) override;
    void recvReturnIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) override;

    SeqPacketSocket m_socket;
    FwSizeType m_allocationSize;
    Filter m_filter;
    void* m_filterContext;
    std::atomic<U32> m_dropped;
};

}  // namespace Mru

#endif
