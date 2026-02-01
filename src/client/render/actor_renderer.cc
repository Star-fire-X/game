// =============================================================================
// Legend2 角色渲染器实现 (Actor Renderer Implementation)
//
// 功能说明:
//   - 角色动画帧计算
//   - Z-order 深度排序
//   - 角色绘制(武器、衣服、头发、特效)
//   - 颜色特效处理
//
// 移植自原版 Actor.pas
// =============================================================================

#include "render/actor_renderer.h"
#include "core/path_utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iostream>
#include <iterator>

namespace mir2::render {

// =============================================================================
// 常量定义
// =============================================================================

// 地图格子尺寸
constexpr int TILE_W = 48;
constexpr int TILE_H = 32;

// 状态位掩码 (与原版保持一致)
constexpr uint32_t STATE_INVISIBLE    = 0x00800000;  // 隐身
constexpr uint32_t STATE_SHIELD       = 0x00100000;  // 防御罩
constexpr uint32_t STATE_POISON_GREEN = 0x80000000;  // 绿毒
constexpr uint32_t STATE_POISON_RED   = 0x40000000;  // 红毒
constexpr uint32_t STATE_EFFECT_BLUE  = 0x20000000;  // 蓝色效果
constexpr uint32_t STATE_EFFECT_YELLOW= 0x10000000;  // 黄色效果
constexpr uint32_t STATE_PARALYSIS    = 0x08000000;  // 麻痹
constexpr uint32_t STATE_STONE        = 0x04000000;  // 石化

namespace {

int get_magic_frame_limit(MagicType type, int fallback_frame) {
    int default_frames = MG_EXPLOSION;
    switch (type) {
        case MagicType::READY:
            default_frames = MG_READY;
            break;
        case MagicType::FLY:
        case MagicType::FLY_AXE:
        case MagicType::FLY_ARROW:
        case MagicType::FIRE_BALL:
        case MagicType::EXPLO_BUJAUK:
        case MagicType::BUJAUK_GROUND_EFFECT:
        case MagicType::LIGHTING_THUNDER:
            default_frames = MG_FLY;
            break;
        case MagicType::FIRE_GUN:
            default_frames = FIREGUN_FRAME;
            break;
        case MagicType::EXPLOSION:
        case MagicType::THUNDER:
        case MagicType::FIRE_THUNDER:
        case MagicType::FIRE_WIND:
        case MagicType::GROUND_EFFECT:
        case MagicType::KYUL_KAI:
        default:
            default_frames = MG_EXPLOSION;
            break;
    }

    if (fallback_frame > 0) {
        default_frames = std::min(default_frames, fallback_frame);
    }
    return default_frames;
}

bool uses_magic2_for_effect(int effect_number) {
    switch (effect_number) {
        case 8:   // 强击
        case 27:  // 神兽召唤
        case 33:  // 灭天火
        case 34:  // 无极真气
        case 35:  // 气功波
            return true;
        default:
            return false;
    }
}

uint64_t get_ticks_ms() {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_GetTicks64();
#else
    return static_cast<uint64_t>(SDL_GetTicks());
#endif
}

}  // namespace

const char* get_magic_archive_for_effect(int effect_number) {
    if (effect_number == 31) {
        return "Mon21";  // 魔法阵(若资源不存在会自动失败)
    }
    return uses_magic2_for_effect(effect_number) ? "Magic2" : "Magic";
}

const char* get_magic_archive_for_hit(int hit_effect_number) {
    return (hit_effect_number == 5) ? "Magic2" : "Magic";
}

struct ActionMapping {
    ActorAction action;
    ActionConfig HumanActionConfig::*config_ptr;
    ActionInfo HumanAction::*fallback_ptr;
    bool sets_war_mode;
    bool use_magic;
    int hit_effect_number;
};

const ActionMapping kHumanActionMappings[] = {
    {ActorAction::STAND, &HumanActionConfig::stand, &HumanAction::act_stand, false, false, 0},
    {ActorAction::WALK, &HumanActionConfig::walk, &HumanAction::act_walk, false, false, 0},
    {ActorAction::BACKSTEP, &HumanActionConfig::walk, &HumanAction::act_walk, false, false, 0},
    {ActorAction::RUN, &HumanActionConfig::run, &HumanAction::act_run, false, false, 0},
    {ActorAction::HIT, &HumanActionConfig::hit, &HumanAction::act_hit, true, false, 0},
    {ActorAction::POWER_HIT, &HumanActionConfig::hit, &HumanAction::act_hit, true, false, 1},
    {ActorAction::LONG_HIT, &HumanActionConfig::hit, &HumanAction::act_hit, true, false, 2},
    {ActorAction::WIDE_HIT, &HumanActionConfig::hit, &HumanAction::act_hit, true, false, 3},
    {ActorAction::FIRE_HIT, &HumanActionConfig::hit, &HumanAction::act_hit, true, false, 4},
    {ActorAction::CROSS_HIT, &HumanActionConfig::hit, &HumanAction::act_hit, true, false, 6},
    {ActorAction::HEAVY_HIT, &HumanActionConfig::heavy_hit, &HumanAction::act_heavy_hit, true, false, 0},
    {ActorAction::BIG_HIT, &HumanActionConfig::big_hit, &HumanAction::act_big_hit, true, false, 0},
    {ActorAction::SPELL, &HumanActionConfig::spell, &HumanAction::act_spell, true, true, 0},
    {ActorAction::STRUCK, &HumanActionConfig::struck, &HumanAction::act_struck, false, false, 0},
    {ActorAction::DIE, &HumanActionConfig::die, &HumanAction::act_die, false, false, 0},
    {ActorAction::SITDOWN, &HumanActionConfig::sitdown, &HumanAction::act_sitdown, false, false, 0},
};

const ActionMapping* find_human_action_mapping(ActorAction action) {
    for (const auto& mapping : kHumanActionMappings) {
        if (mapping.action == action) {
            return &mapping;
        }
    }
    return nullptr;
}

// =============================================================================
// ActorRenderer 构造与析构
// =============================================================================

ActorRenderer::ActorRenderer(SDLRenderer& renderer, ResourceManager& resource_manager, ITextureCache& texture_cache)
    : renderer_(renderer)
    , resource_manager_(resource_manager)
    , texture_cache_(texture_cache)
    , config_loader_("Data/animations/")
{
    int loaded = config_loader_.load_all_configs();
    if (loaded == 0) {
        std::cerr << "[ActorRenderer] No animation configs loaded, using hardcoded values\n";
        use_config_system_ = false;
    } else {
        std::cout << "[ActorRenderer] Loaded " << loaded << " animation configs\n";
    }
}

ActorRenderer::~ActorRenderer() {
    clear_texture_cache();
}

// =============================================================================
// 帧计算实现 (Frame Calculation)
// =============================================================================

void ActorRenderer::calc_actor_frame(Actor& actor) {
    if (actor.data.race == ActorRace::HUMAN) {
        calc_human_frame(actor);
    } else {
        calc_monster_frame(actor);
    }
}

void ActorRenderer::calc_human_frame(Actor& actor) {
    auto& anim = actor.anim;
    int dir = actor.data.direction;
    const HumanActionConfig* config = nullptr;

    // 确保方向在有效范围内
    if (dir < 0 || dir > 7) {
        dir = 0;
        actor.data.direction = 0;
    }

    if (use_config_system_) {
        const Gender gender = (actor.data.sex == 0) ? Gender::MALE : Gender::FEMALE;
        CharacterClass character_class = CharacterClass::WARRIOR;
        switch (actor.data.job) {
            case 1:
                character_class = CharacterClass::MAGE;
                break;
            case 2:
                character_class = CharacterClass::TAOIST;
                break;
            default:
                character_class = CharacterClass::WARRIOR;
                break;
        }
        config = config_loader_.get_human_config(gender, character_class);
        if (!config) {
            static std::array<std::array<bool, 3>, 2> logged_missing{};
            const size_t gender_index = (gender == Gender::FEMALE) ? 1U : 0U;
            const size_t class_index = static_cast<size_t>(character_class);
            if (gender_index < logged_missing.size() &&
                class_index < logged_missing[gender_index].size() &&
                !logged_missing[gender_index][class_index]) {
                std::cerr << "[ActorRenderer] Missing human config (gender="
                          << (gender == Gender::MALE ? "male" : "female")
                          << ", class=" << static_cast<int>(character_class)
                          << "), using hardcoded values\n";
                logged_missing[gender_index][class_index] = true;
            }
        }
    }

    anim.def_frame_count = config ? config->stand.frame : HA.act_stand.frame;

    auto apply_action = [&](const ActionConfig* action_config, const ActionInfo* fallback_action) {
        if (action_config) {
            anim.start_frame = action_config->get_frame_index(dir, 0);
            anim.end_frame = anim.start_frame + action_config->frame - 1;
            anim.frame_time = action_config->ftime;
        } else {
            anim.start_frame = fallback_action->get_frame_index(dir, 0);
            anim.end_frame = anim.start_frame + fallback_action->frame - 1;
            anim.frame_time = fallback_action->ftime;
        }
    };

    auto apply_mapping = [&](const ActionMapping& mapping) {
        const ActionConfig* action_config = config ? &(config->*mapping.config_ptr) : nullptr;
        const ActionInfo* fallback_action = &(HA.*mapping.fallback_ptr);
        apply_action(action_config, fallback_action);

        if (mapping.sets_war_mode) {
            actor.data.war_mode = true;
            anim.war_mode_time = get_ticks_ms();
        }

        if (mapping.use_magic) {
            anim.use_magic = true;
            anim.cur_eff_frame = 0;
            anim.spell_frame = DEF_SPELL_FRAME;
        }

        if (mapping.hit_effect_number > 0) {
            anim.hit_effect = true;
            anim.hit_effect_number = mapping.hit_effect_number;
        }
    };

    if (anim.current_action == ActorAction::RUSH) {
        // 冲刺动作交替使用左右动作
        const bool use_left = (anim.rush_dir == 0);
        ActionConfig HumanActionConfig::*rush_config =
            use_left ? &HumanActionConfig::rush_left : &HumanActionConfig::rush_right;
        ActionInfo HumanAction::*rush_fallback =
            use_left ? &HumanAction::act_rush_left : &HumanAction::act_rush_right;

        const ActionConfig* action_config = config ? &(config->*rush_config) : nullptr;
        const ActionInfo* fallback_action = &(HA.*rush_fallback);
        apply_action(action_config, fallback_action);

        anim.rush_dir = use_left ? 1 : 0;
    } else {
        const ActionMapping* mapping = find_human_action_mapping(anim.current_action);
        if (!mapping) {
            mapping = find_human_action_mapping(ActorAction::STAND);
        }
        if (mapping) {
            apply_mapping(*mapping);
        } else {
            apply_action(nullptr, &HA.act_stand);
        }
    }

    anim.current_frame = anim.start_frame;
    anim.last_frame_tick = get_ticks_ms();
}

void ActorRenderer::calc_monster_frame(Actor& actor) {
    auto& anim = actor.anim;
    int dir = actor.data.direction;
    const MonsterActionConfig* config = nullptr;

    // 获取怪物动作配置
    const MonsterAction* ma = get_monster_action(actor.data.race, actor.data.appearance);
    if (!ma) {
        // 使用默认配置
                ma = &MA14;  // 默认骷髅类
    }
    if (use_config_system_) {
        const int monster_id = static_cast<int>(actor.data.race);
        config = config_loader_.get_monster_config(monster_id);
        if (!config) {
            static std::array<bool, 256> logged_missing{};
            if (monster_id >= 0 && monster_id < static_cast<int>(logged_missing.size()) &&
                !logged_missing[static_cast<size_t>(monster_id)]) {
                std::cerr << "[ActorRenderer] Missing monster config (id="
                          << monster_id << "), using hardcoded values\n";
                logged_missing[static_cast<size_t>(monster_id)] = true;
            }
        }
    }

    if (dir < 0 || dir > 7) {
        dir = 0;
        actor.data.direction = 0;
    }

    anim.def_frame_count = config ? config->stand.frame : ma->act_stand.frame;

    switch (anim.current_action) {
        case ActorAction::STAND:
            if (config) {
                anim.start_frame = config->stand.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->stand.frame - 1;
                anim.frame_time = config->stand.ftime;
            } else {
                anim.start_frame = ma->act_stand.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_stand.frame - 1;
                anim.frame_time = ma->act_stand.ftime;
            }
            break;

        case ActorAction::WALK:
            if (config) {
                anim.start_frame = config->walk.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->walk.frame - 1;
                anim.frame_time = config->walk.ftime;
            } else {
                anim.start_frame = ma->act_walk.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_walk.frame - 1;
                anim.frame_time = ma->act_walk.ftime;
            }
            break;

        case ActorAction::HIT:
            if (config) {
                anim.start_frame = config->attack.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->attack.frame - 1;
                anim.frame_time = config->attack.ftime;
            } else {
                anim.start_frame = ma->act_attack.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_attack.frame - 1;
                anim.frame_time = ma->act_attack.ftime;
            }
            break;

        case ActorAction::STRUCK:
            if (config) {
                anim.start_frame = config->struck.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->struck.frame - 1;
                anim.frame_time = config->struck.ftime;
            } else {
                anim.start_frame = ma->act_struck.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_struck.frame - 1;
                anim.frame_time = ma->act_struck.ftime;
            }
            break;

        case ActorAction::DIE:
            if (config) {
                anim.start_frame = config->die.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->die.frame - 1;
                anim.frame_time = config->die.ftime;
            } else {
                anim.start_frame = ma->act_die.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_die.frame - 1;
                anim.frame_time = ma->act_die.ftime;
            }
            break;

        case ActorAction::DEATH:
            if (config) {
                anim.start_frame = config->death.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->death.frame - 1;
                anim.frame_time = config->death.ftime;
            } else {
                anim.start_frame = ma->act_death.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_death.frame - 1;
                anim.frame_time = ma->act_death.ftime;
            }
            anim.lock_end_frame = true;
            break;

        default:
            if (config) {
                anim.start_frame = config->stand.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + config->stand.frame - 1;
                anim.frame_time = config->stand.ftime;
            } else {
                anim.start_frame = ma->act_stand.get_frame_index(dir, 0);
                anim.end_frame = anim.start_frame + ma->act_stand.frame - 1;
                anim.frame_time = ma->act_stand.ftime;
            }
            break;
    }

    anim.current_frame = anim.start_frame;
    anim.last_frame_tick = get_ticks_ms();
}

int ActorRenderer::get_default_frame(const Actor& actor, bool war_mode) {
    int dir = actor.data.direction;
    if (dir < 0 || dir > 7) dir = 0;

    if (actor.data.race == ActorRace::HUMAN) {
        // 人类角色
        if (actor.data.is_dead) {
            // 死亡最后一帧
            return HA.act_die.get_frame_index(dir, HA.act_die.frame - 1);
        } else if (war_mode) {
                        // 战斗姿态
            return HA.act_warmode.get_frame_index(dir, 0);
        } else {
            // 普通站立
            int cf = actor.anim.def_frame;
            if (cf < 0 || cf >= HA.act_stand.frame) cf = 0;
            return HA.act_stand.get_frame_index(dir, cf);
        }
    } else {
        // 怪物/NPC
        const MonsterAction* ma = get_monster_action(actor.data.race, actor.data.appearance);
        if (!ma) return 0;

        if (actor.data.is_dead) {
            if (actor.data.is_skeleton) {
                return ma->act_death.start;
            }
            return ma->act_die.get_frame_index(dir, ma->act_die.frame - 1);
        } else {
            int cf = actor.anim.def_frame;
            if (cf < 0 || cf >= ma->act_stand.frame) cf = 0;
            return ma->act_stand.get_frame_index(dir, cf);
        }
    }
}

void ActorRenderer::calc_shift(Actor& actor, int direction, int step,
                               int current_tick, int max_tick) {
    if (max_tick <= 0) {
        actor.anim.shift_x = 0;
        actor.anim.shift_y = 0;
        return;
    }

    int cur = current_tick;
    int max = max_tick;
    if (cur > max) cur = max;

    // 每步移动的像素距离
    const int unx = TILE_W * step;
    const int uny = TILE_H * step;

    // 计算剩余移动比例 (1.0 -> 0.0)
    // 在动画开始时 cur=1, 剩余比例接近 1.0，需要显示完整偏移
    // 在动画结束时 cur=max, 剩余比例≈0.0，偏移归零
    const double remaining_ratio = static_cast<double>(max - cur) / max;

    // 根据方向计算 X/Y 偏移
    // 偏移从满值逐渐减少到0，实现平滑过渡
    switch (direction) {
        case DIR_UP:
            actor.anim.shift_x = 0;
            actor.anim.shift_y = static_cast<int>(std::lround(uny * remaining_ratio));
            break;
        case DIR_UP_RIGHT:
            actor.anim.shift_x = -static_cast<int>(std::lround(unx * remaining_ratio));
            actor.anim.shift_y = static_cast<int>(std::lround(uny * remaining_ratio));
            break;
        case DIR_RIGHT:
            actor.anim.shift_x = -static_cast<int>(std::lround(unx * remaining_ratio));
            actor.anim.shift_y = 0;
            break;
        case DIR_DOWN_RIGHT:
            actor.anim.shift_x = -static_cast<int>(std::lround(unx * remaining_ratio));
            actor.anim.shift_y = -static_cast<int>(std::lround(uny * remaining_ratio));
            break;
        case DIR_DOWN:
            actor.anim.shift_x = 0;
            actor.anim.shift_y = -static_cast<int>(std::lround(uny * remaining_ratio));
            break;
        case DIR_DOWN_LEFT:
            actor.anim.shift_x = static_cast<int>(std::lround(unx * remaining_ratio));
            actor.anim.shift_y = -static_cast<int>(std::lround(uny * remaining_ratio));
            break;
        case DIR_LEFT:
            actor.anim.shift_x = static_cast<int>(std::lround(unx * remaining_ratio));
            actor.anim.shift_y = 0;
            break;
        case DIR_UP_LEFT:
            actor.anim.shift_x = static_cast<int>(std::lround(unx * remaining_ratio));
            actor.anim.shift_y = static_cast<int>(std::lround(uny * remaining_ratio));
            break;
        default:
            actor.anim.shift_x = 0;
            actor.anim.shift_y = 0;
            break;
    }
}

// =============================================================================
// 动画更新 (Animation Update)
// =============================================================================

void ActorRenderer::update(Actor& actor, uint32_t delta_ms) {
    auto& anim = actor.anim;
    uint64_t current_tick = get_ticks_ms();

    // 动作或方向变化时重新计算帧区间
    if (anim.current_action != anim.last_action ||
        actor.data.direction != anim.last_direction) {
        anim.last_action = anim.current_action;
        anim.last_direction = actor.data.direction;
        calc_actor_frame(actor);
    }

    // 更新通用动画计数
    if (current_tick - anim.gen_ani_time > 120ULL) {
        anim.gen_ani_count++;
        if (anim.gen_ani_count > 100000) anim.gen_ani_count = 0;
        anim.gen_ani_time = current_tick;
    }

    // 更新武器特效
    if (anim.weapon_effect) {
        if (current_tick - anim.wp_effect_time > 120ULL) {
            anim.wp_effect_time = current_tick;
            anim.cur_wp_effect++;
            if (anim.cur_wp_effect >= MAX_WP_EFFECT_FRAME) {
                anim.weapon_effect = false;
            }
        }
    }

    bool is_move_action = (anim.current_action == ActorAction::WALK ||
                           anim.current_action == ActorAction::RUN ||
                           anim.current_action == ActorAction::RUSH ||
                           anim.current_action == ActorAction::BACKSTEP);

    if (actor.data.war_mode && anim.war_mode_time > 0 &&
        (anim.current_action == ActorAction::NONE || anim.current_action == ActorAction::STAND)) {
        if (current_tick - anim.war_mode_time > 4000ULL) {
            actor.data.war_mode = false;
        }
    }

    // 移动动作：推进帧并计算平滑位移
    if (is_move_action) {
        const int max_step = anim.end_frame - anim.start_frame + 1;
        if (max_step <= 0) {
            anim.shift_x = 0;
            anim.shift_y = 0;
            return;
        }

        if (anim.current_action == ActorAction::BACKSTEP) {
            if (anim.current_frame < anim.start_frame || anim.current_frame > anim.end_frame) {
                anim.current_frame = anim.end_frame;
            }
        } else {
            if (anim.current_frame < anim.start_frame || anim.current_frame > anim.end_frame) {
                anim.current_frame = anim.start_frame;
            }
        }

        if (current_tick - anim.last_frame_tick > static_cast<uint64_t>(anim.frame_time)) {
            if (anim.current_action == ActorAction::BACKSTEP) {
                if (anim.current_frame > anim.start_frame) {
                    anim.current_frame--;
                } else {
                    anim.current_action = ActorAction::NONE;
                    anim.lock_end_frame = true;
                    anim.shift_x = 0;
                    anim.shift_y = 0;
                    return;
                }
            } else {
                if (anim.current_frame < anim.end_frame) {
                    anim.current_frame++;
                } else {
                    anim.current_action = ActorAction::NONE;
                    anim.lock_end_frame = true;
                    anim.shift_x = 0;
                    anim.shift_y = 0;
                    return;
                }
            }
            anim.last_frame_tick = current_tick;
        }

        const int cur_step = (anim.current_action == ActorAction::BACKSTEP)
            ? (anim.end_frame - anim.current_frame + 1)
            : (anim.current_frame - anim.start_frame + 1);
        const int move_step = (anim.current_action == ActorAction::RUN) ? 2 : 1;
        const int move_dir = (anim.current_action == ActorAction::BACKSTEP)
            ? get_back_direction(actor.data.direction)
            : actor.data.direction;
        calc_shift(actor, move_dir, move_step, cur_step, max_step);
        return;
    }

    // 非动作状态，更新默认帧
    if (anim.current_action == ActorAction::NONE ||
        anim.current_action == ActorAction::STAND) {
        if (current_tick - anim.def_frame_time > 500ULL) {
            anim.def_frame++;
            if (anim.def_frame >= anim.def_frame_count) {
                anim.def_frame = 0;
            }
            anim.def_frame_time = current_tick;
        }
        anim.current_frame = get_default_frame(actor, actor.data.war_mode);
        return;
    }

    // 更新动作帧
    if (current_tick - anim.last_frame_tick > static_cast<uint64_t>(anim.frame_time)) {
        if (anim.current_frame < anim.end_frame) {
            anim.current_frame++;
            anim.last_frame_tick = current_tick;

            // 更新施法特效帧
            if (anim.use_magic) {
                anim.cur_eff_frame++;
            }
        } else {
            // 动作完成
            if (!anim.lock_end_frame) {
                anim.current_action = ActorAction::NONE;
                anim.use_magic = false;
                anim.hit_effect = false;
            }
        }
    }
}

void ActorRenderer::set_action(Actor& actor, ActorAction action) {
    actor.anim.current_action = action;
    actor.anim.use_magic = false;
    actor.anim.hit_effect = false;
    actor.anim.hit_effect_number = 0;
    actor.anim.hide_weapon = false;
    actor.anim.lock_end_frame = false;
    calc_actor_frame(actor);
    actor.anim.last_action = actor.anim.current_action;
    actor.anim.last_direction = actor.data.direction;
}

bool ActorRenderer::is_action_finished(const Actor& actor) const {
    const auto& anim = actor.anim;
    return (anim.current_action == ActorAction::NONE ||
            anim.current_action == ActorAction::STAND ||
            (anim.current_frame >= anim.end_frame && !anim.lock_end_frame));
}

// =============================================================================
// 渲染实现 (Rendering)
// =============================================================================

void ActorRenderer::draw_actor(Actor& actor, const Camera& camera, bool is_focused) {
    if (!actor.data.is_visible) return;

    int screen_x, screen_y;
    calc_screen_pos(actor, camera, screen_x, screen_y);

    // 计算是否需要混合绘制(隐身状态)
    bool blend = (actor.data.state & STATE_INVISIBLE) != 0;

    // 获取颜色特效
    ColorEffect color_effect = get_color_effect_from_state(actor.data.state, is_focused);

    // 绘制阴影
    draw_shadow(actor, screen_x, screen_y);

    // 根据种族选择绘制方法
    if (actor.data.race == ActorRace::HUMAN) {
        draw_human(actor, screen_x, screen_y, blend, color_effect);
    } else {
        draw_monster(actor, screen_x, screen_y, blend, color_effect);
    }

    // 绘制名称
    if (!actor.data.name.empty()) {
        draw_name(actor, screen_x, screen_y);
    }
}

void ActorRenderer::draw_human(Actor& actor, int screen_x, int screen_y,
                               bool blend, ColorEffect color_effect) {
    auto& anim = actor.anim;
    int current_frame = anim.current_frame;
    int shift_x = anim.shift_x;
    int shift_y = anim.shift_y;

    // 加载表面资源
    load_actor_surfaces(actor);

    // 获取武器绘制顺序
    // 0 = 武器在前(先绘制武器后绘制身体)
    // 1 = 武器在后(先绘制身体后绘制武器)
    int wpord = 0;
    const int sex_index = (actor.data.sex <= 1)
        ? static_cast<int>(actor.data.sex)
        : 0;  // 确保性别索引有效
    const int max_sex_index = static_cast<int>(std::size(WEAPON_ORDER));
    const int max_frame_index = std::min(HUMAN_FRAME,
                                         static_cast<int>(std::size(WEAPON_ORDER[0])));
    if (current_frame >= 0 && current_frame < max_frame_index &&
        sex_index >= 0 && sex_index < max_sex_index) {
        wpord = WEAPON_ORDER[sex_index][current_frame];
    } else {
        wpord = 0;
    }

    // 绘制位置（底部中心锚点）
    int dx = screen_x + shift_x;
    int dy = screen_y + shift_y;

    auto draw_layer = [&](SDL_Texture* texture, int offset_x, int offset_y,
                          bool use_blend, ColorEffect effect) {
        if (!texture) return;
        int w = 0, h = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        const int draw_x = dx - w / 2 + offset_x;
        const int draw_y = dy - h + offset_y;
        draw_effect_surface(texture, draw_x, draw_y, use_blend, effect);
    };

    // 获取纹理
    int body_ox = 0, body_oy = 0;
    int hair_ox = 0, hair_oy = 0;
    int weapon_ox = 0, weapon_oy = 0;

    SDL_Texture* body_tex = get_frame_texture("Hum", actor.body_offset + current_frame,
                                               body_ox, body_oy);
    SDL_Texture* hair_tex = nullptr;
    if (actor.hair_offset >= 0) {
        hair_tex = get_frame_texture("Hair", actor.hair_offset + current_frame,
                                      hair_ox, hair_oy);
    }
    SDL_Texture* weapon_tex = nullptr;
    if (actor.data.weapon >= 2 && !anim.hide_weapon) {
        weapon_tex = get_frame_texture("Weapon", actor.weapon_offset + current_frame,
                                        weapon_ox, weapon_oy);
    }

    // 按顺序绘制: 武器(前) -> 身体 -> 头发 -> 武器(后) -> 特效

    // 1. 先绘制武器(wpord=0时，隐身状态也绘制但使用blend)
    if (wpord == 0 && weapon_tex) {
        draw_layer(weapon_tex, weapon_ox, weapon_oy, blend, ColorEffect::NONE);
    }

    // 2. 绘制身体
    if (body_tex) {
        draw_layer(body_tex, body_ox, body_oy, blend, color_effect);
    }

    // 3. 绘制头发
    if (hair_tex) {
        draw_layer(hair_tex, hair_ox, hair_oy, blend, color_effect);
    }

    // 4. 后绘制武器(wpord=1时)
    if (wpord == 1 && weapon_tex) {
        draw_layer(weapon_tex, weapon_ox, weapon_oy, blend, ColorEffect::NONE);
    }

    // 5. 绘制防御罩特效
    if (actor.data.state & STATE_SHIELD) {
        draw_shield_effect(actor, dx, dy);
    }

    // 6. 绘制魔法施放特效
    if (anim.use_magic) {
        draw_spell_effect(actor, dx, dy);
    }

    // 7. 绘制攻击特效
    if (anim.hit_effect && anim.hit_effect_number > 0) {
        draw_hit_effect(actor, dx, dy);
    }

    // 8. 绘制武器光效
    if (anim.weapon_effect) {
        draw_weapon_effect(actor, dx, dy);
    }
}

void ActorRenderer::draw_monster(Actor& actor, int screen_x, int screen_y,
                                 bool blend, ColorEffect color_effect) {
    auto& anim = actor.anim;
    int current_frame = anim.current_frame;
    int shift_x = anim.shift_x;
    int shift_y = anim.shift_y;

    // 确保资源已加载并计算偏移
    load_actor_surfaces(actor);

    // 获取怪物图像
    std::string archive_name = get_monster_archive_name(static_cast<int>(actor.data.race));
    int body_ox = 0, body_oy = 0;
    SDL_Texture* body_tex = get_frame_texture(archive_name,
                                               actor.body_offset + current_frame,
                                               body_ox, body_oy);

    if (body_tex) {
        int dx = screen_x + shift_x;
        int dy = screen_y + shift_y;
        int w = 0, h = 0;
        SDL_QueryTexture(body_tex, nullptr, nullptr, &w, &h);
        const int draw_x = dx - w / 2 + body_ox;
        const int draw_y = dy - h + body_oy;
        draw_effect_surface(body_tex, draw_x, draw_y, blend, color_effect);
    }

    // 绘制魔法特效
    if (anim.use_magic) {
        draw_spell_effect(actor, screen_x + shift_x, screen_y + shift_y);
    }
}

void ActorRenderer::draw_shadow(const Actor& actor, int screen_x, int screen_y) {
    // 简单阴影实现: 绘制半透明圆形
    // 后续可扩展为使用阴影纹理

    SDL_Renderer* sdl_renderer = renderer_.get_renderer();
    if (!sdl_renderer) return;

    // 阴影偏移和大小
    int shadow_offset_y = 5;
    int shadow_width = 40;
    int shadow_height = 16;

    // 设置半透明黑色
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 80);

