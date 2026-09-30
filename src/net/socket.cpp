// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace cb::net {
namespace {

void setNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::uint32_t readLengthLE(std::string_view bytes) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 8) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3])) << 24);
}

}  // namespace

SocketTransport::~SocketTransport() {
  if (fd_ >= 0) (void)::close(fd_);
}

void SocketTransport::send(std::string_view frame) {
  std::size_t sent = 0;
  while (sent < frame.size()) {
    const ssize_t n = ::send(fd_, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
    if (n > 0) {
      sent += static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // Wait until the socket can take more, then carry on.
      pollfd p{fd_, POLLOUT, 0};
      (void)::poll(&p, 1, 100);
      continue;
    }
    return;  // the peer is gone; the caller notices via receive()
  }
}

std::optional<std::string> SocketTransport::receive() {
  char chunk[4096];
  for (;;) {
    const ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
    if (n > 0) {
      buffer_.append(chunk, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      // The peer closed the connection.
      return std::nullopt;
    }
    // EAGAIN (and anything else) means nothing more is waiting right now.
    break;
  }

  if (buffer_.size() < 4) return std::nullopt;
  const std::uint32_t length = readLengthLE(buffer_);
  if (buffer_.size() < 4 + length) return std::nullopt;  // a partial frame; wait for more
  std::string frame = buffer_.substr(0, 4 + length);
  buffer_.erase(0, 4 + length);
  return frame;
}

Result<std::unique_ptr<SocketListener>> SocketListener::bind(std::uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return fail(ErrorCode::Internal, std::string("socket: ") + std::strerror(errno));
  }
  int yes = 1;
  (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    const std::string error = std::strerror(errno);
    (void)::close(fd);
    return fail(ErrorCode::Internal, "bind: " + error);
  }
  if (::listen(fd, 8) < 0) {
    const std::string error = std::strerror(errno);
    (void)::close(fd);
    return fail(ErrorCode::Internal, "listen: " + error);
  }

  sockaddr_in bound{};
  socklen_t len = sizeof(bound);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &len) < 0) {
    (void)::close(fd);
    return fail(ErrorCode::Internal, "getsockname failed");
  }
  return std::unique_ptr<SocketListener>(new SocketListener(fd, ntohs(bound.sin_port)));
}

SocketListener::~SocketListener() {
  if (fd_ >= 0) (void)::close(fd_);
}

Result<std::unique_ptr<SocketTransport>> SocketListener::accept(int timeoutMs) const {
  pollfd p{fd_, POLLIN, 0};
  const int ready = ::poll(&p, 1, timeoutMs);
  if (ready <= 0) return fail(ErrorCode::Internal, "accept timed out");
  const int client = ::accept(fd_, nullptr, nullptr);
  if (client < 0) {
    return fail(ErrorCode::Internal, std::string("accept: ") + std::strerror(errno));
  }
  setNonBlocking(client);
  return std::unique_ptr<SocketTransport>(new SocketTransport(client));
}

Result<std::unique_ptr<SocketTransport>> connectTo(const std::string& host,
                                                   std::uint16_t port, int timeoutMs) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return fail(ErrorCode::Internal, std::string("socket: ") + std::strerror(errno));
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
    (void)::close(fd);
    return fail(ErrorCode::Internal, "connect: bad address '" + host + "'");
  }
  setNonBlocking(fd);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 &&
      errno != EINPROGRESS) {
    const std::string error = std::strerror(errno);
    (void)::close(fd);
    return fail(ErrorCode::Internal, "connect: " + error);
  }
  pollfd p{fd, POLLOUT, 0};
  const int ready = ::poll(&p, 1, timeoutMs);
  if (ready <= 0) {
    (void)::close(fd);
    return fail(ErrorCode::Internal, "connect timed out");
  }
  int soError = 0;
  socklen_t len = sizeof(soError);
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) < 0 || soError != 0) {
    (void)::close(fd);
    return fail(ErrorCode::Internal, "connect: " + std::to_string(soError));
  }
  return std::unique_ptr<SocketTransport>(new SocketTransport(fd));
}

}  // namespace cb::net
