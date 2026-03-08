#ifndef MIR2_SERVER_CACHE_POSTGRES_BACKEND_INTERFACE_H_
#define MIR2_SERVER_CACHE_POSTGRES_BACKEND_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cache_types.h"

namespace mir2::cache {

// PostgreSQL 后端接口
// 用于 AsyncPersistenceQueue 与数据库的交互
class PostgresBackend {
public:
    // 虚析构函数
    virtual ~PostgresBackend() = default;

    // 保存单个实体
    // 返回 true 表示成功，false 表示失败
    virtual bool SaveEntity(const std::string& key, const VersionedData& data,
                            uint64_t version) = 0;

    // 批量保存实体
    // 返回实际保存的数量
    virtual uint32_t BatchSaveEntities(
        const std::vector<std::pair<std::string, VersionedData>>& batch) = 0;

    // 加载单个实体
    // 返回 std::nullopt 表示不存在或加载失败
    virtual std::optional<VersionedData> LoadEntity(const std::string& key) = 0;

    // 检查连接是否��用
    virtual bool IsHealthy() const = 0;
};

}  // namespace mir2::cache

#endif  // MIR2_SERVER_CACHE_POSTGRES_BACKEND_INTERFACE_H_
