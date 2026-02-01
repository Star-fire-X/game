/**
 * @file network_client.cc
 * @brief Legend2 网络客户端实现
 *
 * 本文件实现基于Asio的TCP客户端功能，包括：
 * - 异步连接和断开连接
 * - 消息发送和接收队列
 * - 心跳机制和超时检测
 * - 自动重连功能
 * - RTT（往返时间）计算
 */

#include "network/network_client.h"

#include <chrono>

#include "common/enums.h"
#include "common/types/constants.h"
#include "flatbuffers/flatbuffers.h"
#include "system_generated.h"

namespace mir2::client {

// =============================================================================
// NetworkClient 类实现
// =============================================================================

namespace {
uint32_t get_timestamp_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

using PacketHeader = mir2::common::PacketHeader;
using PacketHeaderV2 = mir2::common::PacketHeaderV2;
using ProtocolVersion = mir2::common::ProtocolVersion;
namespace constants = mir2::common::constants;
constexpr uint16_t kSequenceWindow = 256;
} // namespace

/**
 * @brief 构造函数 - 初始化读取缓冲区
 */
NetworkClient::NetworkClient()
    : read_buffer_(READ_BUFFER_SIZE) {
}

/**
 * @brief 析构函数 - 确保断开连接
 */
NetworkClient::~NetworkClient() {
    disconnect();
}

/**
 * @brief 连接到服务器
 * @param host 服务器地址
 * @param port 服务器端口
 * @return true 如果连接请求已发起
 *
 * 异步连接，实际连接结果通过回调通知。
 */
bool NetworkClient::connect(const std::string& host, uint16_t port) {
    if (state_ != ConnectionState::DISCONNECTED) {
        disconnect();
    }

    host_ = host;
    port_ = port;
    state_ = ConnectionState::CONNECTING;
    last_error_ = ErrorCode::SUCCESS;

    try {
        // Create new socket
        socket_ = std::make_unique<asio::ip::tcp::socket>(io_context_);

        // Resolve hostname
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, std::to_string(port));

        // Start async connect
        do_connect(endpoints);

        // Start IO thread
        start_io_thread();

        return true;
    } catch (const std::exception& /*e*/) {
        state_ = ConnectionState::DISCONNECTED;
        last_error_ = ErrorCode::CONNECTION_FAILED;
        return false;
    }
}

/**
 * @brief 断开连接
 *
 * 停止所有定时器，关闭套接字，清空消息队列。
 */
void NetworkClient::disconnect() {
    auto expected = ConnectionState::CONNECTED;
    if (!state_.compare_exchange_strong(expected, ConnectionState::DISCONNECTED)) {
        expected = ConnectionState::CONNECTING;
        if (!state_.compare_exchange_strong(expected, ConnectionState::DISCONNECTED)) {
            expected = ConnectionState::RECONNECTING;
            state_.compare_exchange_strong(expected, ConnectionState::DISCONNECTED);
        }
    }

    // Stop timers
    if (heartbeat_timer_) {
        asio::error_code ec;
        heartbeat_timer_->cancel(ec);
    }
    if (timeout_timer_) {
        asio::error_code ec;
        timeout_timer_->cancel(ec);
    }

    // Close socket
    if (socket_ && socket_->is_open()) {
        asio::error_code ec;
        socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_->close(ec);
    }

    // Stop IO thread
    stop_io_thread();
    write_in_progress_ = false;

    // Clear queues
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        std::queue<std::vector<uint8_t>> empty;
        std::swap(send_queue_, empty);
    }
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        std::queue<NetworkPacket> empty;
        std::swap(receive_queue_, empty);
    }
}

/**
 * @brief 检查是否已连接
 * @return true 如果连接状态为 CONNECTED
 */
bool NetworkClient::is_connected() const {
    return state_ == ConnectionState::CONNECTED;
}

/**
 * @brief 发送消息
 * @param msg 要发送的消息
 *
 * 消息加入发送队列，由 IO 线程异步发送。
 */
void NetworkClient::send(uint16_t msg_id, const std::vector<uint8_t>& payload) {
    if (state_ != ConnectionState::CONNECTED) {
        return;
    }

    if (payload.size() > MAX_PACKET_SIZE) {
        last_error_ = ErrorCode::INVALID_PACKET;
        handle_disconnect(asio::error::message_size);
        return;
    }

    const uint16_t seq = send_sequence_.fetch_add(1, std::memory_order_relaxed);
    auto encoded = mir2::common::EncodePacketV2(msg_id, payload.data(), payload.size(), seq, 0);
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_queue_.push(std::move(encoded));
    }

    // Post write operation to IO thread (serialize writes to avoid interleaving frames).
    asio::post(io_context_, [this]() {
        if (state_ != ConnectionState::CONNECTED || !socket_) {
            write_in_progress_ = false;
            return;
        }

        if (write_in_progress_) {
            return;
        }

        write_in_progress_ = true;
        do_write();
    });
}

