/**
 * @file item_template.cc
 * @brief Item template manager implementation.
 */

#include "data/item_template.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "config/runtime_config.h"

namespace mir2::data {

ItemTemplateManager& ItemTemplateManager::Instance() {
    static ItemTemplateManager instance;
    return instance;
}

bool ItemTemplateManager::LoadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json root;
        file >> root;

        if (!root.contains("items") || !root["items"].is_array()) {
            return false;
        }

        for (const auto& item : root["items"]) {
            ItemTemplate tmpl;
            tmpl.id = item.value("id", 0u);
            tmpl.name = item.value("name", "");
            tmpl.std_mode = item.value("std_mode", 0);
            tmpl.shape = item.value("shape", 0);
            tmpl.weight = item.value("weight", 0);
            tmpl.ani_count = item.value("ani_count", 0);
            tmpl.special_pwr = item.value("special_pwr", 0);
            tmpl.item_desc = item.value("item_desc", 0);
            tmpl.looks = item.value("looks", 0);
            tmpl.dura_max = item.value("dura_max", 0);
            tmpl.ac = item.value("ac", 0);
            tmpl.mac = item.value("mac", 0);
            tmpl.dc = item.value("dc", 0);
            tmpl.mc = item.value("mc", 0);
            tmpl.sc = item.value("sc", 0);
            tmpl.need_type = item.value("need_type", 0);
            tmpl.need_level = item.value("need_level", 0);
            tmpl.need_class = item.value("need_class", 99);
            tmpl.price = item.value("price", 0);
            tmpl.stackable = item.value("stackable", false);
            tmpl.stack_limit = item.value("stack_limit", 1);

            if (tmpl.id > 0) {
                templates_[tmpl.id] = std::move(tmpl);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ItemTemplateManager::LoadFromConfigData(
    const mir2::config::ConfigData& config_data) {
    templates_.clear();
    templates_.insert(config_data.items.begin(), config_data.items.end());
    return true;
}

const ItemTemplate* ItemTemplateManager::GetTemplate(uint32_t item_id) const {
    auto it = templates_.find(item_id);
    return it != templates_.end() ? &it->second : nullptr;
}

}  // namespace mir2::data
