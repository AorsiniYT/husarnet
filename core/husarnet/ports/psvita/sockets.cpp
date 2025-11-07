// Copyright (c) 2025 Husarnet sp. z o.o.
// Authors: listed in project_root/README.md
// License: specified in project_root/LICENSE.txt

// PS Vita socket implementation using LwIP
// This is a stub implementation that allows compilation.
// Full implementation would require integrating with LwIP network stack

#ifdef PSVITA_PLATFORM

#include <sys/socket.h>
#include <netinet/in.h>
#include "sockets.h"
#include <lwip/sockets.h>

namespace OsSocket {

// UDP Listen - Unicast
bool udpListenUnicast(int port, PacketCallback callback, bool setAsDefault) {
  // TODO: Implement with LwIP socket API
  return true;
}

// UDP Send
void udpSend(InetAddress address, string_view data, int fd) {
  // TODO: Implement with LwIP socket API
}

// UDP Listen - Multicast
bool udpListenMulticast(InetAddress address, PacketCallback callback) {
  // TODO: Implement with LwIP socket API
  return true;
}

// UDP Send - Multicast
void udpSendMulticast(InetAddress address, const std::string& data) {
  // TODO: Implement with LwIP socket API
}

// Bind UDP Socket
int bindUdpSocket(InetAddress addr, bool reuse) {
  // TODO: Implement with LwIP socket API
  return -1;  // Invalid socket
}

// TCP Connection - Connect
std::shared_ptr<TcpConnection> TcpConnection::connect(
    const InetAddress& address,
    TcpDataCallback dataCallback,
    TcpErrorCallback errorCallback,
    Encapsulation transportType) {
  // TODO: Implement with LwIP socket API
  auto conn = std::make_shared<TcpConnection>(transportType);
  conn->fd = -1;
  return conn;
}

// TCP Connection - Write (std::string)
bool TcpConnection::write(std::shared_ptr<TcpConnection> conn, std::string& data) {
  // TODO: Implement with LwIP socket API
  return false;
}

// TCP Connection - Write (etl::ivector)
bool TcpConnection::write(std::shared_ptr<TcpConnection> conn, etl::ivector<char>& data) {
  // TODO: Implement with LwIP socket API
  return false;
}

// TCP Connection - Close
void TcpConnection::close(std::shared_ptr<TcpConnection> conn) {
  // TODO: Implement with LwIP socket API
}

// Connect Unmanaged TCP Socket
int connectUnmanagedTcpSocket(InetAddress addr) {
  // TODO: Implement with LwIP socket API
  return -1;
}

// Bind Custom FD
void bindCustomFd(int fd, std::function<void()> readyCallback) {
  // TODO: Implement if needed
}

// IP from Sockaddr (PS Vita version)
InetAddress ipFromSockaddr(const void* sockaddr_ptr) {
  // TODO: Convert sockaddr_storage to InetAddress
  // PS Vita implementation stub
  (void)sockaddr_ptr;
  return InetAddress();
}

// Run Once (event loop)
void runOnce(int timeout) {
  // PS Vita event loop integration would go here
  // For now, do nothing
}

}  // namespace OsSocket

#endif  // PSVITA_PLATFORM