/**
 * @brief 接收消息
 * @return std::optional<NetworkMessage> 有消息返回消息，否则返回 nullopt
 *
 * 从接收队列取出一条消息。
 */
std::optional<NetworkPacket> NetworkClient::receive() {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    if (receive_queue_.empty()) {
        return std::nullopt;
    }

    NetworkPacket packet = std::move(receive_queue_.front());
    receive_queue_.pop();
    return packet;
}

// -----------------------------------------------------------------------------
// 回调设置方法
// -----------------------------------------------------------------------------

/**
 * @brief 设置消息接收回调
 * @param callback 回调函数
 */
void NetworkClient::set_on_message(std::function<void(const NetworkPacket&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_message_ = std::move(callback);
}

/**
 * @brief 设置断开连接回调
 * @param callback 回调函数
 */
void NetworkClient::set_on_disconnect(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_disconnect_ = std::move(callback);
}

/**
 * @brief 设置连接成功回调
 * @param callback 回调函数
 */
void NetworkClient::set_on_connect(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_connect_ = std::move(callback);
}

// -----------------------------------------------------------------------------
// 状态查询方法
// -----------------------------------------------------------------------------

/**
 * @brief 获取连接状态
 * @return ConnectionState 当前连接状态
 */
ConnectionState NetworkClient::get_state() const {
    return state_;
}

/**
 * @brief 获取最后一次错误码
 * @return ErrorCode 错误码
 */
ErrorCode NetworkClient::get_last_error() const {
    return last_error_;
}

/**
 * @brief 更新客户端（处理接收队列中的消息）
 *
 * 在主线程调用，处理所有待处理的消息并触发回调。
 */
void NetworkClient::update() {
    // Process received messages with callbacks
    while (true) {
        std::optional<NetworkPacket> packet;
        {
            std::lock_guard<std::mutex> lock(receive_mutex_);
            if (receive_queue_.empty()) {
                break;
            }
            packet = std::move(receive_queue_.front());
            receive_queue_.pop();
        }

        if (packet) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (on_message_) {
                on_message_(*packet);
            }
        }
    }
}

/**
 * @brief 获取往返时间
 * @return RTT 毫秒数
 */
uint32_t NetworkClient::get_rtt() const {
    return rtt_;
}

/**
 * @brief 设置自动重连
 * @param enable 是否启用
 */
void NetworkClient::set_auto_reconnect(bool enable) {
    auto_reconnect_ = enable;
}

// -----------------------------------------------------------------------------
// IO 线程管理
// -----------------------------------------------------------------------------

/**
 * @brief 启动 IO 线程
 */
void NetworkClient::start_io_thread() {
    // Reset io_context if it was stopped
    if (io_context_.stopped()) {
        io_context_.restart();
    }

    io_thread_ = std::thread([this]() {
        try {
            // Keep io_context running
            auto work = asio::make_work_guard(io_context_);
            io_context_.run();
        } catch (const std::exception& /*e*/) {
            // Log error
        }
    });
}

/**
 * @brief 停止 IO 线程
 */
