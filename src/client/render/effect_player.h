// =============================================================================
// Legend2 特效播放器 (Effect Player)
//
// 功能说明:
//   - 管理客户端特效播放与渲染
//   - 支持技能/普通特效
//   - 统一特效生命周期管理
// =============================================================================

#ifndef LEGEND2_CLIENT_RENDER_EFFECT_PLAYER_H
#define LEGEND2_CLIENT_RENDER_EFFECT_PLAYER_H

#include "render/renderer.h"
#include "client/resource/resource_loader.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace mir2::render {

// 特效类型
enum class EffectPlayType : uint8_t {
    CAST = 1,       // 施法特效
    PROJECTILE = 2, // 弹道特效
    HIT = 3,        // 命中特效
    AOE = 4         // 范围特效
};

// 技能特效类型
enum class SkillEffectType {
    kInstant,      // Immediate effect at target
    kProjectile,   // Travels from caster to target
    kAreaPersist,  // AOE that persists for duration
    kBeam          // Line from caster to target
};

// 活动特效实例
struct ActiveEffect {
    std::string effect_id;
    int x = 0;
    int y = 0;
    uint8_t direction = 0;
    float elapsed_ms = 0.0f;
    float duration_ms = 0.0f;
    int current_frame = 0;
    bool finished = false;
};

// 弹道特效实例
struct ProjectileEffect {
    uint32_t effect_id;
    int start_x = 0;
    int start_y = 0;   // Caster position (world coords)
    int end_x = 0;
    int end_y = 0;     // Target position
    int64_t start_time_ms = 0;
    int64_t duration_ms = 0;    // Travel time
    bool active = false;
    
    float get_progress(int64_t now_ms) const;
    void get_current_position(int64_t now_ms, int& x, int& y) const;
};

// 伤害数字
struct DamageNumber {
    int value = 0;
    bool is_healing = false;   // false=red damage, true=green healing
    int world_x = 0;
    int world_y = 0;
    int64_t spawn_time_ms = 0;
    float alpha = 1.0f;
    float offset_y = 0.0f;  // Floats upward
    
    static constexpr int64_t LIFETIME_MS = 1500;
};

// 特效播放器
class EffectPlayer {
public:
    EffectPlayer(SDLRenderer& renderer, mir2::client::ResourceManager& resource_manager);
    ~EffectPlayer() = default;

    // 播放特效
    void play_effect(const std::string& effect_id, int x, int y,
                     uint8_t direction = 0, uint32_t duration_ms = 500);

    // 播放技能特效
    void play_skill_effect(uint64_t caster_id, uint64_t target_id, uint32_t skill_id,
                           EffectPlayType type, const std::string& effect_id,
                           int x, int y, uint32_t duration_ms);

    // 预加载技能特效（逻辑占位）
    void preload_skill_effects(const std::vector<uint32_t>& effect_ids);

    // 播放技能特效（数值ID）
    void play_skill_effect(uint32_t effect_id, int x, int y);

    // 播放弹道特效
    void play_projectile_effect(uint32_t effect_id, int start_x, int start_y,
                                int end_x, int end_y, int64_t travel_ms);

    // 生成伤害数字
    void spawn_damage_number(int value, bool is_healing, int world_x, int world_y);

    // 更新弹道与伤害数字
    void update_projectiles(int64_t now_ms);
    void update_damage_numbers(int64_t now_ms);

    // 渲染弹道与伤害数字
    void render_projectiles(IRenderer& renderer, const Camera& camera);
    void render_damage_numbers(IRenderer& renderer, const Camera& camera);

    // 更新所有活动特效
    void update(float delta_time);

    // 渲染所有活动特效
    void render(const Camera& camera);

    // 清除所有特效
    void clear();

private:
    static constexpr size_t kMaxActiveEffects = 256;

    SDLRenderer& renderer_;
    mir2::client::ResourceManager& resource_manager_;
    std::vector<ActiveEffect> active_effects_;
    std::vector<ProjectileEffect> projectiles_;
    std::vector<DamageNumber> damage_numbers_;
    std::unordered_set<uint32_t> preloaded_effects_;
    int64_t current_time_ms_ = 0;

    // 获取特效纹理
    std::shared_ptr<Texture> get_effect_texture(const std::string& effect_id, int frame);
};

} // namespace mir2::render

#endif // LEGEND2_CLIENT_RENDER_EFFECT_PLAYER_H
