# NPC 脚本系统

本目录存放 Lua 格式的 NPC 脚本，用于定义 NPC 与玩家的交互逻辑。

## 脚本格式

每个 `.lua` 文件必须返回一个 `function(npc, player)` 闭包：

```lua
-- example.lua
return function(npc, player)
    npc.say(player, "你好！")
end
```

## 可用 API

| 函数 | 参数 | 说明 |
|------|------|------|
| `npc.say(player, text)` | player: 玩家对象, text: 字符串 | 向玩家显示对话文本 |
| `npc.openShop(player, store_id)` | store_id: 商店ID | 打开商店界面 |
| `npc.giveItem(player, item_id, count)` | item_id: 物品ID, count: 数量 | 给予玩家物品 |
| `npc.takeGold(player, amount)` | amount: 金币数量 | 扣除玩家金币 |
| `npc.startQuest(player, quest_id)` | quest_id: 任务ID | 开始指定任务 |
| `npc.teleport(player, map_id, x, y)` | map_id: 地图ID, x/y: 坐标 | 传送玩家到指定位置 |
| `npc.showMenu(player, options)` | options: 字符串数组 | 显示选择菜单，返回选项序号 (1-based) |
| `npc.openStorage(player)` | player: 玩家对象 | 打开仓库界面 |

## 热重载机制

服务器支持在运行时重新加载 NPC 脚本，无需重启：

1. **GM 命令**: 在游戏内使用 `@reloadnpc <脚本名>` 重载指定脚本，或 `@reloadnpc all` 重载全部脚本。
2. **文件监听**: 服务器监听本目录的文件变更，修改保存后自动重载对应脚本。
3. **重载流程**: 重新执行 `dofile()`，获取新的闭包替换旧引用，下次 NPC 交互即使用新逻辑。

> **注意**: 热重载不会影响正在进行中的 NPC 对话，仅对新发起的交互生效。

## 从 JSON 迁移

本目录中的脚本由 `config/npc_scripts.json` 迁移而来，功能映射如下：

| JSON `op` 字段 | Lua API |
|----------------|---------|
| `SAY` | `npc.say()` |
| `OPEN_MERCHANT` | `npc.openShop()` |
| `GIVE_ITEM` | `npc.giveItem()` |
| `TAKE_GOLD` | `npc.takeGold()` |
| `START_QUEST` | `npc.startQuest()` |
| `TELEPORT` | `npc.teleport()` |
