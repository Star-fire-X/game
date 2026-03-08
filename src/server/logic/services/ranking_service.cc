#include "logic/services/ranking_service.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <string>
#include <utility>

#include "ecs/components/character_components.h"
#include "ecs/components/guild_component.h"
#include "log/logger.h"
#include "storage_engine/backends/postgres/pg_connection_pool.h"

namespace mir2::logic {

namespace {

constexpr int64_t kCacheTtlMs = 5 * 60 * 1000;
constexpr uint32_t kDefaultPageSize = 20;
constexpr uint32_t kMaxPageSize = 200;

uint32_t ClampPage(uint32_t page) {
  return page == 0 ? 1 : page;
}

uint32_t ClampPageSize(uint32_t page_size) {
  if (page_size == 0) {
    return kDefaultPageSize;
  }
  return std::min(page_size, kMaxPageSize);
}

bool CompareEntry(const RankingEntryView& lhs, const RankingEntryView& rhs) {
  if (lhs.value != rhs.value) {
    return lhs.value > rhs.value;
  }
  if (lhs.entity_id != rhs.entity_id) {
    return lhs.entity_id < rhs.entity_id;
  }
  return lhs.name < rhs.name;
}

std::string BuildDbValueExpression(mir2::proto::RankingType type) {
  switch (type) {
    case mir2::proto::RankingType::LEVEL:
      return "c.level::bigint";
    case mir2::proto::RankingType::PK:
      // Some environments do not have a dedicated pk_level column yet.
      return "COALESCE((to_jsonb(c)->>'pk_level')::bigint, 0)";
    case mir2::proto::RankingType::GOLD:
      return "c.gold::bigint";
    default:
      return {};
  }
}

}  // namespace

RankingService::RankingService(entt::registry& ecs_registry,
                               std::shared_ptr<mir2::db::PgConnectionPool> db_pool)
    : ecs_registry_(ecs_registry),
      db_pool_(std::move(db_pool)) {}

RankingResultView RankingService::GetRanking(mir2::proto::RankingType type,
                                             uint32_t page,
                                             uint32_t page_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto entries = GetOrBuildCacheUnlocked(type);

  RankingResultView result;
  result.total_count = static_cast<uint32_t>(
      std::min<size_t>(entries.size(), std::numeric_limits<uint32_t>::max()));

  const uint32_t clamped_page = ClampPage(page);
  const uint32_t clamped_size = ClampPageSize(page_size);
  const uint64_t start_u64 =
      static_cast<uint64_t>(clamped_page - 1) * static_cast<uint64_t>(clamped_size);
  if (start_u64 >= entries.size()) {
    return result;
  }

  const size_t start = static_cast<size_t>(start_u64);
  const size_t end = std::min(entries.size(), start + static_cast<size_t>(clamped_size));
  result.entries.insert(result.entries.end(), entries.begin() + start, entries.begin() + end);
  return result;
}

std::optional<RankingEntryView> RankingService::GetMyRank(mir2::proto::RankingType type,
                                                          uint64_t entity_id) {
  if (entity_id == 0) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto entries = GetOrBuildCacheUnlocked(type);
  const auto it = std::find_if(entries.begin(), entries.end(),
                               [entity_id](const RankingEntryView& entry) {
                                 return entry.entity_id == entity_id;
                               });
  if (it == entries.end()) {
    return std::nullopt;
  }
  return *it;
}

void RankingService::Invalidate() {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.clear();
}

std::vector<RankingEntryView> RankingService::GetOrBuildCacheUnlocked(
    mir2::proto::RankingType type) {
  const uint8_t key = CacheKey(type);
  const int64_t now_ms = NowMs();
  auto it = cache_.find(key);
  if (it != cache_.end() && it->second.expire_at_ms > now_ms) {
    return it->second.entries;
  }

  CacheEntry entry;
  entry.entries = BuildRankingUnlocked(type);
  entry.expire_at_ms = now_ms + kCacheTtlMs;
  cache_[key] = entry;
  return entry.entries;
}

std::vector<RankingEntryView> RankingService::BuildRankingUnlocked(
    mir2::proto::RankingType type) const {
  if (const auto db_entries = BuildRankingFromDbUnlocked(type); db_entries.has_value()) {
    return *db_entries;
  }
  return BuildRankingFromEcsUnlocked(type);
}

std::optional<std::vector<RankingEntryView>> RankingService::BuildRankingFromDbUnlocked(
    mir2::proto::RankingType type) const {
  if (!PersistenceEnabled()) {
    return std::nullopt;
  }
  if (type == mir2::proto::RankingType::GUILD) {
    return std::nullopt;
  }

  const std::string value_expr = BuildDbValueExpression(type);
  if (value_expr.empty()) {
    return std::nullopt;
  }

  try {
    const auto conn = db_pool_->Acquire();
    if (!conn) {
      return std::nullopt;
    }
    db::PgConnectionGuard guard(*db_pool_, conn);
    pqxx::read_transaction txn(*conn);

    const std::string sql = "SELECT c.id, c.name, " + value_expr +
                            " AS ranking_value FROM characters c "
                            "ORDER BY ranking_value DESC, c.id ASC";
    const pqxx::result rows = txn.exec(sql);
    if (rows.empty()) {
      return std::nullopt;
    }

    std::vector<RankingEntryView> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
      RankingEntryView entry;
      entry.entity_id = row["id"].as<uint64_t>(0);
      entry.name = row["name"].as<std::string>("");
      entry.value = row["ranking_value"].as<int64_t>(0);
      result.push_back(std::move(entry));
    }

    std::sort(result.begin(), result.end(), CompareEntry);
    for (size_t i = 0; i < result.size(); ++i) {
      result[i].rank = static_cast<uint32_t>(i + 1);
    }
    return result;
  } catch (const std::exception& ex) {
    SYSLOG_WARN("RankingService DB query failed type={} error={}",
                static_cast<int>(type),
                ex.what());
    return std::nullopt;
  }
}