    // 绘制简单阴影(简化为矩形)
    const int anchor_x = screen_x + actor.anim.shift_x;
    const int anchor_y = screen_y + actor.anim.shift_y;
    SDL_Rect shadow_rect = {
        anchor_x - shadow_width / 2,
        anchor_y + shadow_offset_y - shadow_height / 2,
        shadow_width,
        shadow_height
    };
    SDL_RenderFillRect(sdl_renderer, &shadow_rect);
}

void ActorRenderer::draw_name(const Actor& actor, int screen_x, int screen_y) {
    // 名称绘制需要字体支持，这里暂时跳过
    // 可以使用 SDL_ttf 或位图字体
}

// =============================================================================
// 批量渲染 (Batch Rendering)
// =============================================================================

void ActorRenderer::sort_actors_by_z_order(std::vector<Actor*>& actors) {
    // Z-order 排序: 按(render_y - down_draw_level) 升序
    // Y 值小的先绘制(在后面)，Y 值大的后绘制(在前面)
    std::stable_sort(actors.begin(), actors.end(),
        [](const Actor* a, const Actor* b) {
            int a_z = a->render_y - a->down_draw_level * TILE_H;
            int b_z = b->render_y - b->down_draw_level * TILE_H;
            return a_z < b_z;
        });
}

