// ======================================================================
// \title  SeqPacketSocket.hpp
// \brief  F´'s socket helper over an inherited SOCK_SEQPACKET descriptor
// ======================================================================

#ifndef Mru_SeqPacketSocket_HPP
#define Mru_SeqPacketSocket_HPP

#include "Drv/Ip/IpSocket.hpp"

namespace Mru {

//! One end of a socket pair made before the process started. F´'s socket
//! helper calls open once; there is no address, so it cannot reconnect: when
//! the peer is gone, the link is gone.
class SeqPacketSocket final : public Drv::IpSocket {
  public:
    SeqPacketSocket();

    //! Use this descriptor, with these send timeouts
    Drv::SocketIpStatus configureDescriptor(int fd, U32 sendTimeoutSeconds, U32 sendTimeoutMicroseconds);

  protected:
    Drv::SocketIpStatus openProtocol(Drv::SocketDescriptor& socketDescriptor) override;
    FwSignedSizeType sendProtocol(const Drv::SocketDescriptor& socketDescriptor,
                                  const U8* const data,
                                  const FwSizeType size) override;
    FwSignedSizeType recvProtocol(const Drv::SocketDescriptor& socketDescriptor,
                                  U8* const data,
                                  const FwSizeType size) override;

  private:
    int m_fd;
    bool m_used;
};

}  // namespace Mru

#endif
