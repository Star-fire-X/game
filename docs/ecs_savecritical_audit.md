# SaveCritical Audit (2026-02-24)

## Scope
- `src/server/ecs/character_entity_manager.cc`
- All `SaveCritical(...)` call sites in server logic paths

## Confirmed `SaveCritical` call sites
- `src/server/ecs/systems/trade_system.cc`
- `src/server/game/npc/npc_shop_service.cc`
- `src/server/logic/services/merchant_service.cc`
- `src/server/logic/services/ecs_inventory_service.cc`

## Sync write boundary findings
1. `CharacterEntityManager::SaveCritical()` calls `StorageEngine::SetSync(..., Priority::CRITICAL)` on key `char:<id>`.
2. `CharacterEntityManager::StoreCharacterData()` previously called `StorageEngine::Set(..., Priority::HIGH)`.
3. `StorageEngine::Set(...)` auto-upgrades keys matched by `sync_write_key_prefixes` (default includes `char:`) to sync write.
4. Therefore, before this change, non-critical save paths using `StoreCharacterData` also risked sync persistence on main logic thread.

## Applied change
1. Added `StorageEngine::SetAsyncDurable(...)` to bypass sync-prefix auto-upgrade while preserving durable write checks.
2. Switched `CharacterEntityManager::StoreCharacterData()` to `SetAsyncDurable(...)`.
3. Kept `SaveCritical()` semantics unchanged (still sync).
4. Added metrics for both paths:
   - `logic.ecs.save_critical.*`
   - `logic.ecs.save_async_durable.*`
5. Added per-callsite critical-save counters in:
   - trade
   - npc_shop
   - merchant
   - pickup

## Risk/compat notes
- Critical gameplay/account persistence path remains sync.
- Non-critical character snapshot persistence now avoids sync-prefix forced blocking.
- No schema change in `CharacterData`; JSON fields remain for compatibility.