void ActorRenderer::draw_actors(std::vector<Actor*>& actors, const Camera& camera,
                                 Actor* focus_actor) {
    // 预先计算屏幕坐标用于Z-order排序
    for (Actor* actor : actors) {
        if (actor) {
            calc_screen_pos(*actor, camera, actor->render_x, actor->render_y);
        }
    }

    // 先排序
    sort_actors_by_z_order(actors);

    // 按顺序绘制
    for (Actor* actor : actors) {
        if (actor) {
            bool is_focused = (actor == focus_actor);
            draw_actor(*actor, camera, is_focused);
        }
    }
}

// =============================================================================
// 特效绘制 (Effects)
// =============================================================================

void ActorRenderer::draw_spell_effect(const Actor& actor, int screen_x, int screen_y) {
    // 施法特效绘制 (参考magiceff.pas)
    const auto& anim = actor.anim;
    if (!anim.use_magic) return;
    if (anim.magic_effect_number < 0 ||
        anim.magic_effect_number >= static_cast<int>(EFFECT_BASE.size())) {
        return;
    }

    const int frame_limit = get_magic_frame_limit(anim.magic_type, anim.spell_frame);
    if (frame_limit <= 0 || anim.cur_eff_frame >= frame_limit) return;

    int dir = actor.data.direction;
    if (dir < 0 || dir > 7) dir = 0;
    const int dir16 = dir * 2;

    const int effect_base = EFFECT_BASE[anim.magic_effect_number];
    int effect_idx = effect_base + anim.cur_eff_frame;

    switch (anim.magic_type) {
        case MagicType::READY:
            effect_idx = effect_base + anim.cur_eff_frame;
            break;
        case MagicType::FLY:
        case MagicType::FLY_AXE:
        case MagicType::FLY_ARROW:
        case MagicType::FIRE_BALL:
        case MagicType::EXPLO_BUJAUK:
        case MagicType::BUJAUK_GROUND_EFFECT:
            effect_idx = effect_base + FLYBASE + dir16 * 10 + anim.cur_eff_frame;
            break;
        case MagicType::LIGHTING_THUNDER:
            effect_idx = effect_base + dir16 * 10 + anim.cur_eff_frame;
            break;
        case MagicType::THUNDER:
        case MagicType::FIRE_THUNDER:
        case MagicType::FIRE_WIND:
        case MagicType::FIRE_GUN:
        case MagicType::GROUND_EFFECT:
        case MagicType::KYUL_KAI:
            effect_idx = effect_base + anim.cur_eff_frame;
            break;
        case MagicType::EXPLOSION:
        default:
            effect_idx = effect_base + EXPLOSIONBASE + anim.cur_eff_frame;
            break;
    }

    const char* archive_name = get_magic_archive_for_effect(anim.magic_effect_number);
    int ox = 0, oy = 0;
    SDL_Texture* effect_tex = get_frame_texture(archive_name, effect_idx, ox, oy);
    if (effect_tex) {
        draw_blend(effect_tex, screen_x + ox - TILE_W / 2, screen_y + oy - TILE_H / 2);
    }
}

