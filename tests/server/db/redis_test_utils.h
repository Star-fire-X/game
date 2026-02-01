#ifndef MIR2_TESTS_SERVER_DB_REDIS_TEST_UTILS_H
#define MIR2_TESTS_SERVER_DB_REDIS_TEST_UTILS_H

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
using socket_t = SOCKET;
using socklen_t = int;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
constexpr int kShutdownBoth = SD_BOTH;

inline bool InitSockets() {
    WSADATA wsa_data{};
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
}

inline void CleanupSockets() {
    WSACleanup();
}

inline int CloseSocket(socket_t socket_fd) {
    return closesocket(socket_fd);
}
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
constexpr int kShutdownBoth = SHUT_RDWR;

inline bool InitSockets() {
    return true;
}

inline void CleanupSockets() {
}

inline int CloseSocket(socket_t socket_fd) {
    return ::close(socket_fd);
}
#endif

namespace mir2::db::test_utils {

class FakeRedisServer {
public:
    FakeRedisServer() {
        if (!InitSockets()) {
            return;
        }
        sockets_initialized_ = true;

        server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ == kInvalidSocket) {
            return;
        }

        int opt = 1;
        ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            CloseSocket(server_fd_);
            server_fd_ = kInvalidSocket;
            return;
        }

        socklen_t len = sizeof(addr);
        if (::getsockname(server_fd_, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            port_ = ntohs(addr.sin_port);
        }

        if (::listen(server_fd_, 1) < 0) {
            CloseSocket(server_fd_);
            server_fd_ = kInvalidSocket;
            return;
        }

        thread_ = std::thread([this]() { run(); });
    }

    ~FakeRedisServer() {
        stop_ = true;
        if (client_fd_ != kInvalidSocket) {
            ::shutdown(client_fd_, kShutdownBoth);
            CloseSocket(client_fd_);
            client_fd_ = kInvalidSocket;
        }
        if (server_fd_ != kInvalidSocket) {
            ::shutdown(server_fd_, kShutdownBoth);
            CloseSocket(server_fd_);
            server_fd_ = kInvalidSocket;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (sockets_initialized_) {
            CleanupSockets();
        }
    }

    uint16_t port() const { return port_; }
    bool is_ready() const { return server_fd_ != kInvalidSocket && port_ != 0; }

private:
    void run() {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        client_fd_ = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd_ == kInvalidSocket) {
            return;
        }

        while (!stop_) {
            auto command = read_command();
            if (command.empty()) {
                break;
            }
            handle_command(command);
        }
    }

    std::vector<std::string> read_command() {
        char prefix = 0;
        if (!read_char(prefix)) {
            return {};
        }

        if (prefix == '*') {
            std::string line;
            if (!read_line(line)) {
                return {};
            }
            int count = std::stoi(line);
            std::vector<std::string> args;
            args.reserve(static_cast<size_t>(count));

            for (int i = 0; i < count; ++i) {
                char bulk_prefix = 0;
                if (!read_char(bulk_prefix) || bulk_prefix != '$') {
                    return {};
                }
                std::string len_line;
                if (!read_line(len_line)) {
                    return {};
                }
                int len = std::stoi(len_line);
                std::string value;
                if (!read_exact(static_cast<size_t>(len), value)) {
                    return {};
                }
                if (!read_crlf()) {
                    return {};
                }
                args.push_back(std::move(value));
            }
            return args;
        }

        std::string line;
        line.push_back(prefix);
        if (!read_line(line)) {
            return {};
        }
        return split_inline(line);
    }

    bool read_char(char& out) {
        int read_bytes = ::recv(client_fd_, &out, 1, 0);
        return read_bytes == 1;
    }

    bool read_line(std::string& line) {
        char ch = 0;
        while (read_char(ch)) {
            if (ch == '\r') {
                char next = 0;
                if (!read_char(next) || next != '\n') {
                    return false;
                }
                return true;
            }
            line.push_back(ch);
        }
        return false;
    }

    bool read_exact(size_t len, std::string& out) {
        out.resize(len);
        size_t offset = 0;
        while (offset < len) {
            int bytes = ::recv(client_fd_, &out[offset], static_cast<int>(len - offset), 0);
            if (bytes <= 0) {
                return false;
            }
            offset += static_cast<size_t>(bytes);
        }
        return true;
    }

