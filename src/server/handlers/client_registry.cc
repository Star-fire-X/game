#include "handlers/client_registry.h"

namespace legend2::handlers {

void ClientRegistry::Track(uint64_t client_id) {
    if (client_id == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.insert(client_id);
}

void ClientRegistry::Remove(uint64_t client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(client_id);
}

std::vector<uint64_t> ClientRegistry::GetAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<uint64_t>(clients_.begin(), clients_.end());
}

bool ClientRegistry::Contains(uint64_t client_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.find(client_id) != clients_.end();
}

}  // namespace legend2::handlers
