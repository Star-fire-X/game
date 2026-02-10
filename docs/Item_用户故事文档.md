# 物品系统用户故事文档

> 基于 mir2 项目代码分析生成
> 生成日期: 2026-01-30

---

## 目录

1. [物品系统概述](#物品系统概述)
2. [物品分类表](#物品分类表)
3. [核心数据结构](#核心数据结构)
4. [Epic 级别用户故事](#epic-级别用户故事)
5. [详细用户故事](#详细用户故事)
6. [物品效果计算公式](#物品效果计算公式)
7. [边界场景处理](#边界场景处理)

---

## 物品系统概述

mir2 物品系统涵盖非装备类物品的使用、堆叠、购买、出售、丢弃、拾取等功能模块。本文档聚焦于消耗品、材料、任务物品、特殊道具等非装备类物品。

### 核心文件索引

| 文件 | 路径 | 职责 |
|------|------|------|
| Grobal2.pas | Source/Common/Grobal2.pas | 全局常量、物品数据类型定义 |
| ObjBase.pas | Source/M2Server/ObjBase.pas | 物品使用、丢弃、拾取逻辑 |
| ObjNpc.pas | Source/M2Server/ObjNpc.pas | NPC购买、出售逻辑 |
| itmunit.pas | Source/M2Server/itmunit.pas | 物品升级处理 |
| Magic.pas | Source/M2Server/Magic.pas | 符纸消耗逻辑 |

---

## 物品分类表

### 按物品类型(StdMode)分类

| StdMode | 类型 | 说明 | 可堆叠 | 特殊属性 |
|---------|------|------|--------|----------|
| 0 | 药品 | 恢复HP/MP的药水 | 否 | AC=HP恢复, MAC=MP恢复 |
| 1 | 肉类 | 动物掉落的肉 | 否 | 掉落时品质下降 |
| 2 | 食物 | 餐厅食物 | 否 | - |
| 3 | 卷轴 | 传送卷、祝福油等 | 否 | Shape决定具体功能 |
| 4 | 技能书 | 学习魔法技能 | 否 | 职业/等级限制 |
| 25 | 符纸 | 道士施法消耗品 | 是 | 耐久度=数量 |
| 30 | 照明 | 蜡烛、灯笼 | 是 | 耐久度=数量 |
| 31 | 捆绑药 | 6个药品的捆绑包 | 否 | 使用后拆分为6个 |
| 40 | 特殊肉 | 掉落时品质下降 | 否 | Dura-2000 |
| 42 | 药材 | 合成材料 | 否 | - |
| 43 | 特殊物品 | 不可修理的物品 | 否 | - |
| 45 | 骰子/彩票 | 随机外观物品 | 否 | 随机Looks |
| 50 | 商品券 | 特殊兑换券 | 否 | - |

> 代码位置: [ObjNpc.pas:2778](Source/M2Server/ObjNpc.pas#L2778)

### 卷轴类(StdMode=3)按Shape分类

| Shape | 名称 | 功能 | 使用限制 |
|-------|------|------|----------|
| 1 | 随机传送卷 | 传送到出生点 | 活动玩家不可用 |
| 2 | 地牢传送卷 | 当前地图随机传送 | 禁止随机移动地图不可用 |
| 3 | 回城卷 | 传送到回城点/红名村 | 活动玩家不可用 |
| 4 | 祝福油 | 武器增加幸运 | 需穿戴武器 |
| 5 | 回城传书 | 传送到行会城堡 | 需占领城堡 |
| 9 | 修复油 | 普通修复武器耐久 | 需穿戴武器 |
| 10 | 武神油 | 特殊修复武器耐久 | 需穿戴武器 |
| 11 | 彩票 | 抽奖 | - |
| INSTANTABILUP_DRUG | 能力提升药 | 临时提升属性 | 有持续时间 |
| INSTANT_EXP_DRUG | 经验药水 | 获得经验值 | - |

> 代码位置: [ObjBase.pas:3625-3720](Source/M2Server/ObjBase.pas#L3625-L3720)

### 药品类(StdMode=0)按Shape分类

| Shape | 名称 | 功能 |
|-------|------|------|
| 0 | 普通药品 | 渐进恢复HP/MP |
| FASTFILL_ITEM | 仙水 | 瞬间恢复HP/MP |
| FREE_UNKNOWN_ITEM | 解咒药水 | 解除诅咒装备 |

> 代码位置: [ObjBase.pas:8078-8106](Source/M2Server/ObjBase.pas#L8078-L8106)

---

## 核心数据结构

### TStdItem - 标准物品模板

```pascal
TStdItem = record
  Name: string[14];        // 物品名称
  StdMode: byte;           // 物品类型
  Shape: byte;             // 形态编号(决定具体功能)
  Weight: byte;            // 重量
  AniCount: byte;          // 动画帧数
  SpecialPwr: shortint;    // 特殊能力值
  ItemDesc: byte;          // 物品描述标志
  Looks: word;             // 图片编号
  DuraMax: word;           // 最大耐久度(堆叠物品=数量)
  AC: word;                // 药品:HP恢复量
  MAC: word;               // 药品:MP恢复量
  DC: word;                // 能力提升药:DC提升
  MC: word;                // 能力提升药:MC提升
  SC: word;                // 能力提升药:SC提升
  Need: byte;              // 需求类型
  NeedLevel: byte;         // 需求等级
  Price: integer;          // 价格
end;
```

> 代码位置: [Grobal2.pas:404-447](Source/Common/Grobal2.pas#L404-L447)

### TUserItem - 用户物品实例

```pascal
TUserItem = packed record
  MakeIndex: integer;      // 制造索引(唯一ID)
  Index: word;             // 标准物品索引
  Dura: word;              // 当前耐久度/数量
  DuraMax: word;           // 最大耐久度
  Desc: array[0..13] of byte;  // 描述数组
  ColorR, ColorG, ColorB: byte;  // 颜色
  Prefix: array[0..12] of char;  // 前缀名称
end;
```

> 代码位置: [Grobal2.pas:453-471](Source/Common/Grobal2.pas#L453-L471)

### TMapItem - 地图物品记录

```pascal
TMapItem = record
  useritem: TUserItem;  // 用户物品数据
  Name: string[14];     // 物品名称
  Looks: word;          // 外观图片编号
  AniCount: byte;       // 动画帧数
  Count: integer;       // 物品数量(金币)
  Ownership: TObject;   // 拾取权归属者
  Droptime: longword;   // 物品掉落时间
  Droper: TObject;      // 掉落者
end;
```

> 代码位置: [Grobal2.pas:673-684](Source/Common/Grobal2.pas#L673-L684)

---

## Epic 级别用户故事

### Epic 1: 物品使用与消耗

**描述**: 玩家能够使用背包中的消耗品，获得恢复、传送、属性提升等效果。

**核心方法**:
- `EatItem` - [ObjBase.pas:8068-8175](Source/M2Server/ObjBase.pas#L8068-L8175)
- `UseScroll` - [ObjBase.pas:3625-3720](Source/M2Server/ObjBase.pas#L3625-L3720)
- `ServerGetEatItem` - [ObjBase.pas:14932-15027](Source/M2Server/ObjBase.pas#L14932-L15027)

---

### Epic 2: 技能书学习

**描述**: 玩家能够使用技能书学习新的魔法技能。

**核心方法**:
- `ReadBook` - [ObjBase.pas:8200-8229](Source/M2Server/ObjBase.pas#L8200-L8229)
- `IsMyMagic` - [ObjBase.pas:8182-8193](Source/M2Server/ObjBase.pas#L8182-L8193)

---

### Epic 3: 物品丢弃与拾取

**描述**: 玩家能够丢弃背包中的物品到地上，或拾取地上的物品。

**核心方法**:
- `UserDropItem` - [ObjBase.pas:7874-7906](Source/M2Server/ObjBase.pas#L7874-L7906)
- `DropItemDown` - [ObjBase.pas:7754-7811](Source/M2Server/ObjBase.pas#L7754-L7811)
- `PickUp` - [ObjBase.pas:7929-8049](Source/M2Server/ObjBase.pas#L7929-L8049)

---

### Epic 4: 物品购买与出售

**描述**: 玩家能够从NPC商店购买物品，或将物品出售给NPC。

**核心方法**:
- `UserBuyItem` - [ObjNpc.pas:3132-3181](Source/M2Server/ObjNpc.pas#L3132-L3181)
- `UserSellItem` - [ObjNpc.pas:2999-3043](Source/M2Server/ObjNpc.pas#L2999-L3043)
- `AddGoods` - [ObjNpc.pas:2970-2997](Source/M2Server/ObjNpc.pas#L2970-L2997)

---

### Epic 5: 符纸消耗系统

**描述**: 道士施法时消耗符纸，符纸耐久度代表数量。

**核心方法**:
- 符纸检查 - [Magic.pas:697-750](Source/M2Server/Magic.pas#L697-L750)
- 符纸消耗 - [Magic.pas:949-1061](Source/M2Server/Magic.pas#L949-L1061)

---

### Epic 6: 特殊道具效果

**描述**: 祝福油、修复油、传送卷等特殊道具的使用效果。

**核心方法**:
- `MakeWeaponGoodLock` - [ObjBase.pas:3729-3800](Source/M2Server/ObjBase.pas#L3729-L3800)
- `RepaireWeaponNormaly` - ObjBase.pas
- `UserSpaceMove` - ObjBase.pas

---

## 详细用户故事

### US-001: 使用普通药品恢复HP/MP

**作为** 玩家
**我想要** 使用药品恢复生命值和魔法值
**以便** 在战斗中保持生存能力

**验收标准**:
- [x] StdMode=0的物品为药品类
- [x] AC值决定HP恢复量，MAC值决定MP恢复量
- [x] 恢复值累加到IncHealth/IncSpell，渐进恢复
- [x] 单次累加上限500点
- [x] 禁药地图无法使用

**技术实现要点**:
- 涉及文件: [ObjBase.pas:8078-8106](Source/M2Server/ObjBase.pas#L8078-L8106)
- 关键方法: `EatItem`, `IncHealthSpell`
- 数据结构: `TStdItem.AC`, `TStdItem.MAC`

**优先级**: P0
**复杂度**: S
**状态**: 已实现

---

### US-002: 使用仙水瞬间恢复

**作为** 玩家
**我想要** 使用仙水瞬间恢复HP和MP
**以便** 在紧急情况下快速恢复

**验收标准**:
- [x] Shape=FASTFILL_ITEM的药品为仙水
- [x] 调用IncHealthSpell瞬间恢复
- [x] 不受渐进恢复上限限制

**技术实现要点**:
- 涉及文件: [ObjBase.pas:8082-8087](Source/M2Server/ObjBase.pas#L8082-L8087)
- 关键方法: `IncHealthSpell(std.AC, std.MAC)`

**优先级**: P0
**复杂度**: S
**状态**: 已实现

---

### US-003: 使用解咒药水

**作为** 玩家
**我想要** 使用解咒药水解除诅咒装备
**以便** 脱下无法脱下的装备

**验收标准**:
- [x] Shape=FREE_UNKNOWN_ITEM的药品为解咒药水
- [x] 设置BoNextTimeFreeCurseItem=TRUE
- [x] 下次脱装备时可脱下诅咒装备

**技术实现要点**:
- 涉及文件: [ObjBase.pas:8088-8093](Source/M2Server/ObjBase.pas#L8088-L8093)
- 关键标志: `BoNextTimeFreeCurseItem`

**优先级**: P1
**复杂度**: S
**状态**: 已实现

---

### US-004: 使用回城卷传送

**作为** 玩家
**我想要** 使用回城卷传送到安全区域
**以便** 快速返回城镇

**验收标准**:
- [x] StdMode=3, Shape=3为回城卷
- [x] 普通玩家传送到HomeMap(HomeX, HomeY)
- [x] 红名玩家(PKLevel>=2)传送到红名村
- [x] 活动玩家无法使用

**技术实现要点**:
- 涉及文件: [ObjBase.pas:3660-3671](Source/M2Server/ObjBase.pas#L3660-L3671)
- 关键方法: `UserSpaceMove`
- 常量: `BADMANHOMEMAP`, `BADMANSTARTX`, `BADMANSTARTY`

**优先级**: P0
**复杂度**: S
**状态**: 已实现

---

### US-005: 使用随机传送卷

**作为** 玩家
**我想要** 使用随机传送卷在地图内随机传送
**以便** 逃离危险或探索地图

**验收标准**:
- [x] StdMode=3, Shape=2为地牢传送卷
- [x] 在当前地图内随机传送
- [x] 禁止随机移动的地图无法使用
- [x] 攻城战内城有10秒冷却时间

**技术实现要点**:
- 涉及文件: [ObjBase.pas:3637-3658](Source/M2Server/ObjBase.pas#L3637-L3658)
- 冷却检查: `LatestSpaceScrollTime`
- 地图限制: `PEnvir.NoRandomMove`

**优先级**: P0
**复杂度**: M
**状态**: 已实现

---

### US-006: 使用祝福油增加幸运

**作为** 玩家
**我想要** 使用祝福油给武器增加幸运
**以便** 提升武器的攻击效果

**验收标准**:
- [x] StdMode=3, Shape=4为祝福油
- [x] 1/20概率受诅咒(Desc[4]+1)
- [x] 先消除诅咒，再增加幸运(Desc[3])
- [x] 幸运1-3较易获得，3-7需要更高概率

**技术实现要点**:
- 涉及文件: [ObjBase.pas:3729-3800](Source/M2Server/ObjBase.pas#L3729-L3800)
- 关键方法: `MakeWeaponGoodLock`

**优先级**: P1
**复杂度**: M
**状态**: 已实现

---

### US-007: 学习技能书

**作为** 玩家
**我想要** 使用技能书学习新魔法
**以便** 获得新的战斗技能

**验收标准**:
- [x] StdMode=4的物品为技能书
- [x] 检查是否已学会该技能
- [x] 检查职业限制(Job=99为通用)
- [x] 检查等级需求(NeedLevel[0])

**技术实现要点**:
- 涉及文件: [ObjBase.pas:8200-8229](Source/M2Server/ObjBase.pas#L8200-L8229)
- 关键方法: `ReadBook`, `IsMyMagic`

**优先级**: P0
**复杂度**: M
**状态**: 已实现

---

### US-008: 丢弃物品到地上

**作为** 玩家
**我想要** 将背包中的物品丢弃到地上
**以便** 清理背包空间或与他人分享

**验收标准**:
- [x] 物品从背包移除
- [x] 物品显示在地图上
- [x] 活动物品不能丢弃
- [x] 交易窗口关闭3秒内不能丢弃

**技术实现要点**:
- 涉及文件: [ObjBase.pas:7874-7906](Source/M2Server/ObjBase.pas#L7874-L7906)
- 关键方法: `UserDropItem`, `DropItemDown`

**优先级**: P0
**复杂度**: S
**状态**: 已实现

---

### US-009: 拾取地上物品

**作为** 玩家
**我想要** 拾取地上的物品
**以便** 获得掉落的战利品

**验收标准**:
- [x] 检查拾取权(ownership)
- [x] 超时后取消拾取权限制
- [x] 组队成员可共享拾取权
- [x] 检查背包容量和重量

**技术实现要点**:
- 涉及文件: [ObjBase.pas:7929-8049](Source/M2Server/ObjBase.pas#L7929-L8049)
- 关键方法: `PickUp`
- 超时常量: `ANTI_MUKJA_DELAY`

**优先级**: P0
**复杂度**: M
**状态**: 已实现

---

### US-010: 从NPC购买物品

**作为** 玩家
**我想要** 从NPC商店购买物品
**以便** 获取所需的消耗品

**验收标准**:
- [x] 检查金币是否充足
- [x] 检查背包重量是否足够
- [x] 城堡内商店收取5%税金
- [x] 药品类物品新建实例

**技术实现要点**:
- 涉及文件: [ObjNpc.pas:3132-3181](Source/M2Server/ObjNpc.pas#L3132-L3181)
- 关键方法: `UserBuyItem`

**优先级**: P0
**复杂度**: M
**状态**: 已实现

---

### US-011: 向NPC出售物品

**作为** 玩家
**我想要** 将物品出售给NPC
**以便** 获得金币

**验收标准**:
- [x] 计算出售价格(购买价格的一半)
- [x] 符纸/蜡烛耐久度<4000不可出售
- [x] 城堡内商店收取5%税金
- [x] 物品添加到商店库存

**技术实现要点**:
- 涉及文件: [ObjNpc.pas:2999-3043](Source/M2Server/ObjNpc.pas#L2999-L3043)
- 关键方法: `UserSellItem`, `GetBuyPrice`

**优先级**: P0
**复杂度**: M
**状态**: 已实现

---

### US-012: 符纸消耗(道士施法)

**作为** 道士玩家
**我想要** 施法时自动消耗符纸
**以便** 释放召唤和治疗技能

**验收标准**:
- [x] StdMode=25, Shape=5为符纸
- [x] 检查护身符位或左手镯位
- [x] 每次施法消耗100点耐久
- [x] 耐久度<100时符纸消失

**技术实现要点**:
- 涉及文件: [Magic.pas:697-750](Source/M2Server/Magic.pas#L697-L750)
- 检查位置: `U_BUJUK`, `U_ARMRINGL`

**优先级**: P0
**复杂度**: M
**状态**: 已实现

---

### US-013: 使用捆绑药品

**作为** 玩家
**我想要** 使用捆绑药品拆分为单个药品
**以便** 批量购买后分开使用

**验收标准**:
- [x] StdMode=31为捆绑药品
- [x] 使用后拆分为6个单独药品
- [x] 检查背包容量(需要5个空位)

**技术实现要点**:
- 涉及文件: [ObjBase.pas:14998-15006](Source/M2Server/ObjBase.pas#L14998-L15006)
- 关键方法: `UnbindPotionUnit`

**优先级**: P1
**复杂度**: S
**状态**: 已实现

---

### US-014: 使用能力提升药水

**作为** 玩家
**我想要** 使用能力提升药水临时提升属性
**以便** 在战斗中获得优势

**验收标准**:
- [x] StdMode=3, Shape=INSTANTABILUP_DRUG
- [x] DC/MC/SC/攻速/HP/MP可提升
- [x] 持续时间由MAC高字节决定(秒)
- [x] 效果结束后自动重算属性

**技术实现要点**:
- 涉及文件: [ObjBase.pas:8119-8163](Source/M2Server/ObjBase.pas#L8119-L8163)
- 数据结构: `ExtraAbil[]`, `ExtraAbilTimes[]`

**优先级**: P1
**复杂度**: M
**状态**: 已实现

---

### US-015: 使用经验药水

**作为** 玩家
**我想要** 使用经验药水获得经验值
**以便** 快速升级

**验收标准**:
- [x] StdMode=3, Shape=INSTANT_EXP_DRUG
- [x] 获得经验值 = AC低字节 × 100

**技术实现要点**:
- 涉及文件: [ObjBase.pas:8165-8169](Source/M2Server/ObjBase.pas#L8165-L8169)
- 关键方法: `WinExp`

**优先级**: P2
**复杂度**: S
**状态**: 已实现

---

### US-016: 使用修复油修复武器

**作为** 玩家
**我想要** 使用修复油修复武器耐久
**以便** 不用去NPC修理

**验收标准**:
- [x] StdMode=3, Shape=9为普通修复油
- [x] StdMode=3, Shape=10为武神油(特殊修复)
- [x] 需要穿戴武器

**技术实现要点**:
- 涉及文件: [ObjBase.pas:3698-3710](Source/M2Server/ObjBase.pas#L3698-L3710)
- 关键方法: `RepaireWeaponNormaly`, `RepaireWeaponPerfect`

**优先级**: P1
**复杂度**: S
**状态**: 已实现

---

### US-017: 使用彩票抽奖

**作为** 玩家
**我想要** 使用彩票进行抽奖
**以便** 获得随机奖励

**验收标准**:
- [x] StdMode=3, Shape=11为彩票
- [x] 调用UseLotto进行抽奖

**技术实现要点**:
- 涉及文件: [ObjBase.pas:3712-3717](Source/M2Server/ObjBase.pas#L3712-L3717)
- 关键方法: `UseLotto`

**优先级**: P2
**复杂度**: S
**状态**: 已实现

---

### US-018: 使用回城传书传送到城堡

**作为** 行会成员
**我想要** 使用回城传书传送到行会城堡
**以便** 快速参与攻城战

**验收标准**:
- [x] StdMode=3, Shape=5为回城传书
- [x] 需要行会占领城堡
- [x] 自由PK区域无法使用

**技术实现要点**:
- 涉及文件: [ObjBase.pas:3680-3696](Source/M2Server/ObjBase.pas#L3680-L3696)
- 检查: `UserCastle.IsOurCastle`

**优先级**: P1
**复杂度**: S
**状态**: 已实现

---

### US-019: 肉类掉落品质下降

**作为** 系统
**我想要** 肉类物品掉落时品质下降
**以便** 平衡游戏经济

**验收标准**:
- [x] StdMode=40的物品掉落时Dura-2000
- [x] 耐久度最低为0

**技术实现要点**:
- 涉及文件: [ObjBase.pas:7765-7770](Source/M2Server/ObjBase.pas#L7765-L7770)

**优先级**: P2
**复杂度**: S
**状态**: 已实现

---

### US-020: 骰子随机外观

**作为** 系统
**我想要** 骰子类物品显示随机外观
**以便** 增加游戏趣味性

**验收标准**:
- [x] StdMode=45的物品为骰子/彩票类
- [x] 掉落时随机生成Looks

**技术实现要点**:
- 涉及文件: [ObjBase.pas:7777-7779](Source/M2Server/ObjBase.pas#L7777-L7779)
- 关键方法: `GetRandomLook`

**优先级**: P2
**复杂度**: S
**状态**: 已实现

---

## 物品效果计算公式

### 药品恢复公式

```
普通药品:
  IncHealth += std.AC  (上限500)
  IncSpell += std.MAC  (上限500)

仙水(瞬间恢复):
  HP += std.AC
  MP += std.MAC
```

> 代码位置: [ObjBase.pas:8078-8106](Source/M2Server/ObjBase.pas#L8078-L8106)

### 祝福油幸运公式

```
诅咒概率: 1/20
幸运增加概率:
  - 幸运0->1: 100%
  - 幸运1->2: 1/6
  - 幸运2->3: 1/6
  - 幸运3->7: 1/30

难度修正: difficulty = (MaxDC - MinDC) / 5
```

> 代码位置: [ObjBase.pas:3729-3800](Source/M2Server/ObjBase.pas#L3729-L3800)

### 能力提升药水公式

```
持续时间 = Hibyte(std.MAC) 秒
DC提升 = Lobyte(std.DC)
MC提升 = Lobyte(std.MC)
SC提升 = Lobyte(std.SC)
攻速提升 = Hibyte(std.AC)
HP提升 = Lobyte(std.AC)
MP提升 = Lobyte(std.MAC)
```

> 代码位置: [ObjBase.pas:8119-8163](Source/M2Server/ObjBase.pas#L8119-L8163)

---

## 边界场景处理

### 物品使用失败场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 禁药地图使用药品 | 提示"这个地图不能使用药品" | ObjBase.pas:8074 |
| 活动玩家使用传送卷 | 提示"无法使用" | ObjBase.pas:3630 |
| 禁止随机移动地图 | 传送失败 | ObjBase.pas:3639 |
| 攻城战内城冷却中 | 提示剩余冷却时间 | ObjBase.pas:3648 |

### 背包容量场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 背包已满无法拾取 | 拾取失败 | ObjBase.pas:7994 |
| 重量超限无法拾取 | 拾取失败 | ObjBase.pas:7999 |
| 捆绑药品拆分空间不足 | 使用失败 | ObjBase.pas:15000 |

### 交易相关场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 交易中无法拾取 | 拾取失败 | ObjBase.pas:7965 |
| 交易窗口关闭3秒内丢弃 | 丢弃失败 | ObjBase.pas:7884 |
| 活动物品不能丢弃 | 丢弃失败 | ObjBase.pas:7890 |

### NPC交易场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 金币不足购买 | 购买失败 | ObjNpc.pas:3157 |
| 符纸耐久<4000出售 | 出售失败 | ObjNpc.pas:3008 |
| 金币超过上限 | 出售失败 | ObjNpc.pas:3039 |

---

## 文档统计

| 项目 | 数量 |
|------|------|
| Epic 级别故事 | 6 |
| 详细用户故事 | 20 |
| 已实现功能 | 20 |
| 缺失功能 | 0 |

---

*文档生成完成*
