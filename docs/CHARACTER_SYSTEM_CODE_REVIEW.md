# Character System Module - Code Review Report (Merged)

## 1. Review Summary

| Priority | Count | Description |
|---|---:|---|
| P0 - Critical | 4 | Data integrity / identity mapping / security baseline risks |
| P1 - High | 6 | Architectural inconsistency and missing runtime guarantees |
| P2 - Medium | 6 | Code quality, performance, maintainability |
| P3 - Low | 4 | Style and minor improvements |

> This version merges 2 omitted items directly into the original report:
> - P0: `client_id -> character_id` fallback cast identity mismatch risk
> - P1: `PlayerManager` lifecycle not wired into main login/select flow

---

## 2. Detailed Findings (Merged Additions)

### P0 - Critical Issues

#### 2.19 Identity Mapping Risk: `client_id -> character_id` fallback cast

When handler context is built, `client_id` is cast to `uint32_t` as fallback character ID if role binding is absent:

- `src/server/logic/logic_server.cc:742`
- `src/server/logic/logic_server.cc:745`

Then movement and other handlers continue on that ID path:

- `src/server/logic/handlers/movement/movement_handler.cc:213`

**Risk**

- Connection identity and character identity can be mixed.
- Potential wrong-entity operations and privilege boundary confusion.
- Truncation via `uint64_t -> uint32_t` compounds collision risk.

---

### P1 - High Priority Issues

#### 2.20 `PlayerManager` lifecycle not integrated with online role flow

Chat and guild handlers depend on `PlayerManager::GetPlayer(ctx.client_id)`:

- `src/server/logic/handlers/chat/chat_handler.cc:223`
- `src/server/logic/handlers/guild/guild_handler.cc:96`

But no production-side creation path for `PlayerManager::CreatePlayer(...)` was found in server logic flow:

- Declaration/implementation only:
  - `src/server/game/entity/player_manager.h:43`
  - `src/server/game/entity/player_manager.cc:12`

**Risk**

- Runtime `GetPlayer(...) == nullptr` causes feature degradation in chat/guild command flows.
- Further amplifies dual-model divergence between Player model and ECS model.

---

## 3. Improvement Recommendations (Merged)

### R7: Remove identity fallback cast and enforce bound role identity (P0)

**Required changes**

1. In handler context construction, do not fallback to `static_cast<uint32_t>(client_id)` for gameplay identity.
2. Require role binding (`role_store_.GetRoleId(client_id)`) before entering character/gameplay handlers.
3. Standardize one authoritative identity pipeline (`client_id -> bound role_id -> character_id`).

### R8: Resolve `PlayerManager` vs ECS lifecycle split (P1)

Choose one of the two paths (prefer option 2):

1. Keep `PlayerManager`: explicitly create/remove/sync player records on select-role/login/logout.
2. Deprecate `PlayerManager` in handlers and query ECS/CharacterEntityManager directly.

---

## 4. Action Plan Update

| Priority | Issue | Impact | Action |
|---|---|---|---|
| P0 | 2.19 identity fallback cast | Identity mismatch / wrong-entity operations | Remove fallback cast; require bound role ID |
| P1 | 2.20 PlayerManager lifecycle gap | Chat/Guild path instability and state divergence | Consolidate online lifecycle into ECS or fully wire PlayerManager |