    bool read_crlf() {
        char cr = 0;
        char lf = 0;
        return read_char(cr) && read_char(lf) && cr == '\r' && lf == '\n';
    }

    std::vector<std::string> split_inline(const std::string& line) {
        std::vector<std::string> parts;
        std::string current;
        for (char ch : line) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!current.empty()) {
                    parts.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(ch);
            }
        }
        if (!current.empty()) {
            parts.push_back(current);
        }
        return parts;
    }

    void handle_command(const std::vector<std::string>& command) {
        if (command.empty()) {
            send_error("ERR empty command");
            return;
        }

        std::string verb = command[0];
        for (char& ch : verb) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }

        if (verb == "SET" && command.size() >= 3) {
            kv_[command[1]] = command[2];
            send_status("OK");
            return;
        }

        if (verb == "SETEX" && command.size() >= 4) {
            kv_[command[1]] = command[3];
            send_status("OK");
            return;
        }

        if (verb == "GET" && command.size() >= 2) {
            auto it = kv_.find(command[1]);
            if (it == kv_.end()) {
                send_null_bulk();
            } else {
                send_bulk(it->second);
            }
            return;
        }

        if (verb == "DEL" && command.size() >= 2) {
            int64_t removed = 0;
            for (size_t i = 1; i < command.size(); ++i) {
                removed += static_cast<int64_t>(kv_.erase(command[i]));
                auto set_it = sets_.find(command[i]);
                if (set_it != sets_.end()) {
                    sets_.erase(set_it);
                    ++removed;
                }
            }
            send_integer(removed);
            return;
        }

        if (verb == "SADD" && command.size() >= 3) {
            auto& set = sets_[command[1]];
            bool inserted = set.insert(command[2]).second;
            send_integer(inserted ? 1 : 0);
            return;
        }

        if (verb == "SREM" && command.size() >= 3) {
            auto it = sets_.find(command[1]);
            if (it == sets_.end()) {
                send_integer(0);
                return;
            }
            bool removed = it->second.erase(command[2]) > 0;
            send_integer(removed ? 1 : 0);
            return;
        }

        if (verb == "SMEMBERS" && command.size() >= 2) {
            std::vector<std::string> members;
            auto it = sets_.find(command[1]);
            if (it != sets_.end()) {
                members.assign(it->second.begin(), it->second.end());
            }
            send_array(members);
            return;
        }

        send_error("ERR unknown command");
    }

    void send_status(const std::string& text) {
        send_raw("+" + text + "\r\n");
    }

    void send_error(const std::string& text) {
        send_raw("-" + text + "\r\n");
    }

    void send_integer(int64_t value) {
        send_raw(":" + std::to_string(value) + "\r\n");
    }

    void send_bulk(const std::string& value) {
        send_raw("$" + std::to_string(value.size()) + "\r\n" + value + "\r\n");
    }

    void send_null_bulk() {
        send_raw("$-1\r\n");
    }

    void send_array(const std::vector<std::string>& values) {
        std::string payload = "*" + std::to_string(values.size()) + "\r\n";
        for (const auto& value : values) {
            payload += "$" + std::to_string(value.size()) + "\r\n";
            payload += value + "\r\n";
        }
        send_raw(payload);
    }

    void send_raw(const std::string& payload) {
        const char* data = payload.c_str();
        size_t total = payload.size();
        while (total > 0) {
            int sent = ::send(client_fd_, data, static_cast<int>(total), 0);
            if (sent <= 0) {
                break;
            }
            data += sent;
            total -= static_cast<size_t>(sent);
        }
    }

    socket_t server_fd_ = kInvalidSocket;
    socket_t client_fd_ = kInvalidSocket;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    uint16_t port_ = 0;
    bool sockets_initialized_ = false;

    std::unordered_map<std::string, std::string> kv_;
    std::unordered_map<std::string, std::unordered_set<std::string>> sets_;
};

}  // namespace mir2::db::test_utils

#endif  // MIR2_TESTS_SERVER_DB_REDIS_TEST_UTILS_H
