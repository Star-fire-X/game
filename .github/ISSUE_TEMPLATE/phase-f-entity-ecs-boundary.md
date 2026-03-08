---
name: "Phase F - game/entity 与 ECS 边界重划"
about: "执行 F 阶段：去 PlayerManager 依赖，收敛到 ECS 查询"
title: "[Phase F] game/entity 与 ECS 边界重划"
labels: "refactor,server-structure,phase-f,ecs"
assignees: ""
---

## 目标

消除聊天/行会链路对 `game/entity` wrapper 的强耦合，统一到 ECS 数据访问。

## 任务清单

- [ ] **F-01** 新建 `player_presence_service`（ECS 访问层）。  
  文件清单: `src/server/logic/services/player_presence_service.h` (new), `src/server/logic/services/player_presence_service.cc` (new), `src/server/logic/CMakeLists.txt`

- [ ] **F-02** `ChatHandler` 去 `PlayerManager` 依赖。  
  文件清单: `src/server/logic/handlers/chat/chat_handler.h`, `src/server/logic/handlers/chat/chat_handler.cc`, `src/server/game/chat/chat_service.h`, `src/server/game/chat/chat_service.cc`

- [ ] **F-03** `GuildHandler` 去 `PlayerManager` 依赖。  
  文件清单: `src/server/logic/handlers/guild/guild_handler.h`, `src/server/logic/handlers/guild/guild_handler.cc`, `src/server/ecs/systems/guild_system.h`, `src/server/ecs/systems/guild_system.cc`

- [ ] **F-04** 将聊天/行会相关 ID 与在线状态查询统一迁到 ECS 组件查询。  
  文件清单: `src/server/ecs/components/character_components.h`, `src/server/ecs/components/guild_component.h`, `src/server/ecs/components/party_component.h`, `src/server/game/chat/chat_service.cc`

- [ ] **F-05** 下线 `game/entity/player.*`（无引用后）。  
  文件清单: `src/server/game/entity/player.h`, `src/server/game/entity/player.cc`

- [ ] **F-06** 下线 `game/entity/player_manager.*`（无引用后）。  
  文件清单: `src/server/game/entity/player_manager.h`, `src/server/game/entity/player_manager.cc`

- [ ] **F-07** `logic_server` 不再 include `player_manager.h`。  
  文件清单: `src/server/logic/logic_server.cc`

- [ ] **F-08** 更新相关测试用例依赖注入方式。  
  文件清单: `tests/server/logic/chat_handler_test.cc`, `tests/server/guild/guild_handler_test.cc`

## 验收标准

- [ ] `logic_server` 仅依赖 ECS 相关管理器。
- [ ] 聊天/行会行为一致，测试通过。
- [ ] `game/entity` 相关 wrapper 按计划下线。

## 依赖关系

- 前置阶段: Phase D
- 后续阶段: Phase G, Phase H

## 参考

- `docs/plans/2026-02-09-max-fix-checklist.md`

