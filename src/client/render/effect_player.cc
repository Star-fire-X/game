// =============================================================================
// Legend2 特效播放器 (Effect Player)
// =============================================================================

#include "render/effect_player.h"

#include "core/path_utils.h"

#include <algorithm>
#include <cctype>
#include <iostream>

namespace mir2::render {
namespace {

constexpr float kDefaultFrameDurationMs = 100.0f;
constexpr uint32_t kDefaultDurationMs = 500;
constexpr float kDamageFloatSpeed = 30.0f;
constexpr int kDamageDigitWidth = 6;
constexpr int kDamageDigitHeight = 10;
constexpr int kDamageDigitSpacing = 2;

struct EffectRef {
    std::string archive;
    int base_index = 0;
};

bool parse_int(const std::string& text, int* out_value) {
    if (!out_value) {
        return false;
    }
    if (text.empty()) {
        return false;
    }

    size_t idx = 0;
    int value = 0;
    try {
        value = std::stoi(text, &idx, 10);
    } catch (...) {
        return false;
    }

    if (idx != text.size()) {
        return false;
    }

    if (value < 0) {
        return false;
    }

    *out_value = value;
    return true;
}

std::string trim_copy(const std::string& input) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

    size_t start = 0;
    while (start < input.size() && is_space(static_cast<unsigned char>(input[start]))) {
        ++start;
    }

    size_t end = input.size();
    while (end > start && is_space(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(start, end - start);
}

std::string strip_extension(const std::string& name) {
    const size_t dot_pos = name.find_last_of('.');
    if (dot_pos == std::string::npos) {
        return name;
    }

    const std::string ext = name.substr(dot_pos + 1);
    if (ext == "wil" || ext == "WIL" || ext == "wix" || ext == "WIX") {
        return name.substr(0, dot_pos);
    }

    return name;
}

bool parse_effect_id(const std::string& effect_id, EffectRef* out_ref) {
    if (!out_ref) {
        return false;
    }

    out_ref->archive.clear();
    out_ref->base_index = 0;

    const std::string trimmed = trim_copy(effect_id);
    if (trimmed.empty()) {
        return false;
    }

    const size_t delim_pos = trimmed.find_first_of(":/#");
    if (delim_pos == std::string::npos) {
        int base_index = 0;
        if (parse_int(trimmed, &base_index)) {
            out_ref->archive = "Effect";
            out_ref->base_index = base_index;
            return true;
        }

        out_ref->archive = strip_extension(trimmed);
        out_ref->base_index = 0;
        return !out_ref->archive.empty();
    }

    std::string archive = strip_extension(trimmed.substr(0, delim_pos));
    std::string rest = trimmed.substr(delim_pos + 1);
    const size_t rest_delim = rest.find_first_of(":/#");
    if (rest_delim != std::string::npos) {
        rest = rest.substr(0, rest_delim);
    }

    int base_index = 0;
    if (!parse_int(rest, &base_index)) {
        return false;
    }

    if (archive.empty()) {
        archive = "Effect";
    }

    out_ref->archive = archive;
    out_ref->base_index = base_index;
    return true;
}

bool ensure_archive_loaded(const std::string& archive,
                           mir2::client::ResourceManager& resource_manager) {
    if (archive.empty()) {
        return false;
    }

    if (resource_manager.is_archive_loaded(archive)) {
        return true;
    }

    const std::string data_path = mir2::core::find_data_path(archive + ".wil");
    if (data_path.empty()) {
        return false;
    }

    const std::string path = data_path + archive + ".wil";
    if (resource_manager.load_wil_archive(path)) {
        return true;
    }

    std::string lower = archive;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == archive) {
        return false;
    }

    return resource_manager.load_wil_archive(data_path + lower + ".wil");
}

void draw_number_placeholder(IRenderer& renderer, int value, int x, int y, const Color& color) {
    std::string text = std::to_string(value);
    if (text.empty()) {
        return;
    }

    const int total_width = static_cast<int>(text.size()) *
                            (kDamageDigitWidth + kDamageDigitSpacing) -
                            kDamageDigitSpacing;
    int start_x = x - total_width / 2;
    int top_y = y - kDamageDigitHeight / 2;

    for (size_t i = 0; i < text.size(); ++i) {
        Rect digit_rect = {
            start_x + static_cast<int>(i) * (kDamageDigitWidth + kDamageDigitSpacing),
            top_y,
            kDamageDigitWidth,
            kDamageDigitHeight
        };
        renderer.draw_rect(digit_rect, color);
    }
}

} // namespace

float ProjectileEffect::get_progress(int64_t now_ms) const {
    if (!active) {
        return 1.0f;
    }
    if (duration_ms <= 0) {
        return 1.0f;
    }

    const int64_t elapsed = now_ms - start_time_ms;
    if (elapsed <= 0) {
        return 0.0f;
    }
    if (elapsed >= duration_ms) {
        return 1.0f;
    }
    return static_cast<float>(elapsed) / static_cast<float>(duration_ms);
}

void ProjectileEffect::get_current_position(int64_t now_ms, int& x, int& y) const {
    float progress = get_progress(now_ms);
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }

