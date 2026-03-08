/**
 * @file ranking_service.h
 * @brief Ranking service with short-lived in-memory cache.
 */

#ifndef MIR2_LOGIC_SERVICES_RANKING_SERVICE_H_
#define MIR2_LOGIC_SERVICES_RANKING_SERVICE_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

#include "ranking_generated.h"

namespace mir2::db {
class PgConnectionPool;
}  // namespace mir2::db

namespace mir2::logic {

struct RankingEntryView {
  uint32_t rank = 0;
  uint64_t entity_id = 0;
  std::string name;
  int64_t value = 0;
  std::string extra;
};

struct RankingResultView {
  std::vector<RankingEntryView> entries;
  uint32_t total_count = 0;
};

class RankingService {
 public:
  explicit RankingService(entt::registry& ecs_registry,
                          std::shared_ptr<mir2::db::PgConnectionPool> db_pool = nullptr);

  RankingResultView GetRanking(mir2::proto::RankingType type,
                               uint32_t page,
                               uint32_t page_size);
  std::optional<RankingEntryView> GetMyRank(mir2::proto::RankingType type,
                                            uint64_t entity_id);

  void Invalidate();

 private:
  struct CacheEntry {
    std::vector<RankingEntryView> entries;
    int64_t expire_at_ms = 0;
  };

  std::optional<std::vector<RankingEntryView>> BuildRankingFromDbUnlocked(
      mir2::proto::RankingType type) const;
  std::vector<RankingEntryView> BuildRankingFromEcsUnlocked(
      mir2::proto::RankingType type) const;
  std::vector<RankingEntryView> BuildRankingUnlocked(
      mir2::proto::RankingType type) const;
  std::vector<RankingEntryView> GetOrBuildCacheUnlocked(
      mir2::proto::RankingType type);
  bool PersistenceEnabled() const;
  static int64_t NowMs();
  static uint8_t CacheKey(mir2::proto::RankingType type);

  entt::registry& ecs_registry_;
  std::shared_ptr<mir2::db::PgConnectionPool> db_pool_;
  std::mutex mutex_;
  std::unordered_map<uint8_t, CacheEntry> cache_;
};

}  // namespace mir2::logic

#endif  // MIR2_LOGIC_SERVICES_RANKING_SERVICE_H_
