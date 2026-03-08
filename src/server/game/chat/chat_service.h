/**
 * @file chat_service.h
 * @brief 聊天服务
 */

#ifndef MIR2_GAME_CHAT_CHAT_SERVICE_H_
#define MIR2_GAME_CHAT_CHAT_SERVICE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

namespace mir2::game::map {
class AOIManager;
}  // namespace mir2::game::map

namespace mir2::logic {
class PlayerPresenceService;
}  // namespace mir2::logic

namespace mir2::game::chat {

namespace ChatColor {
constexpr uint32_t kNormal = 0x00FF;       // 黑色 (0, 255)
constexpr uint32_t kWhisper = 0xFCFF;      // 紫色 (252, 255)
constexpr uint32_t kTeam = 0xC4FF;         // 绿色 (196, 255)
constexpr uint32_t kArea = 0x0097;         // 黄色 (0, 151)
constexpr uint32_t kGuild = 0xD4FF;        // 紫色 (212, 255)
constexpr uint32_t kSystemYellow = 0x0097;
constexpr uint32_t kSystemBlue = 0x00F0;
}  // namespace ChatColor

using ChatDispatch = std::pair<uint64_t, std::vector<uint8_t>>;
using ChatDispatchList = std::vector<ChatDispatch>;

class ChatService {
 public:
  ChatService(mir2::logic::PlayerPresenceService& player_presence,
              entt::registry& ecs_registry);

  // 普通聊天 - AOI范围
  ChatDispatchList SendNormalChat(uint64_t sender_id, const std::string& content,
                                  map::AOIManager& aoi);

  // 私聊
  ChatDispatchList SendWhisper(uint64_t sender_id, uint64_t target_id,
                               const std::string& content);

  // 组队聊天
  ChatDispatchList SendTeamChat(uint64_t sender_id, const std::string& content);

  // 区域喊话
  ChatDispatchList SendAreaChat(uint64_t sender_id, const std::string& content);

  // 行会聊天
  ChatDispatchList SendGuildChat(uint64_t sender_id, const std::string& content);

  // 系统消息 (mode: 0=黄色, 2=蓝色)
  ChatDispatchList SendSystemMessage(uint64_t player_id, const std::string& content,
                                     uint8_t mode = 0);

  // 全服广播
  ChatDispatchList BroadcastSystemMessage(const std::string& content);

 private:
  std::vector<uint8_t> BuildChatMessage(uint8_t channel, uint64_t from_id,
                                        const std::string& from_name,
                                        uint64_t to_id,
                                        const std::string& content,
                                        uint32_t color);

  mir2::logic::PlayerPresenceService& player_presence_;
  entt::registry& ecs_registry_;
};

}  // namespace mir2::game::chat

#endif  // MIR2_GAME_CHAT_CHAT_SERVICE_H_
