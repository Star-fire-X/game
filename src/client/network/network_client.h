/**
 * @file network_client.h
 * @brief Legend2 网络客户端
 *
 * 基于Asio的TCP客户端实现，包括：
 * - 连接管理
 * - 消息发送和接收
 * - 心跳和超时处理
 * - 自动重连
 */

#ifndef LEGEND2_CLIENT_NETWORK_CLIENT_H
#define LEGEND2_CLIENT_NETWORK_CLIENT_H

#include "common/protocol/packet_codec.h"
#include "common/types.h"
#include <asio.hpp>
#include <string>
#include <queue>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>
#include <memory>
#include <thread>
#include <array>

namespace mir2::client {

using mir2::common::ErrorCode;

/**
 * @brief 连接状态
 */
enum class ConnectionState {
    DISCONNECTED,   ///< 已断开
    CONNECTING,     ///< 连接中
    CONNECTED,      ///< 已连接
    RECONNECTING    ///< 重连中
};

using NetworkPacket = mir2::common::NetworkPacket;

/**
 * @brief 网络客户端接口
 *
 * Threading: implementations may use internal IO threads; update() is expected to be
 * called from the main thread to pump callbacks.
 *
 * Ownership: typically owned by NetworkManager via unique_ptr.
 */
class INetworkClient {
public:
    using MessageCallback = std::function<void(const NetworkPacket&)>;
    using EventCallback = std::function<void()>;

    virtual ~INetworkClient() = default;

    virtual bool connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    virtual void send(uint16_t msg_id, const std::vector<uint8_t>& payload) = 0;
    virtual std::optional<NetworkPacket> receive() = 0;
    virtual void update() = 0;

    virtual ConnectionState get_state() const = 0;
    virtual ErrorCode get_last_error() const = 0;

    virtual void set_on_message(MessageCallback callback) = 0;
    virtual void set_on_disconnect(EventCallback callback) = 0;
    virtual void set_on_connect(EventCallback callback) = 0;
};

/**
 * @brief 基于Asio的TCP网络客户端
 *
 * Responsibilities:
 * - Connect/disconnect lifecycle and send/receive queues.
 * - Heartbeat/timeout tracking and optional auto-reconnect.
 *
 * Threading: uses an internal IO thread. connect/disconnect/send are called from
 * the main thread; on_connect/on_disconnect may fire on the IO thread; on_message
 * is invoked during update() on the caller thread.
 *
 * Ownership: owns socket/timers and the IO thread; callbacks are stored by value.
 */
class NetworkClient : public INetworkClient {
public:
    /// 构造函数
    NetworkClient();

    /// 析构函数
    ~NetworkClient() override;

    // 不可复制
    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    /**
     * @brief 连接到服务器
     * @param host 服务器主机名或IP地址
     * @param port 服务器端口
     * @return 是否成功发起连接
     */
    bool connect(const std::string& host, uint16_t port) override;

    /// 断开连接
    void disconnect() override;

    /// 检查是否已连接
    bool is_connected() const override;

    /// 发送消息到服务器
    void send(uint16_t msg_id, const std::vector<uint8_t>& payload) override;

    /// 从队列接收消息（非阻塞）
    std::optional<NetworkPacket> receive() override;

    /// 更新客户端（从主线程调用以处理回调）
    void update() override;

    /// 获取当前连接状态
    ConnectionState get_state() const override;

    /// 获取最后的错误码
    ErrorCode get_last_error() const override;

    /// 设置收到消息时的回调
    void set_on_message(MessageCallback callback) override;

    /// 设置断开连接时的回调
    void set_on_disconnect(EventCallback callback) override;

    /// 设置连接成功时的回调
    void set_on_connect(EventCallback callback) override;

    /// 获取往返时间（毫秒）
    uint32_t get_rtt() const;

    /// 启用/禁用自动重连
    void set_auto_reconnect(bool enable);

 private:
    // Asio components
    asio::io_context io_context_;
    std::unique_ptr<asio::ip::tcp::socket> socket_;
    std::unique_ptr<asio::steady_timer> heartbeat_timer_;
    std::unique_ptr<asio::steady_timer> timeout_timer_;
    std::thread io_thread_;

    // Connection info
    std::string host_;
    uint16_t port_ = 0;
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};
    std::atomic<ErrorCode> last_error_{ErrorCode::SUCCESS};
    // Protocol version
    mir2::common::ProtocolVersion protocol_version_{mir2::common::ProtocolVersion::kV2};
    std::atomic<uint16_t> send_sequence_{0};
    std::atomic<uint16_t> recv_sequence_{0};

    // Header buffer for V2 (16 bytes)
    static constexpr size_t HEADER_BUFFER_SIZE = 16;
    std::array<uint8_t, HEADER_BUFFER_SIZE> header_buffer_{};
     std::atomic<bool> auto_reconnect_{false};

     // Message queues
     std::queue<std::vector<uint8_t>> send_queue_;
     std::queue<NetworkPacket> receive_queue_;
     std::mutex send_mutex_;
     std::mutex receive_mutex_;
    bool write_in_progress_ = false;

     // Callbacks
     std::function<void(const NetworkPacket&)> on_message_;
     std::function<void()> on_disconnect_;
     std::function<void()> on_connect_;
    std::mutex callback_mutex_;

    // Read buffer
    std::vector<uint8_t> read_buffer_;
    uint16_t current_msg_id_ = 0;
    uint32_t current_payload_size_ = 0;
    static constexpr size_t READ_BUFFER_SIZE = 8192;
    static constexpr size_t MAX_PACKET_SIZE = mir2::common::kMaxPayloadSize;

    // Heartbeat
    std::atomic<uint32_t> last_heartbeat_time_{0};
    std::atomic<uint32_t> last_heartbeat_send_time_{0};
    std::atomic<uint32_t> rtt_{0};
    uint32_t heartbeat_sequence_ = 0;

    // Internal methods
    void start_io_thread();
    void stop_io_thread();
    void do_connect(const asio::ip::tcp::resolver::results_type& endpoints);
    void do_read_header();
    void do_read_remaining_header(size_t remaining_size);
    void do_read_body(size_t body_length);
    void do_write();
    void start_heartbeat();
    void send_heartbeat();
    void check_timeout();
    void handle_packet(const NetworkPacket& packet);
    bool check_recv_sequence(uint16_t seq);
    void handle_disconnect(const asio::error_code& ec);
    void try_reconnect();
    void process_received_data();
};

} // namespace mir2::client

#endif // LEGEND2_CLIENT_NETWORK_CLIENT_H