void ActorRenderer::draw_hit_effect(const Actor& actor, int screen_x, int screen_y) {
    // 攻击特效绘制 (参考magiceff.pas HitEffectBase数组)
    const auto& anim = actor.anim;
    if (!anim.hit_effect || anim.hit_effect_number <= 0) return;

    int dir = actor.data.direction;
    if (dir < 0 || dir > 7) dir = 0;
    
    int frame_offset = anim.current_frame - anim.start_frame;
    if (frame_offset < 0) frame_offset = 0;

    int effect_num = anim.hit_effect_number;
    if (effect_num < 0 || effect_num >= static_cast<int>(HIT_EFFECT_BASE.size())) return;
    int base_idx = HIT_EFFECT_BASE[effect_num];
    
    // 特效索引计算
    int effect_idx;
    if (effect_num == 7) {
        effect_idx = base_idx + dir * 20 + frame_offset;
    } else {
        effect_idx = base_idx + dir * 10 + frame_offset;
    }

    int ox = 0, oy = 0;
    const char* archive_name = get_magic_archive_for_hit(effect_num);
    SDL_Texture* effect_tex = get_frame_texture(archive_name, effect_idx, ox, oy);
    if (effect_tex) {
        draw_blend(effect_tex, screen_x + ox - TILE_W / 2, screen_y + oy - TILE_H / 2);
    }
}