std::vector<RankingEntryView> RankingService::BuildRankingFromEcsUnlocked(
    mir2::proto::RankingType type) const {
  std::vector<RankingEntryView> result;
  switch (type) {
    case mir2::proto::RankingType::LEVEL:
    case mir2::proto::RankingType::PK:
    case mir2::proto::RankingType::GOLD: {
      auto view = ecs_registry_.view<mir2::ecs::CharacterIdentityComponent,
                                     mir2::ecs::CharacterAttributesComponent>();
      for (const auto entity : view) {
        const auto& identity =
            view.get<mir2::ecs::CharacterIdentityComponent>(entity);
        const auto& attributes =
            view.get<mir2::ecs::CharacterAttributesComponent>(entity);
        RankingEntryView entry;
        entry.entity_id = identity.id;
        entry.name = identity.name;
        if (type == mir2::proto::RankingType::LEVEL) {
          entry.value = attributes.level;
        } else if (type == mir2::proto::RankingType::PK) {
          entry.value = attributes.pk_level;
        } else {
          entry.value = attributes.gold;
        }
        result.push_back(std::move(entry));
      }
      break;
    }
    case mir2::proto::RankingType::GUILD: {
      auto view = ecs_registry_.view<mir2::ecs::GuildComponent>();
      for (const auto entity : view) {
        const auto& guild = view.get<mir2::ecs::GuildComponent>(entity);
        RankingEntryView entry;
        entry.entity_id = guild.guild_id;
        entry.name = guild.guild_name;
        entry.value = guild.match_point;
        entry.extra = std::to_string(guild.members.size());
        result.push_back(std::move(entry));
      }
      break;
    }
    default:
      break;
  }

  std::sort(result.begin(), result.end(), CompareEntry);
  for (size_t i = 0; i < result.size(); ++i) {
    result[i].rank = static_cast<uint32_t>(i + 1);
  }
  return result;
}

bool RankingService::PersistenceEnabled() const {
  return db_pool_ != nullptr && db_pool_->IsReady() && db_pool_->PoolSize() > 0;
}

int64_t RankingService::NowMs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

uint8_t RankingService::CacheKey(mir2::proto::RankingType type) {
  return static_cast<uint8_t>(type);
}

}  // namespace mir2::logic
