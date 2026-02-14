#include "network/tcp_server.h"

#include <filesystem>
#include <iostream>

namespace mir2::network {

TcpServer::TcpServer(asio::io_context& io_context)
    : io_context_(io_context) {}

bool TcpServer::Start(const std::string& bind_ip, uint16_t port, int max_connections) {
  max_connections_ = max_connections;
  StopAndResetAcceptors();
  CleanupUnixSocketPath();

  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(bind_ip), port);
  tcp_acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_);

  asio::error_code ec;
  tcp_acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    std::cerr << "Failed to open acceptor: " << ec.message() << std::endl;
    return false;
  }
  tcp_acceptor_->set_option(asio::socket_base::reuse_address(true), ec);
  tcp_acceptor_->bind(endpoint, ec);
  if (ec) {
    std::cerr << "Failed to bind acceptor: " << ec.message() << std::endl;
    return false;
  }
  tcp_acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    std::cerr << "Failed to listen: " << ec.message() << std::endl;
    return false;
  }

  DoAccept();
  return true;
}

#if defined(ASIO_HAS_LOCAL_SOCKETS)
bool TcpServer::StartUnix(const std::string& socket_path, int max_connections) {
  max_connections_ = max_connections;
  StopAndResetAcceptors();
  CleanupUnixSocketPath();

  if (socket_path.empty()) {
    std::cerr << "Failed to start uds server: socket_path is empty" << std::endl;
    return false;
  }

  const std::filesystem::path path(socket_path);
  const std::filesystem::path parent = path.parent_path();
  std::error_code fs_ec;
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, fs_ec);
    if (fs_ec) {
      std::cerr << "Failed to create uds parent dir: " << fs_ec.message() << std::endl;
      return false;
    }
  }

  std::filesystem::remove(path, fs_ec);
  fs_ec.clear();

  uds_acceptor_ = std::make_unique<asio::local::stream_protocol::acceptor>(io_context_);
  const asio::local::stream_protocol::endpoint endpoint(socket_path);

  asio::error_code ec;
  uds_acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    std::cerr << "Failed to open uds acceptor: " << ec.message() << std::endl;
    return false;
  }
  uds_acceptor_->bind(endpoint, ec);
  if (ec) {
    std::filesystem::remove(path, fs_ec);
    std::cerr << "Failed to bind uds acceptor: " << ec.message() << std::endl;
    return false;
  }
  uds_acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    std::filesystem::remove(path, fs_ec);
    std::cerr << "Failed to listen uds acceptor: " << ec.message() << std::endl;
    return false;
  }

  uds_socket_path_ = socket_path;
  DoAccept();
  return true;
}
#else
bool TcpServer::StartUnix(const std::string& socket_path, int max_connections) {
  (void)socket_path;
  (void)max_connections;
  return false;
}
#endif

void TcpServer::Stop() {
  StopAndResetAcceptors();
  CleanupUnixSocketPath();
}

void TcpServer::StopAndResetAcceptors() {
  if (tcp_acceptor_) {
    asio::error_code ec;
    tcp_acceptor_->close(ec);
    tcp_acceptor_.reset();
  }
#if defined(ASIO_HAS_LOCAL_SOCKETS)
  if (uds_acceptor_) {
    asio::error_code ec;
    uds_acceptor_->close(ec);
    uds_acceptor_.reset();
  }
#endif
}

void TcpServer::CleanupUnixSocketPath() {
  if (uds_socket_path_.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(uds_socket_path_, ec);
  uds_socket_path_.clear();
}

void TcpServer::DoAccept() {
  if (tcp_acceptor_) {
    tcp_acceptor_->async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
      if (!ec) {
        uint64_t connection_id = next_connection_id_.fetch_add(1);
        auto connection = std::make_shared<TcpConnection>(
            std::make_unique<AsioSocketAdapter>(std::move(socket)),
            connection_id);
        if (connect_handler_) {
          connect_handler_(connection);
        } else {
          connection->Close();
        }
      }
      DoAccept();
    });
    return;
  }

#if defined(ASIO_HAS_LOCAL_SOCKETS)
  if (uds_acceptor_) {
    uds_acceptor_->async_accept(
        [this](const asio::error_code& ec, asio::local::stream_protocol::socket socket) {
          if (!ec) {
            uint64_t connection_id = next_connection_id_.fetch_add(1);
            auto connection = std::make_shared<TcpConnection>(
                std::make_unique<UdsSocketAdapter>(std::move(socket)),
                connection_id);
            if (connect_handler_) {
              connect_handler_(connection);
            } else {
              connection->Close();
            }
          }
          DoAccept();
        });
  }
#endif
}

}  // namespace mir2::network