void ActorRenderer::draw_weapon_effect(const Actor& actor, int screen_x, int screen_y) {
    // 武器光效绘制 (参考magiceff.pas)
    const auto& anim = actor.anim;
    if (!anim.weapon_effect) return;

    int dir = actor.data.direction;
    if (dir < 0 || dir > 7) dir = 0;
    
    // 武器光效图像索引计算: WP_EFFECT_BASE = 3750, 每个方向10帧
    int effect_idx = WP_EFFECT_BASE + dir * 10 + anim.cur_wp_effect;

    int ox = 0, oy = 0;
    SDL_Texture* effect_tex = get_frame_texture("Magic", effect_idx, ox, oy);
    if (effect_tex) {
        draw_blend(effect_tex, screen_x + ox - TILE_W / 2, screen_y + oy - TILE_H / 2);
    }
}

void ActorRenderer::draw_shield_effect(const Actor& actor, int screen_x, int screen_y) {
    // 防御罩特效绘制 (参考magiceff.pas)
    const auto& anim = actor.anim;
    int idx;

    // 根据受击状态选择不同的护盾图像
    if (anim.current_action == ActorAction::STRUCK && anim.gen_ani_count % 10 < 3) {
        idx = MAG_BUBBLE_STRUCK_BASE + (anim.gen_ani_count % 3);
    } else {
        idx = MAG_BUBBLE_BASE + (anim.gen_ani_count % 3);
    }

    int ox = 0, oy = 0;
    SDL_Texture* shield_tex = get_frame_texture("Magic", idx, ox, oy);
    if (shield_tex) {
        draw_blend(shield_tex, screen_x + ox - TILE_W / 2, screen_y + oy - TILE_H / 2);
    }
}