void NetworkClient::stop_io_thread() {
    io_context_.stop();

    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

// -----------------------------------------------------------------------------
// 异步操作实现
// -----------------------------------------------------------------------------

/**
 * @brief 执行异步连接
 * @param endpoints 解析后的端点列表
 */
void NetworkClient::do_connect(const asio::ip::tcp::resolver::results_type& endpoints) {
    asio::async_connect(*socket_, endpoints,
        [this](const asio::error_code& ec, const asio::ip::tcp::endpoint&) {
            if (!ec) {
                state_ = ConnectionState::CONNECTED;
                last_error_ = ErrorCode::SUCCESS;
                last_heartbeat_time_ = get_timestamp_ms();
                protocol_version_ = ProtocolVersion::kV2;
                send_sequence_.store(0, std::memory_order_relaxed);
                recv_sequence_.store(0, std::memory_order_relaxed);

                // Disable Nagle's algorithm for lower latency
                socket_->set_option(asio::ip::tcp::no_delay(true));

                // Start reading
                do_read_header();

                // Start heartbeat
                start_heartbeat();

                // Notify connection
                {
                    std::lock_guard<std::mutex> lock(callback_mutex_);
                    if (on_connect_) {
                        on_connect_();
                    }
                }
            } else {
                state_ = ConnectionState::DISCONNECTED;
                last_error_ = ErrorCode::CONNECTION_FAILED;

                if (auto_reconnect_) {
                    try_reconnect();
                }
            }
        });
}

/**
 * @brief 异步读取消息头
 *
 * 先读取 magic，再读取剩余包头并进入消息体读取。
 */
void NetworkClient::do_read_header() {
    if (state_ != ConnectionState::CONNECTED || !socket_) {
        return;
    }

    asio::async_read(*socket_, asio::buffer(header_buffer_.data(), sizeof(uint32_t)),
        [this](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (ec || bytes_transferred != sizeof(uint32_t)) {
                handle_disconnect(ec);
                return;
            }

            protocol_version_ = mir2::common::DetectProtocolVersion(header_buffer_.data());
            const size_t header_size = protocol_version_ == ProtocolVersion::kV2
                                           ? PacketHeaderV2::kSize
                                           : PacketHeader::kSize;
            const size_t remaining_size = header_size - sizeof(uint32_t);
            do_read_remaining_header(remaining_size);
        });
}

void NetworkClient::do_read_remaining_header(size_t remaining_size) {
    if (state_ != ConnectionState::CONNECTED || !socket_) {
        return;
    }

    asio::async_read(*socket_,
        asio::buffer(header_buffer_.data() + sizeof(uint32_t), remaining_size),
        [this, remaining_size](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (ec || bytes_transferred != remaining_size) {
                handle_disconnect(ec);
                return;
            }

            if (protocol_version_ == ProtocolVersion::kV2) {
                PacketHeaderV2 header{};
                if (!PacketHeaderV2::FromBytes(header_buffer_.data(),
                                               PacketHeaderV2::kSize,
                                               &header) ||
                    header.version != PacketHeaderV2::kVersion) {
                    last_error_ = ErrorCode::INVALID_PACKET;
                    handle_disconnect(asio::error::invalid_argument);
                    return;
                }
                current_msg_id_ = header.msg_id;
                current_payload_size_ = header.payload_size;
            } else {
                PacketHeader header{};
                if (!PacketHeader::FromBytes(header_buffer_.data(), PacketHeader::kSize, &header)) {
                    last_error_ = ErrorCode::INVALID_PACKET;
                    handle_disconnect(asio::error::invalid_argument);
                    return;
                }
                current_msg_id_ = header.msg_id;
                current_payload_size_ = header.payload_size;
            }

            if (current_payload_size_ > MAX_PACKET_SIZE) {
                last_error_ = ErrorCode::INVALID_PACKET;
                handle_disconnect(asio::error::message_size);
                return;
            }

            if (current_payload_size_ > 0) {
                if (read_buffer_.size() < current_payload_size_) {
                    read_buffer_.resize(current_payload_size_);
                }
                do_read_body(current_payload_size_);
                return;
            }

            if (protocol_version_ == ProtocolVersion::kV2) {
                NetworkPacket packet{};
                uint16_t sequence = 0;
                const auto status = mir2::common::DecodePacketV2(header_buffer_.data(),
                                                                 PacketHeaderV2::kSize,
                                                                 &packet,
                                                                 &sequence);
                if (status != mir2::common::DecodeStatus::kOk) {
                    last_error_ = ErrorCode::INVALID_PACKET;
                    handle_disconnect(asio::error::invalid_argument);
                    return;
                }
                if (!check_recv_sequence(sequence)) {
                    last_error_ = ErrorCode::INVALID_PACKET;
                    handle_disconnect(asio::error::invalid_argument);
                    return;
                }
                handle_packet(packet);
                do_read_header();
                return;
            }

            NetworkPacket packet{current_msg_id_, {}};
            handle_packet(packet);
            do_read_header();
        });
}

/**
 * @brief 异步读取消息体
 * @param body_length 消息体长度
 */
void NetworkClient::do_read_body(size_t body_length) {
    if (state_ != ConnectionState::CONNECTED || !socket_) {
        return;
    }

    asio::async_read(*socket_, asio::buffer(read_buffer_.data(), body_length),
        [this, body_length](const asio::error_code& ec, std::size_t bytes_transferred) {
            if (!ec && bytes_transferred == body_length) {
                if (protocol_version_ == ProtocolVersion::kV2) {
                    std::vector<uint8_t> packet_buffer;
                    packet_buffer.reserve(PacketHeaderV2::kSize + body_length);
                    packet_buffer.insert(packet_buffer.end(),
                                         header_buffer_.begin(),
                                         header_buffer_.begin() + PacketHeaderV2::kSize);
                    packet_buffer.insert(packet_buffer.end(),
                                         read_buffer_.begin(),
                                         read_buffer_.begin() + body_length);

                    NetworkPacket packet{};
                    uint16_t sequence = 0;
                    const auto status = mir2::common::DecodePacketV2(packet_buffer.data(),
                                                                     packet_buffer.size(),
                                                                     &packet,
                                                                     &sequence);
                    if (status != mir2::common::DecodeStatus::kOk) {
                        last_error_ = ErrorCode::INVALID_PACKET;
                        handle_disconnect(asio::error::invalid_argument);
                        return;
                    }
                    if (!check_recv_sequence(sequence)) {
                        last_error_ = ErrorCode::INVALID_PACKET;
                        handle_disconnect(asio::error::invalid_argument);
                        return;
                    }
                    handle_packet(packet);
                } else {
                    std::vector<uint8_t> payload(read_buffer_.begin(),
                                                 read_buffer_.begin() + body_length);
                    NetworkPacket packet{current_msg_id_, std::move(payload)};
                    handle_packet(packet);
                }

                // Continue reading
                do_read_header();
            } else {
                handle_disconnect(ec);
            }
        });
}

bool NetworkClient::check_recv_sequence(uint16_t seq) {
    const uint16_t last = recv_sequence_.load(std::memory_order_relaxed);
    const uint16_t forward = static_cast<uint16_t>(seq - last);
    if (forward == 0) {
        return true;
    }
    if (forward < kSequenceWindow) {
        recv_sequence_.store(seq, std::memory_order_relaxed);
        return true;
    }

    const uint16_t backward = static_cast<uint16_t>(last - seq);
    if (backward < kSequenceWindow) {
        return true;
    }

    return false;
}

/**
 * @brief 异步写入消息
 *
 * 从发送队列取出消息并发送。
 */
void NetworkClient::do_write() {
    if (state_ != ConnectionState::CONNECTED || !socket_) {
        write_in_progress_ = false;
        return;
    }

    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (send_queue_.empty()) {
            write_in_progress_ = false;
            return;
        }
        data = std::move(send_queue_.front());
        send_queue_.pop();
    }

    auto buffer = std::make_shared<std::vector<uint8_t>>(std::move(data));
    asio::async_write(*socket_, asio::buffer(*buffer),
        [this, buffer](const asio::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (!ec) {
                // Continue draining the queue (only one async_write in-flight at a time).
                do_write();
            } else {
                write_in_progress_ = false;
                handle_disconnect(ec);
            }
        });
}