    x = static_cast<int>(start_x + (end_x - start_x) * progress);
    y = static_cast<int>(start_y + (end_y - start_y) * progress);
}

EffectPlayer::EffectPlayer(SDLRenderer& renderer, mir2::client::ResourceManager& resource_manager)
    : renderer_(renderer)
    , resource_manager_(resource_manager) {
}

void EffectPlayer::play_effect(const std::string& effect_id, int x, int y,
                               uint8_t direction, uint32_t duration_ms) {
    if (effect_id.empty()) {
        return;
    }

    ActiveEffect effect;
    effect.effect_id = effect_id;
    effect.x = x;
    effect.y = y;
    effect.direction = direction;
    effect.elapsed_ms = 0.0f;
    effect.duration_ms = duration_ms > 0 ? static_cast<float>(duration_ms)
                                         : static_cast<float>(kDefaultDurationMs);
    effect.current_frame = 0;
    effect.finished = false;

    while (active_effects_.size() >= kMaxActiveEffects) {
        active_effects_.erase(active_effects_.begin());
    }
    active_effects_.push_back(std::move(effect));
}

void EffectPlayer::play_skill_effect(uint64_t /*caster_id*/, uint64_t /*target_id*/,
                                     uint32_t /*skill_id*/, EffectPlayType /*type*/,
                                     const std::string& effect_id, int x, int y,
                                     uint32_t duration_ms) {
    play_effect(effect_id, x, y, 0, duration_ms);
}

void EffectPlayer::preload_skill_effects(const std::vector<uint32_t>& effect_ids) {
    for (const auto effect_id : effect_ids) {
        preloaded_effects_.insert(effect_id);
    }
}

void EffectPlayer::play_skill_effect(uint32_t effect_id, int x, int y) {
    if (preloaded_effects_.find(effect_id) == preloaded_effects_.end()) {
        std::cerr << "[EffectPlayer] Skill effect not preloaded: " << effect_id << std::endl;
    }
    play_effect(std::to_string(effect_id), x, y, 0, kDefaultDurationMs);
}

void EffectPlayer::play_projectile_effect(uint32_t effect_id, int start_x, int start_y,
                                          int end_x, int end_y, int64_t travel_ms) {
    ProjectileEffect projectile;
    projectile.effect_id = effect_id;
    projectile.start_x = start_x;
    projectile.start_y = start_y;
    projectile.end_x = end_x;
    projectile.end_y = end_y;
    projectile.start_time_ms = current_time_ms_;
    projectile.duration_ms = travel_ms > 0 ? travel_ms : 1;
    projectile.active = true;

    projectiles_.push_back(projectile);
}

void EffectPlayer::spawn_damage_number(int value, bool is_healing, int world_x, int world_y) {
    DamageNumber number;
    number.value = value;
    number.is_healing = is_healing;
    number.world_x = world_x;
    number.world_y = world_y;
    number.spawn_time_ms = current_time_ms_;
    number.alpha = 1.0f;
    number.offset_y = 0.0f;

    damage_numbers_.push_back(number);
}

void EffectPlayer::update_projectiles(int64_t now_ms) {
    if (projectiles_.empty()) {
        return;
    }

    for (auto& projectile : projectiles_) {
        if (!projectile.active) {
            continue;
        }
        if (projectile.get_progress(now_ms) >= 1.0f) {
            projectile.active = false;
        }
    }

    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
                                      [](const ProjectileEffect& projectile) {
                                          return !projectile.active;
                                      }),
                       projectiles_.end());
}

void EffectPlayer::update_damage_numbers(int64_t now_ms) {
    if (damage_numbers_.empty()) {
        return;
    }

    auto it = damage_numbers_.begin();
    while (it != damage_numbers_.end()) {
        int64_t elapsed = now_ms - it->spawn_time_ms;
        if (elapsed < 0) {
            elapsed = 0;
        }
        if (elapsed >= DamageNumber::LIFETIME_MS) {
            it = damage_numbers_.erase(it);
            continue;
        }

        const float progress = static_cast<float>(elapsed) /
                               static_cast<float>(DamageNumber::LIFETIME_MS);
        it->alpha = 1.0f - progress;
        it->offset_y = -kDamageFloatSpeed * (static_cast<float>(elapsed) / 1000.0f);
        ++it;
    }
}

