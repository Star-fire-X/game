#include "common/network/channel_router.h"

#include "common/enums.h"

namespace mir2::common {

ChannelRouter::ChannelRouter() {
  InitializeDefaults();
}

ChannelType ChannelRouter::GetChannel(uint16_t msg_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = routes_.find(msg_id);
  if (it != routes_.end()) {
    return it->second;
  }
  return default_channel_;
}

ChannelType ChannelRouter::GetChannelWithFallback(uint16_t msg_id, bool kcp_available) const {
  ChannelType channel = GetChannel(msg_id);
  if (channel == ChannelType::kKcp && !kcp_available) {
    return ChannelType::kTcp;
  }
  return channel;
}

void ChannelRouter::SetRoute(uint16_t msg_id, ChannelType channel) {
  std::lock_guard<std::mutex> lock(mutex_);
  routes_[msg_id] = channel;
}

void ChannelRouter::SetDefaultChannel(ChannelType channel) {
  std::lock_guard<std::mutex> lock(mutex_);
  default_channel_ = channel;
}

void ChannelRouter::InitializeDefaults() {
  std::lock_guard<std::mutex> lock(mutex_);
  routes_.clear();
  default_channel_ = ChannelType::kTcp;

  // PRD routing principle:
  //   - "看见的走 KCP" : low-latency visual broadcasts (movement, AOI, combat effects).
  //   - "算账的走 TCP" : reliable state changes, commands, and transactional messages.
  const auto add_kcp = [this](MsgId msg_id) {
    routes_[static_cast<uint16_t>(msg_id)] = ChannelType::kKcp;
  };

  // Movement + AOI visual sync (player/monster positions, enter/leave).
  add_kcp(MsgId::kMoveReq);
  add_kcp(MsgId::kMoveRsp);
  add_kcp(MsgId::kEntityEnter);
  add_kcp(MsgId::kEntityLeave);
  add_kcp(MsgId::kEntityMove);
  add_kcp(MsgId::kEntityUpdate);

  // HP/MP and lightweight state sync broadcasts.
  add_kcp(MsgId::kStateSync);

  // Combat visuals (attack animation, hit/death effects, skill visuals).
  add_kcp(MsgId::kAttackRsp);
  add_kcp(MsgId::kDeath);
  add_kcp(MsgId::kSkillEffect);
  add_kcp(MsgId::kPlayEffect);
  add_kcp(MsgId::kPlaySound);
}

void ChannelRouter::AddRange(uint16_t start, uint16_t end, ChannelType channel) {
  if (end < start) {
    return;
  }
  for (uint16_t msg_id = start; msg_id <= end; ++msg_id) {
    routes_[msg_id] = channel;
  }
}

}  // namespace mir2::common