// =============================================================================
// 资源加载 (Resource Loading)
// =============================================================================

void ActorRenderer::load_actor_surfaces(Actor& actor) {
    if (actor.data.race == ActorRace::HUMAN) {
        try_load_archive("Hum");
        try_load_archive("Hair");
        try_load_archive("Weapon");

        actor.body_offset = HUMAN_FRAME * actor.data.dress;

        int hair = actor.data.hair;
        if (hair >= MAX_HAIR_STYLES) {
            hair = MAX_HAIR_STYLES - 1;
        }
        hair *= 2;
        if (hair > 1) {
            actor.hair_offset = HUMAN_FRAME * (hair + actor.data.sex);
        } else {
            actor.hair_offset = -1;
        }

        actor.weapon_offset = HUMAN_FRAME * actor.data.weapon;
        return;
    }

    const int appearance = (actor.data.appearance > 0)
        ? actor.data.appearance
        : static_cast<int>(actor.data.race);

    if (actor.data.race == ActorRace::NPC) {
        // NPC使用Npc.wil资源
        try_load_archive("Npc");
        if (appearance <= 22) {
            actor.body_offset = MERCHANT_FRAME * appearance;
        } else if (appearance == 23) {
            actor.body_offset = 1380;
        } else {
            actor.body_offset = 1470 + MERCHANT_FRAME * (appearance - 24);
        }
    } else {
        actor.body_offset = get_monster_offset(actor.data.race, actor.data.appearance);
        const std::string archive_name = get_monster_archive_name(appearance);
        try_load_archive(archive_name);
    }

    actor.hair_offset = -1;
    actor.weapon_offset = 0;
}

