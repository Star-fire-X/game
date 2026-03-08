// =============================================================================
// Legend2 Skill UI - Test Mocks
// =============================================================================

#ifndef LEGEND2_TESTS_CLIENT_UI_SKILL_MOCKS_H
#define LEGEND2_TESTS_CLIENT_UI_SKILL_MOCKS_H

#include <gmock/gmock.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include "client/game/skill/skill_manager.h"
#include "render/camera.h"
#include "render/i_renderer.h"

namespace mir2::ui::skill {

class MockRenderer : public render::IRenderer {
public:
    MOCK_METHOD(bool, initialize, (int width, int height, const std::string& title), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(bool, is_initialized, (), (const, override));
    MOCK_METHOD(void, begin_frame, (), (override));
    MOCK_METHOD(void, end_frame, (), (override));
    MOCK_METHOD(void, clear, (const mir2::common::Color& color), (override));
    MOCK_METHOD(mir2::common::Size, get_window_size, (), (const, override));
    MOCK_METHOD(void, set_logical_size, (int width, int height), (override));
    MOCK_METHOD(std::shared_ptr<render::Texture>, create_texture_from_sprite,
                (const mir2::client::Sprite& sprite, bool flip_vertical), (override));
    MOCK_METHOD(void, draw_texture, (const render::Texture& texture, int x, int y), (override));
    MOCK_METHOD(void, draw_texture,
                (const render::Texture& texture,
                 const mir2::common::Rect& src,
                 const mir2::common::Rect& dst),
                (override));
    MOCK_METHOD(void, draw_sprite,
                (const render::Texture& texture, int x, int y, int offset_x, int offset_y),
                (override));
    MOCK_METHOD(void, draw_rect, (const mir2::common::Rect& rect,
                                  const mir2::common::Color& color),
                (override));
    MOCK_METHOD(void, draw_rect_outline, (const mir2::common::Rect& rect,
                                          const mir2::common::Color& color),
                (override));
    MOCK_METHOD(void, draw_line, (int x1, int y1, int x2, int y2,
                                  const mir2::common::Color& color),
                (override));
    MOCK_METHOD(void, draw_point, (int x, int y, const mir2::common::Color& color),
                (override));
    MOCK_METHOD(void, set_draw_color, (const mir2::common::Color& color), (override));
    MOCK_METHOD(void, set_blend_mode, (SDL_BlendMode mode), (override));
};

class MockSkillManager : public client::skill::ISkillManager {
public:
    using LearnedSkillsArray =
        std::array<std::optional<client::skill::ClientLearnedSkill>,
                   client::skill::kSkillManagerMaxSkills>;

    const client::skill::ClientSkillTemplate* get_template(uint32_t skill_id) const override {
        ++get_template_calls;
        auto it = templates_.find(skill_id);
        if (it == templates_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const LearnedSkillsArray& get_learned_skills() const override {
        ++get_learned_skills_calls;
        return learned_skills_;
    }

    bool bind_hotkey(uint8_t slot, uint32_t skill_id) override {
        ++bind_hotkey_calls;
        last_bind_slot = slot;
        last_bind_skill_id = skill_id;
        if (bind_hotkey_result.has_value()) {
            return *bind_hotkey_result;
        }
        if (slot == 0 || slot > client::skill::kSkillManagerHotkeyCount || skill_id == 0) {
            return false;
        }
        hotkeys_[slot - 1] = skill_id;
        return true;
    }

    uint32_t get_skill_by_hotkey(uint8_t slot) const override {
        ++get_skill_by_hotkey_calls;
        if (slot == 0 || slot > client::skill::kSkillManagerHotkeyCount) {
            return 0;
        }
        return hotkeys_[slot - 1];
    }

    int64_t get_remaining_cooldown_ms(uint32_t skill_id, int64_t now_ms) const override {
        (void)now_ms;
        ++get_remaining_cooldown_ms_calls;
        auto it = cooldown_remaining_.find(skill_id);
        if (it == cooldown_remaining_.end()) {
            return 0;
        }
        return it->second;
    }

    void update(int64_t now_ms) override {
        ++update_calls;
        last_update_ms = now_ms;
    }

    void set_skill_by_hotkey(uint8_t slot, uint32_t skill_id) {
        if (slot == 0 || slot > client::skill::kSkillManagerHotkeyCount) {
            return;
        }
        hotkeys_[slot - 1] = skill_id;
    }

    void set_remaining_cooldown(uint32_t skill_id, int64_t remaining_ms) {
        cooldown_remaining_[skill_id] = remaining_ms;
    }

    void set_template(const client::skill::ClientSkillTemplate& skill) {
        templates_[skill.id] = skill;
    }

    void set_learned_skill(size_t index, const client::skill::ClientLearnedSkill& skill) {
        if (index >= learned_skills_.size()) {
            return;
        }
        learned_skills_[index] = skill;
    }

    LearnedSkillsArray learned_skills_{};
    std::unordered_map<uint32_t, client::skill::ClientSkillTemplate> templates_{};
    std::array<uint32_t, client::skill::kSkillManagerHotkeyCount> hotkeys_{};
    std::unordered_map<uint32_t, int64_t> cooldown_remaining_{};

    std::optional<bool> bind_hotkey_result{};
    mutable int get_template_calls = 0;
    mutable int get_learned_skills_calls = 0;
    mutable int get_skill_by_hotkey_calls = 0;
    mutable int get_remaining_cooldown_ms_calls = 0;
    int update_calls = 0;
    int bind_hotkey_calls = 0;
    int64_t last_update_ms = 0;
    uint8_t last_bind_slot = 0;
    uint32_t last_bind_skill_id = 0;
};

struct MockCamera : public render::Camera {};

} // namespace mir2::ui::skill

#endif // LEGEND2_TESTS_CLIENT_UI_SKILL_MOCKS_H
