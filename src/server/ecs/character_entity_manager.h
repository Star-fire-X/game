/**
 * @file character_entity_manager.h
 * @brief 角色实体管理器
 */

#ifndef LEGEND2_SERVER_ECS_CHARACTER_ENTITY_MANAGER_H
#define LEGEND2_SERVER_ECS_CHARACTER_ENTITY_MANAGER_H

#include "common/character_data.h"

#include <entt/entt.hpp>
#include <cassert>
#include <cstdint>
#include <optional>
#include <thread>
#include <unordered_map>

namespace mir2::ecs {

class EventBus;
class RegistryManager;

/**
 * @brief 角色实体管理器
 *
 * 负责角色实体的创建、索引、加载与生命周期管理。
 *
 * @warning 非线程安全！所有方法必须在同一线程调用。
 * @note 基于单线程设计优化，使用 std::unordered_map 而非并发容器。
 * @see src/server/ecs/THREADING.md 了解线程模型详情
 */
class CharacterEntityManager {
 public:
  enum class SaveResult {
    kSuccess,
    kNotDirty,
    kEntityNotFound,
    kSaveFailed,
  };

  enum class ErrorPolicy {
    kRetainDirtyFlag,
    kClearDirtyFlag,
  };

  /// 旧接口：绑定单 Registry（兼容已有测试与单 World 使用）
  explicit CharacterEntityManager(entt::registry& registry);

  /// 新接口：通过 RegistryManager 跨 World 管理角色
  explicit CharacterEntityManager(RegistryManager& registry_manager);

  /// 获取或创建角色实体
  entt::entity GetOrCreate(uint32_t character_id);

  /// 获取或创建角色实体（指定地图）
  entt::entity GetOrCreate(uint32_t character_id, uint32_t map_id);

  /// 通过创建请求创建角色实体
  entt::entity CreateFromRequest(uint32_t character_id,
                                 const mir2::common::CharacterCreateRequest& request,
                                 EventBus* event_bus = nullptr);

  /// 预加载角色数据并创建实体
  entt::entity Preload(const mir2::common::CharacterData& data);

  /// 尝试获取角色实体
  std::optional<entt::entity> TryGet(uint32_t character_id);

  /// 获取角色所在地图
  std::optional<uint32_t> TryGetMapId(uint32_t character_id) const;

  /// 获取角色对应的 Registry（若不存在返回 nullptr）
  entt::registry* TryGetRegistry(uint32_t character_id);
  const entt::registry* TryGetRegistry(uint32_t character_id) const;

  /// 设置角色位置（并更新地图）
  bool SetPosition(uint32_t character_id, int x, int y, uint32_t map_id);

  /// 跨地图移动角色（会转移 Registry）
  bool MoveToMap(uint32_t character_id, uint32_t new_map_id);
  bool MoveToMap(uint32_t character_id, uint32_t new_map_id, int x, int y);

  /// 登录时标记角色在线
  void OnLogin(uint32_t character_id, EventBus* event_bus = nullptr);

  /// 断线时保存角色并标记离线
  void OnDisconnect(uint32_t character_id, EventBus* event_bus = nullptr);

  /// 周期更新（保存与超时清理）
  void Update(float delta_time);

  /// 保存单个角色数据
  std::optional<mir2::common::CharacterData> Save(uint32_t character_id);

  /// 仅在脏标记存在时保存
  SaveResult SaveIfDirty(uint32_t character_id);

  /// 保存全部角色数据
  void SaveAll();

  /// 保存全部脏数据
  void SaveAllDirty();

  /// 获取已存储角色数据（用于测试或预加载验证）
  std::optional<mir2::common::CharacterData> GetStoredData(uint32_t character_id) const;

  /// 获取索引大小（测试用）
  size_t GetIndexSize() const;

  void SetSaveIntervalSeconds(float seconds) { save_interval_seconds_ = seconds; }
  void SetTimeoutSeconds(float seconds) { timeout_seconds_ = seconds; }
  void SetErrorPolicy(ErrorPolicy policy) { error_policy_ = policy; }

 private:
  struct SessionState {
    bool connected = false;
    float time_since_last_save = 0.0f;
    float time_since_disconnect = 0.0f;
  };

  entt::registry* ResolveRegistry(uint32_t map_id) const;
  entt::registry* EnsureRegistry(uint32_t map_id);
  std::optional<entt::entity> FindEntityByComponentId(uint32_t character_id,
                                                      uint32_t map_id);
  std::optional<entt::entity> FindEntityByComponentId(uint32_t character_id);
  bool IndexCharacter(uint32_t character_id, uint32_t map_id, entt::entity entity);
  void UnindexCharacter(uint32_t character_id);
  void Touch(uint32_t character_id);
  static int64_t NowSeconds();
  void AssertSameThread() const {
    assert(std::this_thread::get_id() == thread_id_ &&
           "CharacterEntityManager: 所有方法必须在同一线程调用");
  }

  std::thread::id thread_id_;

  /// 兼容单 Registry 模式（World 单实例）
  entt::registry* registry_ = nullptr;

  /// 多 World 模式：通过 RegistryManager 获取指定地图的 Registry
  RegistryManager* registry_manager_ = nullptr;

  /// character_id -> entt::entity（需结合 character_to_map_ 才能定位 Registry）
  std::unordered_map<uint32_t, entt::entity> id_index_;

  /// character_id -> map_id
  std::unordered_map<uint32_t, uint32_t> character_to_map_;
  std::unordered_map<uint32_t, mir2::common::CharacterData> stored_characters_;
  std::unordered_map<uint32_t, SessionState> sessions_;
  float save_interval_seconds_ = 30.0f;
  float timeout_seconds_ = 60.0f;
  ErrorPolicy error_policy_ = ErrorPolicy::kRetainDirtyFlag;
};

}  // namespace mir2::ecs

#endif  // LEGEND2_SERVER_ECS_CHARACTER_ENTITY_MANAGER_H
