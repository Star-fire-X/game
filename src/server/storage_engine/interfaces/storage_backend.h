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

    using BatchItems =
        std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>>;

    /**
     * @brief 保存单个实体
     *
     * 版本号必须由调用方提供且保持单调递增，否则后端可拒绝写入
     * 或保留旧值（例如带版本比较的 UPSERT 语义）。
     */
    virtual StorageResult Save(
        const std::string& key,
        uint64_t version,
        const std::vector<uint8_t>& data) = 0;

    /**
     * @brief 批量保存
     */
    virtual StorageResult SaveBatch(const BatchItems& items) = 0;

    /**
     * @brief 删除单个实体
     *
     * @param key 键
     * @param version 删除操作版本（用于防止旧版本删除覆盖新写入）
     * @param hard_delete true=物理删除，false=逻辑删除（由后端决定具体语义）
     */
    virtual StorageResult Delete(const std::string& key,
                                 uint64_t version,
                                 bool hard_delete) {
        (void)key;
        (void)version;
        (void)hard_delete;
        return StorageResult{true, "", 0};
    }

    /**
     * @brief 批量删除
     */
    virtual StorageResult DeleteBatch(
        const std::vector<std::pair<std::string, uint64_t>>& items,
        bool hard_delete) {
        (void)items;
        (void)hard_delete;
        return StorageResult{true, "", 0};
    }

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

/**
 * @brief 原子批量扩展能力接口
 *
 * 注意：新增后端能力必须通过扩展接口提供，避免直接扩展 IStorageBackend ABI。
 */
class IAtomicBatchStorageBackend {
 public:
  virtual ~IAtomicBatchStorageBackend() = default;

  virtual IStorageBackend::StorageResult SaveBatchAtomic(
      const IStorageBackend::BatchItems& items) = 0;

  virtual IStorageBackend::StorageResult DeleteBatchAtomic(
      const std::vector<std::pair<std::string, uint64_t>>& items,
      bool hard_delete) {
    (void)items;
    (void)hard_delete;
    return IStorageBackend::StorageResult{true, "", 0};
  }
};

}  // namespace mir2::storage_engine

#endif  // MIR2_STORAGE_ENGINE_INTERFACES_STORAGE_BACKEND_H_
