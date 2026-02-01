/**
 * @file monster_template_mapper.h
 * @brief Monster template id -> render configuration mapping.
 */

#ifndef LEGEND2_CLIENT_GAME_MONSTER_MONSTER_TEMPLATE_MAPPER_H
#define LEGEND2_CLIENT_GAME_MONSTER_MONSTER_TEMPLATE_MAPPER_H

#include "client/game/actor_data.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mir2::game::monster {

struct MonsterRenderInfo {
    ActorRace race = ActorRace::CHICKEN_DOG;
    int appearance = 0;
};

class MonsterTemplateMapper {
public:
    static MonsterTemplateMapper& instance();

    bool load_from_file(const std::string& file_path);
    MonsterRenderInfo get_render_info(uint32_t template_id) const;
    bool has_template(uint32_t template_id) const;

private:
    MonsterTemplateMapper() = default;

    std::unordered_map<uint32_t, MonsterRenderInfo> templates_;
};

} // namespace mir2::game::monster

#endif // LEGEND2_CLIENT_GAME_MONSTER_MONSTER_TEMPLATE_MAPPER_H
