/**
 * @file client_registry.h
 * @brief 客户端注册表
 */

#ifndef MIR2_SERVER_LOGIC_SERVICES_CLIENT_REGISTRY_H_
#define MIR2_SERVER_LOGIC_SERVICES_CLIENT_REGISTRY_H_

#include <cstdint>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace mir2::logic {

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

}  // namespace mir2::logic

#endif  // MIR2_SERVER_LOGIC_SERVICES_CLIENT_REGISTRY_H_