// -----------------------------------------------------------------------------
// 心跳和超时检测
// -----------------------------------------------------------------------------

/**
 * @brief 启动心跳机制
 */
void NetworkClient::start_heartbeat() {
    heartbeat_timer_ = std::make_unique<asio::steady_timer>(io_context_);
    timeout_timer_ = std::make_unique<asio::steady_timer>(io_context_);

    last_heartbeat_time_ = get_timestamp_ms();
    send_heartbeat();
    check_timeout();
}

/**
 * @brief 发送心跳包
 *
 * 定期发送心跳消息，用于保持连接和计算 RTT。
 */
void NetworkClient::send_heartbeat() {
    if (state_ != ConnectionState::CONNECTED) {
        return;
    }

    const uint32_t now = get_timestamp_ms();
    last_heartbeat_send_time_ = now;
    const uint32_t seq = ++heartbeat_sequence_;

    flatbuffers::FlatBufferBuilder builder;
    const auto heartbeat = mir2::proto::CreateHeartbeat(builder, seq, now);
    builder.Finish(heartbeat);
    const uint8_t* data = builder.GetBufferPointer();
    std::vector<uint8_t> payload(data, data + builder.GetSize());

    send(static_cast<uint16_t>(mir2::common::MsgId::kHeartbeat), payload);

    // Schedule next heartbeat
    heartbeat_timer_->expires_after(
        std::chrono::milliseconds(constants::HEARTBEAT_INTERVAL_MS));
    heartbeat_timer_->async_wait([this](const asio::error_code& ec) {
        if (!ec) {
            send_heartbeat();
        }
    });
}

/**
 * @brief 检查连接超时
 *
 * 如果长时间未收到心跳响应，断开连接。
 */
