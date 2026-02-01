/**
 * @file world.h
 * @brief ECS世界封装
 */

#ifndef MIR2_ECS_WORLD_H
#define MIR2_ECS_WORLD_H

#include <entt/entt.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace mir2::game::npc {
class NpcAISystem;
}  // namespace mir2::game::npc

namespace mir2::ecs {

class EventBus;

/**
 * @brief 系统优先级
 */
enum class SystemPriority {
    kMovement = 100,
    kInventory = 150,
    kCombat = 200,
    kLevelUp = 300,
};

/**
 * @brief ECS系统基类
 */
class System {
 public:
    explicit System(SystemPriority priority) : priority_(priority) {}
    virtual ~System() = default;

    SystemPriority Priority() const { return priority_; }

    virtual void Update(entt::registry& registry, float delta_time) = 0;

 private:
    SystemPriority priority_;
};

/**
 * @brief ECS世界
 *
 * @warning 非线程安全！Update() 必须在单线程调用。
 * @note 所有 System 按优先级顺序执行，不并行。
 * @see src/server/ecs/THREADING.md 了解线程模型详情
 */
class World {
 public:
    /**
     * @brief 构造 World
     * @param reserve_capacity 预估实体容量（默认 1000，单地图预估玩家数），用于预分配常用组件池以降低分配/rehash成本
     */
    explicit World(std::size_t reserve_capacity = 1000);
    ~World();

    /**
     * @brief 获取底层Registry
     */
    entt::registry& Registry() { return registry_; }

    /**
     * @brief 获取事件总线
     */
    EventBus& GetEventBus();

    /**
     * @brief 获取 NPC AI 系统
     */
    game::npc::NpcAISystem* GetNpcAISystem();
    const game::npc::NpcAISystem* GetNpcAISystem() const;

    /**
     * @brief 创建并注册系统（World拥有系统生命周期）
     * @tparam T 系统类型
     * @tparam Args 构造参数类型
     * @return 系统指针（由World管理）
     */
    template<typename T, typename... Args>
    T* CreateSystem(Args&&... args) {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = system.get();
        systems_.push_back(std::move(system));
        systems_dirty_ = true;
        return ptr;
    }

    /**
     * @brief 清空已注册系统
     */
    void ClearSystems();

    /**
     * @brief Tick更新入口（非线程安全，仅支持单线程调用）
     */
    void Update(float delta_time);

    /// 获取已注册系统数量（测试用）
    size_t GetSystemCount() const { return systems_.size(); }

 private:
    entt::registry registry_;
    std::unique_ptr<EventBus> event_bus_;
    std::unique_ptr<game::npc::NpcAISystem> npc_ai_system_;
    std::vector<std::unique_ptr<System>> systems_;
    bool systems_dirty_ = false;
};

}  // namespace mir2::ecs

#endif  // MIR2_ECS_WORLD_H
