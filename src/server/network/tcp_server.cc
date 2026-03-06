#include "network/tcp_server.h"

#include <algorithm>
#include <filesystem>

#include "log/logger.h"

namespace mir2::network {

TcpServer::TcpServer(asio::io_context& io_context)
    : io_context_(io_context) {}

bool TcpServer::Start(const std::string& bind_ip, uint16_t port, int max_connections) {
  max_connections_ = max_connections;
  StopAndResetAcceptors();
  CleanupUnixSocketPath();

  asio::error_code ec;
  asio::ip::address bind_address = asio::ip::address_v4::any();
  if (!bind_ip.empty()) {
    bind_address = asio::ip::make_address(bind_ip, ec);
    if (ec) {
      SYSLOG_ERROR("Invalid bind IP '{}': {}", bind_ip, ec.message());
      return false;
    }
  }
  asio::ip::tcp::endpoint endpoint(bind_address, port);
  tcp_acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_);

  tcp_acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    SYSLOG_ERROR("Failed to open TCP acceptor: {}", ec.message());
    return false;
  }
  tcp_acceptor_->set_option(asio::socket_base::reuse_address(true), ec);
  tcp_acceptor_->bind(endpoint, ec);
  if (ec) {
    SYSLOG_ERROR("Failed to bind TCP acceptor {}:{}: {}",
                 endpoint.address().to_string(),
                 endpoint.port(),
                 ec.message());
    return false;
  }
  tcp_acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    SYSLOG_ERROR("Failed to listen on TCP acceptor: {}", ec.message());
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
    SYSLOG_ERROR("Failed to start UDS server: socket_path is empty");
    return false;
  }

  const std::filesystem::path path(socket_path);
  const std::filesystem::path parent = path.parent_path();
  std::error_code fs_ec;
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, fs_ec);
    if (fs_ec) {
      SYSLOG_ERROR("Failed to create UDS parent directory '{}': {}",
                   parent.string(),
                   fs_ec.message());
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
    SYSLOG_ERROR("Failed to open UDS acceptor '{}': {}",
                 socket_path,
                 ec.message());
    return false;
  }
  uds_acceptor_->bind(endpoint, ec);
  if (ec) {
    std::filesystem::remove(path, fs_ec);
    SYSLOG_ERROR("Failed to bind UDS acceptor '{}': {}",
                 socket_path,
                 ec.message());
    return false;
  }
  uds_acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    std::filesystem::remove(path, fs_ec);
    SYSLOG_ERROR("Failed to listen on UDS acceptor '{}': {}",
                 socket_path,
                 ec.message());
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

void TcpServer::SetAcceptedConnectionWriteQueueSize(size_t max_write_queue_size) {
  accepted_connection_write_queue_size_ = std::max<size_t>(max_write_queue_size, 1);
}

void TcpServer::SetAcceptedConnectionWriteBatchOptions(
    TcpConnection::WriteBatchOptions options) {
  options.flush_interval_us = std::max<int64_t>(options.flush_interval_us, 0);
  accepted_connection_write_batch_options_ = options;
}

void TcpServer::SetAcceptedConnectionLowCopySendEnabled(bool enabled) {
  accepted_connection_low_copy_send_enabled_ = enabled;
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
            connection_id,
            accepted_connection_write_queue_size_,
            accepted_connection_write_batch_options_);
        connection->SetLowCopySendEnabled(accepted_connection_low_copy_send_enabled_);
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
                connection_id,
                accepted_connection_write_queue_size_,
                accepted_connection_write_batch_options_);
            connection->SetLowCopySendEnabled(accepted_connection_low_copy_send_enabled_);
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