void NetworkClient::check_timeout() {
    if (state_ != ConnectionState::CONNECTED) {
        return;
    }

    timeout_timer_->expires_after(
        std::chrono::milliseconds(constants::CONNECTION_TIMEOUT_MS));
    timeout_timer_->async_wait([this](const asio::error_code& ec) {
        if (!ec && state_ == ConnectionState::CONNECTED) {
            uint32_t now = get_timestamp_ms();
            uint32_t last = last_heartbeat_time_;

            // Check if we've received a heartbeat response recently
            if (now - last > constants::CONNECTION_TIMEOUT_MS) {
                // Timeout - disconnect
                last_error_ = ErrorCode::CONNECTION_TIMEOUT;
                handle_disconnect(asio::error::timed_out);
            } else {
                // Continue checking
                check_timeout();
            }
        }
    });
}

// -----------------------------------------------------------------------------
// 消息和断开处理
// -----------------------------------------------------------------------------

/**
 * @brief 处理接收到的网络包
 * @param packet 接收到的网络包
 *
 * 心跳响应用于更新 RTT，其他消息加入接收队列。
 */
void NetworkClient::handle_packet(const NetworkPacket& packet) {
    if (packet.msg_id == static_cast<uint16_t>(mir2::common::MsgId::kHeartbeatRsp)) {
        if (!packet.payload.empty()) {
            flatbuffers::Verifier verifier(packet.payload.data(), packet.payload.size());
            if (verifier.VerifyBuffer<mir2::proto::HeartbeatRsp>(nullptr)) {
                const auto* rsp = flatbuffers::GetRoot<mir2::proto::HeartbeatRsp>(packet.payload.data());
                if (rsp) {
                    const uint32_t now = get_timestamp_ms();
                    rtt_ = now - last_heartbeat_send_time_;
                    last_heartbeat_time_ = now;
                    return;
                }
            }
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        receive_queue_.push(packet);
    }
}

/**
 * @brief 处理断开连接
 * @param ec 错误码
 *
 * 关闭套接字，通知回调，尝试重连。
 */
void NetworkClient::handle_disconnect(const asio::error_code& ec) {
    if (state_ == ConnectionState::DISCONNECTED) {
        return;
    }

    state_ = ConnectionState::DISCONNECTED;
    write_in_progress_ = false;

    // Drop queued messages to avoid sending stale frames after reconnect.
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        std::queue<std::vector<uint8_t>> empty;
        std::swap(send_queue_, empty);
    }
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        std::queue<NetworkPacket> empty;
        std::swap(receive_queue_, empty);
    }

    if (last_error_ != ErrorCode::INVALID_PACKET) {
        if (ec == asio::error::eof || ec == asio::error::connection_reset) {
            last_error_ = ErrorCode::CONNECTION_LOST;
        } else if (ec == asio::error::timed_out) {
            last_error_ = ErrorCode::CONNECTION_TIMEOUT;
        } else {
            last_error_ = ErrorCode::CONNECTION_FAILED;
        }
    }

    // Close socket
    if (socket_ && socket_->is_open()) {
        asio::error_code close_ec;
        socket_->close(close_ec);
    }

    // Notify disconnect
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_disconnect_) {
            on_disconnect_();
        }
    }

    // Try reconnect if enabled
    if (auto_reconnect_) {
        try_reconnect();
    }
}

/**
 * @brief 尝试重新连接
 *
 * 等待一段时间后尝试重新连接到服务器。
 */
void NetworkClient::try_reconnect() {
    if (state_ != ConnectionState::DISCONNECTED || host_.empty()) {
        return;
    }

    state_ = ConnectionState::RECONNECTING;

    // Wait before reconnecting
    auto reconnect_timer = std::make_shared<asio::steady_timer>(io_context_);
    reconnect_timer->expires_after(std::chrono::seconds(2));
    reconnect_timer->async_wait([this, reconnect_timer](const asio::error_code& ec) {
        if (!ec && state_ == ConnectionState::RECONNECTING) {
            // Try to reconnect
            try {
                socket_ = std::make_unique<asio::ip::tcp::socket>(io_context_);
                asio::ip::tcp::resolver resolver(io_context_);
                auto endpoints = resolver.resolve(host_, std::to_string(port_));
                do_connect(endpoints);
            } catch (const std::exception&) {
                state_ = ConnectionState::DISCONNECTED;
                // Try again later
                if (auto_reconnect_) {
                    try_reconnect();
                }
            }
        }
    });
}

} // namespace mir2::client
