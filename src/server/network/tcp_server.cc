#include "network/tcp_server.h"

#include <iostream>

namespace mir2::network {

TcpServer::TcpServer(asio::io_context& io_context)
    : io_context_(io_context) {}

bool TcpServer::Start(const std::string& bind_ip, uint16_t port, int max_connections) {
  max_connections_ = max_connections;
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(bind_ip), port);
  acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(io_context_);

  asio::error_code ec;
  acceptor_->open(endpoint.protocol(), ec);
  if (ec) {
    std::cerr << "Failed to open acceptor: " << ec.message() << std::endl;
    return false;
  }
  acceptor_->set_option(asio::socket_base::reuse_address(true), ec);
  acceptor_->bind(endpoint, ec);
  if (ec) {
    std::cerr << "Failed to bind acceptor: " << ec.message() << std::endl;
    return false;
  }
  acceptor_->listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    std::cerr << "Failed to listen: " << ec.message() << std::endl;
    return false;
  }

  DoAccept();
  return true;
}

void TcpServer::Stop() {
  if (acceptor_) {
    asio::error_code ec;
    acceptor_->close(ec);
  }
}

void TcpServer::DoAccept() {
  if (!acceptor_) {
    return;
  }

  acceptor_->async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
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
}

}  // namespace mir2::network
