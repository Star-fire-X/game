/**
 * @file storage_engine_demo.cc
 * @brief StorageEngine API演示
 */

#include "storage_engine/storage_engine.h"
#include "storage_engine/test_backend_mocks.h"
#include <iostream>
#include <cstring>

using namespace mir2::storage_engine;

namespace {
std::vector<uint8_t> EncodeInt32(int32_t value) {
    std::vector<uint8_t> data(sizeof(int32_t));
    std::memcpy(data.data(), &value, sizeof(int32_t));
    return data;
}

int32_t DecodeInt32(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(int32_t)) {
        return 0;
    }
    int32_t value = 0;
    std::memcpy(&value, data.data(), sizeof(int32_t));
    return value;
}
}  // namespace

int main() {
    std::cout << "=== StorageEngine Phase 2 Demo ===" << std::endl;

    // 1. 初始化
    auto backend = std::make_unique<test::NoopStorageBackend>();

    if (!StorageEngine::Initialize(std::move(backend))) {
        std::cerr << "初始化失败" << std::endl;
        return 1;
    }

    auto& engine = StorageEngine::Instance();
    std::cout << "✓ 初始化成功" << std::endl;

    // 2. Set/Get操作（L1命中）
    engine.Set("test_l1", {1, 2, 3});
    auto l1_hit = engine.Get("test_l1");
    if (l1_hit) {
        std::cout << "✓ L1命中: test_l1" << std::endl;
    }

    // 3. Set操作（金币100）
    if (engine.Set("player:123:gold", EncodeInt32(100))) {
        std::cout << "✓ Set操作成功" << std::endl;
    }

    // 4. Update操作（纯计算）
    bool update_success = engine.Update("player:123:gold",
        [](const std::optional<VersionedData>& current) -> std::optional<std::vector<uint8_t>> {
            std::cout << "  -> update_fn被调用（纯计算）" << std::endl;

            if (!current) {
                std::cout << "  -> 数据不存在，放弃" << std::endl;
                return std::nullopt;
            }

            if (current->data.size() < sizeof(int32_t)) {
                std::cout << "  -> 数据格式错误，放弃" << std::endl;
                return std::nullopt;
            }

            int32_t gold = DecodeInt32(current->data);
            std::cout << "  -> 当前金币: " << gold << std::endl;

            // 扣除50
            if (gold < 50) {
                std::cout << "  -> 余额不足，放弃" << std::endl;
                return std::nullopt;
            }

            int32_t new_gold = gold - 50;
            std::cout << "  -> 扣除后金币: " << new_gold << std::endl;

            return EncodeInt32(new_gold);
        }
    );

    std::cout << "✓ Update操作: " << (update_success ? "成功" : "失败") << std::endl;

    auto new_gold = engine.Get("player:123:gold");
    if (new_gold) {
        std::cout << "✓ 更新后金币: " << DecodeInt32(new_gold->data) << std::endl;
    }

    // 5. 健康指标
    auto metrics = engine.GetHealthMetrics();
    std::cout << "✓ 健康指标:" << std::endl;
    std::cout << "  - 健康状态: " << (metrics.is_healthy ? "是" : "否") << std::endl;
    std::cout << "  - L1命中率: " << (metrics.l1_hit_ratio * 100) << "%" << std::endl;
    std::cout << "  - L2命中率: " << (metrics.l2_hit_ratio * 100) << "%" << std::endl;
    std::cout << "  - L1大小: " << metrics.l1_size << std::endl;
    std::cout << "  - 待同步数: " << metrics.pending_syncs << std::endl;
    std::cout << "  - 总更新次数: " << metrics.total_updates << std::endl;
    std::cout << "  - 成功更新: " << metrics.successful_updates << std::endl;
    std::cout << "  - 冲突次数: " << metrics.update_conflicts << std::endl;
    std::cout << "  - 放弃次数: " << metrics.update_aborted << std::endl;

    // 6. 关闭
    StorageEngine::Shutdown();
    std::cout << "✓ 优雅关闭" << std::endl;

    std::cout << "\n=== Phase 2 完成 ===" << std::endl;
    std::cout << "✓ PIMPL模式：零头文件污染" << std::endl;
    std::cout << "✓ 类型擦除：Update函数非模板化" << std::endl;
    std::cout << "✓ FNV-1a哈希：均匀分布" << std::endl;
    std::cout << "✓ 64路分段锁：高并发支持" << std::endl;

    return 0;
}