void EffectPlayer::render_projectiles(IRenderer& renderer, const Camera& camera) {
    if (projectiles_.empty()) {
        return;
    }

    for (const auto& projectile : projectiles_) {
        if (!projectile.active) {
            continue;
        }

        int world_x = projectile.start_x;
        int world_y = projectile.start_y;
        projectile.get_current_position(current_time_ms_, world_x, world_y);

        const int frame = static_cast<int>(
            (current_time_ms_ - projectile.start_time_ms) / kDefaultFrameDurationMs);

        const std::string effect_id = std::to_string(projectile.effect_id);
        auto texture = get_effect_texture(effect_id, frame);
        if (!texture) {
            continue;
        }

        Position screen_pos = camera.world_to_screen({world_x, world_y});

        EffectRef ref;
        if (parse_effect_id(effect_id, &ref)) {
            const int frame_index = ref.base_index + frame;
            auto sprite_opt = resource_manager_.get_sprite(ref.archive, frame_index);
            if (sprite_opt) {
                renderer.draw_sprite(*texture, screen_pos.x, screen_pos.y,
                                     sprite_opt->offset_x, sprite_opt->offset_y);
                continue;
            }
        }

        renderer.draw_texture(*texture, screen_pos.x, screen_pos.y);
    }
}

void EffectPlayer::render_damage_numbers(IRenderer& renderer, const Camera& camera) {
    if (damage_numbers_.empty()) {
        return;
    }

    for (const auto& number : damage_numbers_) {
        Position screen_pos = camera.world_to_screen({number.world_x, number.world_y});
        const int x = screen_pos.x;
        const int y = screen_pos.y + static_cast<int>(number.offset_y);

        Color color = number.is_healing ? Color::green() : Color::red();
        float alpha = number.alpha;
        if (alpha < 0.0f) {
            alpha = 0.0f;
        } else if (alpha > 1.0f) {
            alpha = 1.0f;
        }
        color.a = static_cast<uint8_t>(alpha * 255.0f);

        draw_number_placeholder(renderer, number.value, x, y, color);
    }
}

void EffectPlayer::update(float delta_time) {
    const float delta_ms = std::max(0.0f, delta_time * 1000.0f);
    current_time_ms_ += static_cast<int64_t>(delta_ms);

    if (!active_effects_.empty()) {
        for (auto& effect : active_effects_) {
            if (effect.finished) {
                continue;
            }

            effect.elapsed_ms += delta_ms;
            effect.current_frame = static_cast<int>(effect.elapsed_ms / kDefaultFrameDurationMs);

            if (effect.elapsed_ms >= effect.duration_ms) {
                effect.finished = true;
            }
        }

        active_effects_.erase(std::remove_if(active_effects_.begin(), active_effects_.end(),
                                             [](const ActiveEffect& effect) {
                                                 return effect.finished;
                                             }),
                              active_effects_.end());
    }

    update_projectiles(current_time_ms_);
    update_damage_numbers(current_time_ms_);
}

void EffectPlayer::render(const Camera& camera) {
    if (!active_effects_.empty()) {
        for (const auto& effect : active_effects_) {
            if (effect.finished) {
                continue;
            }

            auto texture = get_effect_texture(effect.effect_id, effect.current_frame);
            if (!texture) {
                continue;
            }

            Position screen_pos = camera.world_to_screen({effect.x, effect.y});

            EffectRef ref;
            if (parse_effect_id(effect.effect_id, &ref)) {
                const int frame_index = ref.base_index + effect.current_frame;
                auto sprite_opt = resource_manager_.get_sprite(ref.archive, frame_index);
                if (sprite_opt) {
                    renderer_.draw_sprite(*texture, screen_pos.x, screen_pos.y,
                                          sprite_opt->offset_x, sprite_opt->offset_y);
                    continue;
                }
            }

            renderer_.draw_texture(*texture, screen_pos.x, screen_pos.y);
        }
    }

    render_projectiles(renderer_, camera);
    render_damage_numbers(renderer_, camera);
}

void EffectPlayer::clear() {
    active_effects_.clear();
    projectiles_.clear();
    damage_numbers_.clear();
}

std::shared_ptr<Texture> EffectPlayer::get_effect_texture(const std::string& effect_id, int frame) {
    EffectRef ref;
    if (!parse_effect_id(effect_id, &ref)) {
        return nullptr;
    }

    if (!ensure_archive_loaded(ref.archive, resource_manager_)) {
        return nullptr;
    }

    const int frame_index = ref.base_index + frame;
    auto sprite_opt = resource_manager_.get_sprite(ref.archive, frame_index);
    if (!sprite_opt) {
        return nullptr;
    }

    const std::string cache_key = "effect:" + ref.archive + ":" +
                                  std::to_string(frame_index);
    return renderer_.get_sprite_texture(cache_key, *sprite_opt, true);
}

} // namespace mir2::render
