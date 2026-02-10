/**
 * @file storage_backend.h
 * @brief 存储后端抽象接口
 */

#ifndef MIR2_STORAGE_ENGINE_INTERFACES_STORAGE_BACKEND_H_
#define MIR2_STORAGE_ENGINE_INTERFACES_STORAGE_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace mir2::storage_engine {

/**
 * @brief 存储后端抽象接口
 *
 * 实现类：PostgresBackend, MockBackend等
 */
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    struct StorageResult {
        bool success;
        std::string error_message;
        int64_t operation_time_ms = 0;
    };

    /**
     * @brief 保存单个实体
     */
    virtual StorageResult Save(
        const std::string& key,
        uint64_t version,
        const std::vector<uint8_t>& data) = 0;

    /**
     * @brief 批量保存
     */
    virtual StorageResult SaveBatch(
        const std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>& items) = 0;

    /**
     * @brief 加载数据
     */
    virtual std::optional<std::pair<uint64_t, std::vector<uint8_t>>> Load(
        const std::string& key) = 0;

    /**
     * @brief 加载所有数据（启动恢复）
     */
    virtual std::optional<std::map<std::string, std::pair<uint64_t, std::vector<uint8_t>>>> LoadAll() = 0;

    /**
     * @brief 验证数据完整性
     */
    virtual StorageResult Validate() = 0;

    /**
     * @brief 健康检查
     */
    virtual bool IsHealthy() const = 0;
};

}  // namespace mir2::storage_engine

#endif  // MIR2_STORAGE_ENGINE_INTERFACES_STORAGE_BACKEND_H_
