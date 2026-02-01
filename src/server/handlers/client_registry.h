/**
 * @file client_registry.h
 * @brief 客户端注册表
 */

#ifndef LEGEND2_SERVER_HANDLERS_CLIENT_REGISTRY_H
#define LEGEND2_SERVER_HANDLERS_CLIENT_REGISTRY_H

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace legend2::handlers {

/**
 * @brief 记录已知客户端
 */
class ClientRegistry {
public:
    void Track(uint64_t client_id);
    void Remove(uint64_t client_id);
    std::vector<uint64_t> GetAll() const;
    bool Contains(uint64_t client_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_set<uint64_t> clients_;
};

}  // namespace legend2::handlers

#endif  // LEGEND2_SERVER_HANDLERS_CLIENT_REGISTRY_H
