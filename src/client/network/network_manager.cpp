#include "client/network/network_manager.h"

#include <utility>

namespace mir2::client {

NetworkManager::NetworkManager(std::unique_ptr<INetworkClient> client)
    : client_(std::move(client)) {
    wire_callbacks();
}

bool NetworkManager::connect(const std::string& host, uint16_t port) {
    if (!client_) {
        return false;
    }

    disconnect_notified_.store(false);
    return client_->connect(host, port);
}

void NetworkManager::disconnect() {
    if (!client_) {
        return;
    }

    const bool was_connected = client_->is_connected();
    client_->disconnect();

    if (was_connected) {
        handle_disconnect();
    }
}

bool NetworkManager::is_connected() const {
    return client_ && client_->is_connected();
}

void NetworkManager::send_message(mir2::common::MsgId msg_id,
                                  const std::vector<uint8_t>& payload) {
    if (!client_) {
        return;
    }
    client_->send(static_cast<uint16_t>(msg_id), payload);
}

void NetworkManager::register_handler(mir2::common::MsgId msg_id, HandlerFunc handler) {
    dispatcher_.RegisterHandler(msg_id, std::move(handler));
}

void NetworkManager::set_default_handler(HandlerFunc handler) {
    dispatcher_.SetDefaultHandler(std::move(handler));
}

void NetworkManager::set_on_connect(EventCallback callback) {
    on_connect_ = std::move(callback);
}

void NetworkManager::set_on_disconnect(EventCallback callback) {
    on_disconnect_ = std::move(callback);
}

ConnectionState NetworkManager::get_state() const {
    if (!client_) {
        return ConnectionState::DISCONNECTED;
    }
    return client_->get_state();
}

ErrorCode NetworkManager::get_last_error() const {
    if (!client_) {
        return ErrorCode::CONNECTION_FAILED;
    }
    return client_->get_last_error();
}

void NetworkManager::update() {
    if (client_) {
        client_->update();
    }
    if (connect_pending_.exchange(false)) {
        handle_connect();
    }
    if (disconnect_pending_.exchange(false)) {
        handle_disconnect();
    }
}

void NetworkManager::wire_callbacks() {
    if (!client_) {
        return;
    }

    client_->set_on_message([this](const NetworkPacket& packet) {
        dispatcher_.Dispatch(packet);
    });

    client_->set_on_connect([this]() {
        connect_pending_.store(true);
    });

    client_->set_on_disconnect([this]() {
        disconnect_pending_.store(true);
    });
}

void NetworkManager::handle_connect() {
    disconnect_notified_.store(false);
    if (on_connect_) {
        on_connect_();
    }
}

void NetworkManager::handle_disconnect() {
    bool expected = false;
    if (!disconnect_notified_.compare_exchange_strong(expected, true)) {
        return;
    }

    if (on_disconnect_) {
        on_disconnect_();
    }
}

} // namespace mir2::client