void ActorRenderer::clear_texture_cache() {
    texture_cache_.clear();
    offset_cache_.clear();
}

bool ActorRenderer::load_archives() {
    bool success = true;

    if (data_path_.empty()) {
        data_path_ = mir2::core::find_data_path("Hum.wil");
        if (data_path_.empty()) {
            data_path_ = mir2::core::find_data_path("hum.wil");
        }
    }

    if (data_path_.empty()) {
        std::cerr << "[ActorRenderer] Could not find Data directory for actor archives\n";
        return false;
    }

    success &= try_load_archive("Hum");
    success &= try_load_archive("Hair");
    success &= try_load_archive("Weapon");
    success &= try_load_archive("Magic");
    success &= try_load_archive("Magic2");

    resources_loaded_ = success;
    return success;
}

void ActorRenderer::apply_color_effect(SDL_Texture* texture, ColorEffect effect) {
    if (!texture) return;

    uint8_t r = 255, g = 255, b = 255;
    switch (effect) {
        case ColorEffect::NONE:
            r = g = b = 255;
            break;
        case ColorEffect::BRIGHT:
            r = g = b = 255;
            break;
        case ColorEffect::GREEN:
            r = 120; g = 255; b = 120;
            break;
        case ColorEffect::RED:
            r = 255; g = 120; b = 120;
            break;
        case ColorEffect::BLUE:
            r = 140; g = 140; b = 255;
            break;
        case ColorEffect::YELLOW:
            r = 255; g = 255; b = 120;
            break;
        case ColorEffect::FUCHSIA:
            r = 255; g = 120; b = 255;
            break;
        case ColorEffect::GRAYSCALE:
            r = g = b = 160;
            break;
        default:
            r = g = b = 255;
            break;
    }

    SDL_SetTextureColorMod(texture, r, g, b);
}

void ActorRenderer::draw_effect_surface(SDL_Texture* texture, int x, int y,
                                        bool blend, ColorEffect effect) {
    if (!texture) return;
    SDL_Renderer* sdl_renderer = renderer_.get_renderer();
    if (!sdl_renderer) return;

    uint8_t prev_r = 255, prev_g = 255, prev_b = 255, prev_a = 255;
    SDL_BlendMode prev_blend = SDL_BLENDMODE_NONE;
    SDL_GetTextureColorMod(texture, &prev_r, &prev_g, &prev_b);
    SDL_GetTextureAlphaMod(texture, &prev_a);
    SDL_GetTextureBlendMode(texture, &prev_blend);

    apply_color_effect(texture, effect);

    if (blend) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, 160);
    } else {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, 255);
    }

    int w = 0, h = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(sdl_renderer, texture, nullptr, &dst);

    SDL_SetTextureColorMod(texture, prev_r, prev_g, prev_b);
    SDL_SetTextureAlphaMod(texture, prev_a);
    SDL_SetTextureBlendMode(texture, prev_blend);
}

void ActorRenderer::draw_blend(SDL_Texture* texture, int x, int y, float alpha) {
    if (!texture) return;
    SDL_Renderer* sdl_renderer = renderer_.get_renderer();
    if (!sdl_renderer) return;

    SDL_BlendMode prev_blend = SDL_BLENDMODE_NONE;
    uint8_t prev_alpha = 255;
    SDL_GetTextureBlendMode(texture, &prev_blend);
    SDL_GetTextureAlphaMod(texture, &prev_alpha);

    float clamped = std::clamp(alpha, 0.0f, 1.0f);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, static_cast<uint8_t>(clamped * 255.0f));

    int w = 0, h = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(sdl_renderer, texture, nullptr, &dst);

    SDL_SetTextureAlphaMod(texture, prev_alpha);
    SDL_SetTextureBlendMode(texture, prev_blend);
}

