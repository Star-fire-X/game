/**
 * @file item_template.h
 * @brief Item template data structure and manager.
 */

#ifndef MIR2_SERVER_DATA_ITEM_TEMPLATE_H_
#define MIR2_SERVER_DATA_ITEM_TEMPLATE_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <optional>

namespace mir2::data {

/**
 * @brief Static item template data (mirrors TStdItem from Grobal2.pas).
 */
struct ItemTemplate {
    uint32_t id = 0;              ///< 物品模板ID
    std::string name;             ///< 物品名称
    uint8_t std_mode = 0;         ///< 物品类型(StdMode)
    uint16_t shape = 0;           ///< 形态编号(决定具体功能)
    uint8_t weight = 0;           ///< 重量
    uint8_t ani_count = 0;        ///< 动画帧数
    int8_t special_pwr = 0;       ///< 特殊能力值
    uint8_t item_desc = 0;        ///< 物品描述标志
    uint16_t looks = 0;           ///< 图片编号
    uint16_t dura_max = 0;        ///< 最大耐久度(堆叠物品=数量)
    uint16_t ac = 0;              ///< 药品:HP恢复量 / 装备:防御
    uint16_t mac = 0;             ///< 药品:MP恢复量 / 装备:魔防
    uint16_t dc = 0;              ///< DC提升
    uint16_t mc = 0;              ///< MC提升
    uint16_t sc = 0;              ///< SC提升
    uint8_t need_type = 0;        ///< 需求类型
    uint8_t need_level = 0;       ///< 需求等级
    uint8_t need_class = 99;      ///< 职业限制(99=通用)
    int32_t price = 0;            ///< 价格
    bool stackable = false;       ///< 是否可堆叠
    int stack_limit = 1;          ///< 堆叠上限

    /// Check if item is a drug (StdMode=0)
    [[nodiscard]] bool IsDrug() const { return std_mode == 0; }
    /// Check if item is a scroll (StdMode=3)
    [[nodiscard]] bool IsScroll() const { return std_mode == 3; }
    /// Check if item is a skill book (StdMode=4)
    [[nodiscard]] bool IsSkillBook() const { return std_mode == 4; }
    /// Check if item is an amulet (StdMode=25)
    [[nodiscard]] bool IsAmulet() const { return std_mode == 25; }
    /// Check if item is bundled drug (StdMode=31)
    [[nodiscard]] bool IsBundledDrug() const { return std_mode == 31; }
};

/**
 * @brief Singleton manager for item templates.
 */
class ItemTemplateManager {
 public:
    static ItemTemplateManager& Instance();

    /// Load templates from JSON file
    bool LoadFromJson(const std::string& path);

    /// Get template by ID (returns nullptr if not found)
    [[nodiscard]] const ItemTemplate* GetTemplate(uint32_t item_id) const;

    /// Get all templates
    [[nodiscard]] const std::unordered_map<uint32_t, ItemTemplate>& GetAll() const {
        return templates_;
    }

    /// Clear all templates
    void Clear() { templates_.clear(); }

    /// Get template count
    [[nodiscard]] size_t Count() const { return templates_.size(); }

 private:
    ItemTemplateManager() = default;
    std::unordered_map<uint32_t, ItemTemplate> templates_;
};

}  // namespace mir2::data

#endif  // MIR2_SERVER_DATA_ITEM_TEMPLATE_H_
