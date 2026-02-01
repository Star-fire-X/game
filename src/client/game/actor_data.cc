#include "client/game/actor_data.h"

#include <cstdint>

namespace mir2::game {

namespace {
constexpr uint32_t kStatePoisonGreen = 0x80000000;  // 绿毒
constexpr uint32_t kStatePoisonRed = 0x40000000;    // 红毒
constexpr uint32_t kStateEffectBlue = 0x20000000;   // 蓝色效果
constexpr uint32_t kStateEffectYellow = 0x10000000; // 黄色效果
constexpr uint32_t kStateParalysis = 0x08000000;    // 麻痹
constexpr uint32_t kStateStone = 0x04000000;        // 石化
}

const MonsterAction* get_monster_action(ActorRace race, int /*appearance*/) {
    switch (race) {
        case ActorRace::SOCCER_BALL:
            return &MA9;
        case ActorRace::CHICKEN_DOG:
            return &MA10;
        case ActorRace::DEER:
            return &MA11;
        case ActorRace::GUARD:
        case ActorRace::SUPERIOR_GUARD:
            return &MA12;
        case ActorRace::KILLER_HERB:
            return &MA13;
        case ActorRace::SKELETON_OMA:
        case ActorRace::WHITE_SKELETON:
        case ActorRace::SKELETON_KING:
        case ActorRace::SKELETON_SOLDIER_66:
        case ActorRace::SKELETON_SOLDIER_67:
        case ActorRace::SKELETON_SOLDIER_68:
        case ActorRace::SKELETON_ARCHER:
            return &MA14;
        case ActorRace::CAT_MON:
        case ActorRace::HUSUABI:
        case ActorRace::COW_FACE:
        case ActorRace::FIRE_COW_FACE:
        case ActorRace::COW_FACE_KING:
            return &MA19;
        case ActorRace::NPC:
            return &MA50;
        default:
            return nullptr;
    }
}

ColorEffect get_color_effect_from_state(uint32_t state, bool is_focused) {
    ColorEffect effect = ColorEffect::NONE;

    if (is_focused) {
        effect = ColorEffect::BRIGHT;
    }

    if ((state & kStatePoisonGreen) != 0) {
        effect = ColorEffect::GREEN;
    }
    if ((state & kStatePoisonRed) != 0) {
        effect = ColorEffect::RED;
    }
    if ((state & kStateEffectBlue) != 0) {
        effect = ColorEffect::BLUE;
    }
    if ((state & kStateEffectYellow) != 0) {
        effect = ColorEffect::YELLOW;
    }
    if ((state & kStateParalysis) != 0) {
        effect = ColorEffect::FUCHSIA;
    }
    if ((state & kStateStone) != 0) {
        effect = ColorEffect::GRAYSCALE;
    }

    return effect;
}

}  // namespace mir2::game