SDL_Texture* ActorRenderer::get_frame_texture(const std::string& archive, int frame_index,
                                              int& out_offset_x, int& out_offset_y) {
    out_offset_x = 0;
    out_offset_y = 0;
    if (frame_index < 0) return nullptr;

    if (!resource_manager_.is_archive_loaded(archive)) {
        if (!try_load_archive(archive)) {
            return nullptr;
        }
    }

    const std::string cache_key = archive + ":" + std::to_string(frame_index);
    auto offset_it = offset_cache_.find(cache_key);
    if (offset_it != offset_cache_.end()) {
        out_offset_x = offset_it->second.offset_x;
        out_offset_y = offset_it->second.offset_y;
    }

    if (auto cached = texture_cache_.get(cache_key, true)) {
        if (offset_it != offset_cache_.end()) {
            return cached->get();
        }
    }

    auto sprite_opt = resource_manager_.get_sprite(archive, frame_index);
    if (!sprite_opt) {
        return nullptr;
    }

    out_offset_x = sprite_opt->offset_x;
    out_offset_y = sprite_opt->offset_y;
    offset_cache_[cache_key] = {out_offset_x, out_offset_y};

    auto texture = texture_cache_.get_or_create(cache_key, *sprite_opt, true);
    return texture ? texture->get() : nullptr;
}

void ActorRenderer::calc_screen_pos(const Actor& actor, const Camera& camera,
                                    int& out_x, int& out_y) {
    const float world_x = static_cast<float>(actor.data.map_x * TILE_W);
    const float world_y = static_cast<float>(actor.data.map_y * TILE_H);
    const float screen_x = (world_x - camera.x) * camera.zoom + camera.viewport_width * 0.5f;
    const float screen_y = (world_y - camera.y) * camera.zoom + camera.viewport_height * 0.5f;
    out_x = static_cast<int>(std::lround(screen_x + TILE_W * 0.5f));
    out_y = static_cast<int>(std::lround(screen_y + TILE_H));
}

bool ActorRenderer::try_load_archive(const std::string& archive_name) {
    if (archive_name.empty()) {
        return false;
    }

    auto loaded_it = loaded_archives_.find(archive_name);
    if (loaded_it != loaded_archives_.end()) {
        return loaded_it->second;
    }

    if (failed_archives_.find(archive_name) != failed_archives_.end()) {
        return false;
    }

    if (resource_manager_.is_archive_loaded(archive_name)) {
        loaded_archives_[archive_name] = true;
        return true;
    }

    if (data_path_.empty()) {
        std::cerr << "[ActorRenderer] Data path is empty, cannot load archive '"
                  << archive_name << "'\n";
        loaded_archives_[archive_name] = false;
        failed_archives_.insert(archive_name);
        return false;
    }

    auto load_with_retry = [&](const std::string& path) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (resource_manager_.load_archive(path)) {
                return true;
            }
        }
        return false;
    };

    std::string path = data_path_ + archive_name + ".wil";
    if (load_with_retry(path)) {
        loaded_archives_[archive_name] = true;
        return true;
    }

    std::string lower_name = archive_name;
    for (auto& ch : lower_name) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lower_name != archive_name) {
        path = data_path_ + lower_name + ".wil";
        if (load_with_retry(path)) {
            loaded_archives_[archive_name] = true;
            return true;
        }
    }

    std::cerr << "[ActorRenderer] Failed to load archive '" << archive_name
              << "' after retry, path base: " << data_path_ << "\n";
    loaded_archives_[archive_name] = false;
    failed_archives_.insert(archive_name);
    return false;
}

std::string ActorRenderer::get_monster_archive_name(int monster_id) const {
    if (monster_id == static_cast<int>(ActorRace::NPC)) {
        return "Npc";
    }
    const int group = monster_id / 10;
    switch (group) {
        case 0:  return "Mon1";
        case 1:  return "Mon2";
        case 2:  return "Mon3";
        case 3:  return "Mon4";
        case 4:  return "Mon5";
        case 5:  return "Mon6";
        case 6:  return "Mon7";
        case 7:  return "Mon8";
        case 8:  return "Mon9";
        case 9:  return "Mon10";
        case 10: return "Mon11";
        case 11: return "Mon12";
        case 12: return "Mon13";
        case 13: return "Mon14";
        case 14: return "Mon15";
        case 15: return "Mon16";
        case 16: return "Mon17";
        case 17: return "Mon18";
        case 90: return "Effect";
        default: return "Mon1";
    }
}

// =============================================================================
// 辅助函数 (Helper Functions)
// =============================================================================

int get_monster_offset(ActorRace race, int appearance) {
    int appr = appearance;
    if (appr <= 0) {
        appr = static_cast<int>(race);
    }

    const int nrace = appr / 10;
    const int npos = appr % 10;

    switch (nrace) {
        case 0:
            return npos * MON_FRAME;
        case 1:
            return npos * 230;
        case 2:
        case 3:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 14:
        case 15:
        case 16:
            return npos * EXPMON_FRAME;
        case 13:
            switch (npos) {
                case 1: return 360;
                case 2: return 440;
                case 3: return 550;
                default: return npos * EXPMON_FRAME;
            }
        case 4: {
            int offset = npos * EXPMON_FRAME;
            if (npos == 1) {
                offset = 600;
            }
            return offset;
        }
        case 5:
            return npos * ZOMBI_FRAME;
        case 6:
            return npos * SCULMON_FRAME;
        case 17:
            return npos * 350;
        case 18:
            switch (npos) {
                case 0: return 0;
                case 1: return 520;
                case 2: return 950;
                default: return npos * EXPMON_FRAME;
            }
        case 19:
            switch (npos) {
                case 0: return 0;
                case 1: return 370;
                case 2: return 810;
                case 3: return 1250;
                case 4: return 1630;
                case 5: return 2010;
                case 6: return 2390;
                default: return npos * EXPMON_FRAME;
            }
        case 20:
            switch (npos) {
                case 0: return 0;
                case 1: return 360;
                case 2: return 720;
                case 3: return 1080;
                case 4: return 1440;
                case 5: return 1800;
                case 6: return 2350;
                case 7: return 3060;
                default: return npos * EXPMON_FRAME;
            }
        case 90:
            switch (npos) {
                case 0: return 80;
                case 1: return 168;
                case 2: return 184;
                case 3: return 200;
                default: return 0;
            }
        default:
            return npos * MON_FRAME;
    }
}

}  // namespace mir2::render
