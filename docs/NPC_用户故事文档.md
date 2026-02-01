# NPC系统用户故事文档

> 基于 mir2 项目代码分析生成
> 生成日期: 2026-01-30

---

## 目录

1. [NPC系统概述](#npc系统概述)
2. [NPC类型分类表](#npc类型分类表)
3. [核心数据结构](#核心数据结构)
4. [Epic 级别用户故事](#epic-级别用户故事)
5. [详细用户故事](#详细用户故事)
6. [脚本命令参考表](#脚本命令参考表)
7. [边界场景处理](#边界场景处理)

---

## NPC系统概述

mir2 NPC系统是游戏核心交互系统之一，涵盖商人交易、物品修理、武器升级、行会管理、城堡管理、任务对话、脚本执行等功能模块。NPC通过脚本文件定义对话内容和交互逻辑，支持条件判断、变量操作、物品交换等复杂功能。

### 核心文件索引

| 文件 | 路径 | 职责 |
|------|------|------|
| Grobal2.pas | Source/Common/Grobal2.pas | 通用数据结构定义(TUserItem、TStdItem等) |
| ObjNpc.pas | Source/M2Server/ObjNpc.pas | NPC类定义(TNormNpc、TMerchant、TGuildOfficial、TCastleManager) |
| LocalDB.pas | Source/M2Server/LocalDB.pas | NPC配置加载、脚本解析、商品数据管理 |
| Guild.pas | Source/M2Server/Guild.pas | 行会系统管理(创建、战争、联盟) |
| Castle.pas | Source/M2Server/Castle.pas | 城堡系统管理(攻城战、税收、防御) |
| ObjBase.pas | Source/M2Server/ObjBase.pas | 生物基类、玩家类定义 |

### 配置文件索引

| 文件/目录 | 路径 | 用途 |
|-----------|------|------|
| Npcs.txt | Envir/Npcs.txt | NPC列表配置 |
| Merchant.txt | Envir/Merchant.txt | 商人列表配置 |
| Npc_def/ | Envir/Npc_def/ | NPC脚本定义目录 |
| Market_Def/ | Envir/Market_Def/ | 商店商品定义目录 |
| Market_Prices/ | Envir/Market_Prices/ | 商店价格存储目录 |
| Market_Saved/ | Envir/Market_Saved/ | 商店库存存储目录 |
| GuardList.txt | Envir/GuardList.txt | 守卫列表配置 |

---

## NPC类型分类表

### 按功能分类

| NPC类型 | 类名 | 配置类型值 | 主要功能 |
|---------|------|------------|----------|
| 基础NPC | TNormNpc | - | 对话、任务触发 |
| 商人 | TMerchant | 0 | 买卖、修理、制药、升级 |
| 行会官员 | TGuildOfficial | 1 | 创建行会、宣战、捐献 |
| 城堡管理员 | TCastleManager | 2 | 城堡修复、雇佣守卫 |
| 训练师 | TTrainer | - | 伤害测试 |

> 代码位置: [ObjNpc.pas:118-349](Source/M2Server/ObjNpc.pas#L118-L349)

### 按脚本功能分类

| 功能标签 | 脚本标识 | 说明 |
|----------|----------|------|
| 购买物品 | @buy | 打开购买界面 |
| 出售物品 | @sell | 打开出售界面 |
| 存储物品 | @storage | 打开仓库存储界面 |
| 取回物品 | @getback | 打开仓库取回界面 |
| 修理物品 | @repair | 打开修理界面 |
| 制作药品 | @makedrug | 打开制药界面 |
| 武器升级 | @upgradenow | 打开武器升级界面 |

> 代码位置: [ObjNpc.pas:442-454](Source/M2Server/ObjNpc.pas#L442-L454)

### NPC类继承关系

```
TCreature (生物基类)
    └── TAnimal (动物类)
            └── TNormNpc (基础NPC)
                    ├── TMerchant (商人)
                    │       └── TCastleManager (城堡管理员)
                    ├── TGuildOfficial (行会官员)
                    └── TTrainer (训练师)
```

---

## 核心数据结构

### TUpgradeInfo - 武器升级信息

```pascal
TUpgradeInfo = record
   UserName: string[14];    // 用户名
   uitem: TUserItem;        // 用户物品信息
   updc: byte;              // 攻击力升级点数
   upsc: byte;              // 道术升级点数
   upmc: byte;              // 魔法升级点数
   durapoint: byte;         // 耐久度点数
   readydate: TDateTime;    // 完成日期
   readycount: longword;    // 完成计数(GetTickCount)
end;
```

> 代码位置: [ObjNpc.pas:40-50](Source/M2Server/ObjNpc.pas#L40-L50)

### TQuestRequire - 任务需求条件

```pascal
TQuestRequire = record
   RandomCount: integer;    // 随机计数(>0时进行随机判断)
   CheckIndex: word;        // 检查索引(任务标记索引)
   CheckValue: byte;        // 检查值(0或1)
end;
```

> 代码位置: [ObjNpc.pas:54-58](Source/M2Server/ObjNpc.pas#L54-L58)

### TQuestConditionInfo - 任务条件信息

```pascal
TQuestConditionInfo = record
   IfIdent: integer;        // 条件标识(QI_CHECK等)
   IfParam: string;         // 条件参数
   IfParamVal: integer;     // 条件参数值
   IfTag: string;           // 条件标签
   IfTagVal: integer;       // 条件标签值
end;
```

> 代码位置: [ObjNpc.pas:75-82](Source/M2Server/ObjNpc.pas#L75-L82)

### TQuestActionInfo - 任务动作信息

```pascal
TQuestActionInfo = record
   ActIdent: integer;       // 动作标识(QA_SET等)
   ActParam: string;        // 动作参数
   ActParamVal: integer;    // 动作参数值
   ActTag: string;          // 动作标签
   ActTagVal: integer;      // 动作标签值
   ActExtra: string;        // 额外参数
   ActExtraVal: integer;    // 额外参数值
end;
```

> 代码位置: [ObjNpc.pas:62-71](Source/M2Server/ObjNpc.pas#L62-L71)

### TSayingProcedure - 对话过程

```pascal
TSayingProcedure = record
   ConditionList: TList;    // 条件列表(PTQuestConditionInfo)
   ActionList: TList;       // 动作列表(条件为真时执行)
   Saying: string;          // 对话内容(条件为真时显示)
   ElseActionList: TList;   // 否则动作列表(条件为假时执行)
   ElseSaying: string;      // 否则对话内容(条件为假时显示)
   AvailableCommands: TStringList;  // 可用命令列表
end;
```

> 代码位置: [ObjNpc.pas:86-94](Source/M2Server/ObjNpc.pas#L86-L94)

### TSayingRecord - 对话记录

```pascal
TSayingRecord = record
   Title: string;           // 对话标题(如@main)
   Procs: TList;            // 处理过程列表(PTSayingProcedure)
end;
```

> 代码位置: [ObjNpc.pas:98-102](Source/M2Server/ObjNpc.pas#L98-L102)

### TQuestRecord - 任务记录

```pascal
TQuestRecord = record
   BoRequire: Boolean;      // 是否有需求条件
   LocalNumber: integer;    // 本地编号(用于GOTO跳转)
   QuestRequireArr: array[0..9] of TQuestRequire;  // 任务需求条件数组
   SayingList: TList;       // 对话列表(PTSayingRecord)
end;
```

> 代码位置: [ObjNpc.pas:106-112](Source/M2Server/ObjNpc.pas#L106-L112)

### TNormNpc 核心属性

| 属性 | 类型 | 说明 |
|------|------|------|
| NpcFace | byte | NPC脸型(聊天窗口头像) |
| Sayings | TList | 对话列表(PTQuestRecord) |
| DefineDirectory | string | 脚本定义目录 |
| BoInvisible | Boolean | 是否不可见 |
| CanSell | Boolean | 是否可出售物品 |
| CanBuy | Boolean | 是否可购买物品 |
| CanStorage | Boolean | 是否可存储物品 |
| CanRepair | Boolean | 是否可修理物品 |
| CanMakeDrug | Boolean | 是否可制药 |
| CanUpgrade | Boolean | 是否可升级武器 |

> 代码位置: [ObjNpc.pas:118-132](Source/M2Server/ObjNpc.pas#L118-L132)

### TMerchant 核心属性

| 属性 | 类型 | 说明 |
|------|------|------|
| MarketName | string | 市场名称 |
| MarketType | byte | 市场类型 |
| PriceRate | integer | 物价比率(100为正常) |
| NoSeal | Boolean | 是否不出售物品 |
| BoCastleManage | Boolean | 是否城堡管理商店 |
| DealGoods | TList | 经营的物品类型列表 |
| ProductList | TList | 生产的物品列表 |
| GoodsList | TList | 当前出售的商品列表 |
| UpgradingList | TList | 升级中的物品列表 |

> 代码位置: [ObjNpc.pas:172-214](Source/M2Server/ObjNpc.pas#L172-L214)

---

## Epic 级别用户故事

### Epic 1: NPC对话系统

**描述**: 玩家点击NPC时触发对话，NPC根据脚本文件显示对话内容，支持条件判断和分支选择。

**核心方法**:
- `UserCall` - [ObjNpc.pas:146](Source/M2Server/ObjNpc.pas#L146) - 用户调用NPC
- `UserSelect` - [ObjNpc.pas:149](Source/M2Server/ObjNpc.pas#L149) - 用户选择菜单
- `NpcSayTitle` - [ObjNpc.pas:637-815](Source/M2Server/ObjNpc.pas#L637-L815) - 按标题显示对话
- `CheckSayingCondition` - [ObjNpc.pas:902-1189](Source/M2Server/ObjNpc.pas#L902-L1189) - 检查对话条件
- `DoActionList` - [ObjNpc.pas:1453-1700](Source/M2Server/ObjNpc.pas#L1453-L1700) - 执行动作列表

---

### Epic 2: 商人交易系统

**描述**: 玩家与商人NPC进行物品买卖，包括价格计算、库存管理、城堡税收等功能。

**核心方法**:
- `UserBuyItem` - [ObjNpc.pas:266](Source/M2Server/ObjNpc.pas#L266) - 用户购买物品
- `UserSellItem` - [ObjNpc.pas:233](Source/M2Server/ObjNpc.pas#L233) - 用户出售物品
- `GetGoodsPrice` - [ObjNpc.pas:188](Source/M2Server/ObjNpc.pas#L188) - 获取商品价格
- `GetSellPrice` - [ObjNpc.pas:2250-2263](Source/M2Server/ObjNpc.pas#L2250-L2263) - 获取售价(含城堡折扣)
- `RefillGoods` - [ObjNpc.pas:2283-2371](Source/M2Server/ObjNpc.pas#L2283-L2371) - 补充商品库存

---

### Epic 3: 物品修理系统

**描述**: 玩家将损坏的装备交给商人修理，恢复耐久度，支持普通修理和特殊修理。

**核心方法**:
- `QueryRepairCost` - [ObjNpc.pas:236](Source/M2Server/ObjNpc.pas#L236) - 查询修理费用
- `UserRepairItem` - [ObjNpc.pas:240](Source/M2Server/ObjNpc.pas#L240) - 用户修理物品

---

### Epic 4: 武器升级系统

**描述**: 玩家将武器和材料交给商人进行升级，升级需要时间，结果有成功/失败概率。

**核心方法**:
- `UserSelectUpgradeWeapon` - [ObjNpc.pas:2459-2631](Source/M2Server/ObjNpc.pas#L2459-L2631) - 提交武器升级
- `UserSelectGetBackUpgrade` - [ObjNpc.pas:2633-2700](Source/M2Server/ObjNpc.pas#L2633-L2700) - 领取升级武器
- `PrepareWeaponUpgrade` - [ObjNpc.pas:2460-2568](Source/M2Server/ObjNpc.pas#L2460-L2568) - 准备升级材料
- `VerifyUpgradeList` - [ObjNpc.pas:2423-2457](Source/M2Server/ObjNpc.pas#L2423-L2457) - 清理过期升级

---

### Epic 5: 行会管理系统

**描述**: 玩家通过行会官员NPC创建行会、宣战、解散行会、捐献金币等。

**核心方法**:
- `UserBuildGuildNow` - [ObjNpc.pas:293](Source/M2Server/ObjNpc.pas#L293) - 创建行会
- `UserDeclareGuildWarNow` - [ObjNpc.pas:289](Source/M2Server/ObjNpc.pas#L289) - 宣战
- `UserFreeGuild` - [ObjNpc.pas:297](Source/M2Server/ObjNpc.pas#L297) - 解散行会
- `UserDonateGold` - [ObjNpc.pas:300](Source/M2Server/ObjNpc.pas#L300) - 捐献金币
- `UserRequestCastleWar` - [ObjNpc.pas:303](Source/M2Server/ObjNpc.pas#L303) - 申请攻城战

---

### Epic 6: 城堡管理系统

**描述**: 城堡拥有者通过城堡管理员NPC修复城门/城墙、雇佣守卫/弓箭手。

**核心方法**:
- `RepaireCastlesMainDoor` - [ObjNpc.pas:334](Source/M2Server/ObjNpc.pas#L334) - 修复城堡主门
- `RepaireCoreCastleWall` - [ObjNpc.pas:337](Source/M2Server/ObjNpc.pas#L337) - 修复核心城墙
- `HireCastleGuard` - [ObjNpc.pas:340](Source/M2Server/ObjNpc.pas#L340) - 雇佣城堡守卫
- `HireCastleArcher` - [ObjNpc.pas:343](Source/M2Server/ObjNpc.pas#L343) - 雇佣城堡弓箭手

---

## 详细用户故事

### US-NPC-001: 玩家点击NPC触发对话

**作为** 玩家
**我想要** 点击NPC时显示对话窗口
**以便于** 与NPC进行交互

**验收标准**:
1. 点击NPC后显示对话窗口，显示NPC头像和名称
2. 对话内容从脚本文件的`@main`标题加载
3. 对话中的变量标签被替换为实际值(如`<$USERNAME>`)
4. 可点击的选项以链接形式显示

**技术实现要点**:
- 入口方法: `TNormNpc.UserCall` [ObjNpc.pas:146](Source/M2Server/ObjNpc.pas#L146)
- 对话显示: `NpcSay` [ObjNpc.pas:495-499](Source/M2Server/ObjNpc.pas#L495-L499)
- 标签替换: `CheckNpcSayCommand` [ObjNpc.pas:533-588](Source/M2Server/ObjNpc.pas#L533-L588)

**优先级**: P0 | **复杂度**: 中

---

### US-NPC-002: 玩家选择对话选项

**作为** 玩家
**我想要** 点击对话中的选项
**以便于** 触发相应的功能或跳转到其他对话

**验收标准**:
1. 点击选项后执行对应的脚本动作
2. 支持跳转到其他对话标题(如`@guildwar`)
3. 支持触发功能(如`@buy`打开购买界面)
4. 条件不满足时显示else分支内容

**技术实现要点**:
- 入口方法: `TNormNpc.UserSelect` [ObjNpc.pas:149](Source/M2Server/ObjNpc.pas#L149)
- 条件检查: `CheckSayingCondition` [ObjNpc.pas:902-1189](Source/M2Server/ObjNpc.pas#L902-L1189)
- 动作执行: `DoActionList` [ObjNpc.pas:1453-1700](Source/M2Server/ObjNpc.pas#L1453-L1700)

**优先级**: P0 | **复杂度**: 高

---

### US-NPC-003: 脚本条件判断

**作为** 游戏系统
**我想要** 根据玩家状态判断脚本条件
**以便于** 实现分支对话和任务逻辑

**验收标准**:
1. 支持检查任务标记(#IF CHECK)
2. 支持检查物品(#IF CHECKITEM)
3. 支持检查金币(#IF CHECKGOLD)
4. 支持检查等级(#IF CHECKLEVEL)
5. 支持检查职业(#IF CHECKJOB)
6. 支持随机条件(#IF RANDOM)

**技术实现要点**:
- 条件类型常量: QI_CHECK, QI_CHECKITEM, QI_CHECKGOLD等
- 条件检查: `CheckSayingCondition` [ObjNpc.pas:902-1189](Source/M2Server/ObjNpc.pas#L902-L1189)

**支持的条件类型**:

| 条件标识 | 说明 | 参数格式 |
|----------|------|----------|
| QI_CHECK | 检查任务标记 | CHECK 索引 值 |
| QI_CHECKITEM | 检查背包物品 | CHECKITEM 物品名 数量 |
| QI_CHECKITEMW | 检查装备物品 | CHECKITEMW [位置] |
| QI_CHECKGOLD | 检查金币 | CHECKGOLD 数量 |
| QI_CHECKLEVEL | 检查等级 | CHECKLEVEL 等级 |
| QI_CHECKJOB | 检查职业 | CHECKJOB Warrior/Wizard/Taoist |
| QI_GENDER | 检查性别 | GENDER MAN/WOMAN |
| QI_RANDOM | 随机条件 | RANDOM 概率 |
| QI_DAYTIME | 检查时间 | DAYTIME DAY/NIGHT/SUNRAISE/SUNSET |

**优先级**: P0 | **复杂度**: 高

---

### US-NPC-004: 脚本动作执行

**作为** 游戏系统
**我想要** 执行脚本中定义的动作
**以便于** 实现物品交换、传送等功能

**验收标准**:
1. 支持设置任务标记(#ACT SET)
2. 支持获取物品(#ACT TAKE)
3. 支持给予物品(#ACT GIVE)
4. 支持地图传送(#ACT MAPMOVE)
5. 支持生成怪物(#ACT MONGEN)

**技术实现要点**:
- 动作执行: `DoActionList` [ObjNpc.pas:1453-1700](Source/M2Server/ObjNpc.pas#L1453-L1700)
- 获取物品: `TakeItemFromUser` [ObjNpc.pas:1216-1261](Source/M2Server/ObjNpc.pas#L1216-L1261)
- 给予物品: `GiveItemToUser` [ObjNpc.pas:1379-1442](Source/M2Server/ObjNpc.pas#L1379-L1442)

**支持的动作类型**:

| 动作标识 | 说明 | 参数格式 |
|----------|------|----------|
| QA_SET | 设置任务标记 | SET 索引 值 |
| QA_TAKE | 获取物品 | TAKE 物品名 数量 |
| QA_TAKEW | 获取装备 | TAKEW [位置] |
| QA_GIVE | 给予物品 | GIVE 物品名 数量 |
| QA_MAPMOVE | 地图传送 | MAPMOVE 地图 X Y |
| QA_MAPRANDOM | 随机传送 | MAPRANDOM 地图 |
| QA_MONGEN | 生成怪物 | MONGEN 怪物名 数量 范围 |
| QA_CLOSE | 关闭对话 | CLOSE |
| QA_BREAK | 中断执行 | BREAK |

**优先级**: P0 | **复杂度**: 高

---

### US-NPC-005: 玩家购买物品

**作为** 玩家
**我想要** 从商人处购买物品
**以便于** 获得所需的装备和消耗品

**验收标准**:
1. 打开购买界面显示商人出售的物品列表
2. 物品价格根据PriceRate计算
3. 城堡行会成员享受8折优惠
4. 购买成功后扣除金币，物品加入背包
5. 背包已满时提示无法购买

**技术实现要点**:
- 购买方法: `TMerchant.UserBuyItem` [ObjNpc.pas:266](Source/M2Server/ObjNpc.pas#L266)
- 价格计算: `GetSellPrice` [ObjNpc.pas:2250-2263](Source/M2Server/ObjNpc.pas#L2250-L2263)
- 城堡折扣: 城堡行会成员价格 = 原价 × PriceRate × 0.8

**优先级**: P0 | **复杂度**: 中

---

### US-NPC-006: 玩家出售物品

**作为** 玩家
**我想要** 将物品出售给商人
**以便于** 获得金币

**验收标准**:
1. 打开出售界面显示背包物品
2. 只能出售商人经营类型的物品
3. 出售价格为原价的50%
4. 物品耐久度影响出售价格
5. 升级属性增加出售价格

**技术实现要点**:
- 出售方法: `TMerchant.UserSellItem` [ObjNpc.pas:233](Source/M2Server/ObjNpc.pas#L233)
- 收购价格: `GetBuyPrice` [ObjNpc.pas:2265-2268](Source/M2Server/ObjNpc.pas#L2265-L2268) - 原价50%
- 价格计算: `GetGoodsPrice` [ObjNpc.pas:2200-2246](Source/M2Server/ObjNpc.pas#L2200-L2246)

**优先级**: P0 | **复杂度**: 中

---

### US-NPC-007: 商品库存管理

**作为** 游戏系统
**我想要** 自动管理商人的商品库存
**以便于** 保持商品供应平衡

**验收标准**:
1. 定时检查商品库存数量
2. 库存不足时自动补货并涨价
3. 库存过多时自动清理
4. 非本店商品超过1000个时清理
5. 本店商品超过5000个时清理

**技术实现要点**:
- 补货方法: `TMerchant.RefillGoods` [ObjNpc.pas:2283-2371](Source/M2Server/ObjNpc.pas#L2283-L2371)
- 涨价方法: `PriceUp` [ObjNpc.pas:252](Source/M2Server/ObjNpc.pas#L252)
- 降价方法: `PriceDown` [ObjNpc.pas:249](Source/M2Server/ObjNpc.pas#L249)

**优先级**: P1 | **复杂度**: 中

---

### US-NPC-008: 玩家修理物品

**作为** 玩家
**我想要** 将损坏的装备交给商人修理
**以便于** 恢复装备的耐久度

**验收标准**:
1. 打开修理界面显示可修理的装备
2. 显示修理费用(根据损坏程度计算)
3. 修理后耐久度恢复到最大值
4. 扣除相应金币

**技术实现要点**:
- 查询费用: `TMerchant.QueryRepairCost` [ObjNpc.pas:236](Source/M2Server/ObjNpc.pas#L236)
- 修理方法: `TMerchant.UserRepairItem` [ObjNpc.pas:240](Source/M2Server/ObjNpc.pas#L240)

**优先级**: P0 | **复杂度**: 低

---

### US-NPC-009: 玩家提交武器升级

**作为** 玩家
**我想要** 将武器和材料交给商人升级
**以便于** 提升武器属性

**验收标准**:
1. 需要装备武器且携带黑铁矿石
2. 支付升级费用(10000金币)
3. 武器和材料被收取
4. 升级需要1小时完成
5. 同一玩家只能有一件武器在升级中

**技术实现要点**:
- 升级方法: `TMerchant.UserSelectUpgradeWeapon` [ObjNpc.pas:2459-2631](Source/M2Server/ObjNpc.pas#L2459-L2631)
- 材料处理: `PrepareWeaponUpgrade` [ObjNpc.pas:2460-2568](Source/M2Server/ObjNpc.pas#L2460-L2568)
- 升级费用: UPGRADEWEAPONFEE = 10000 [ObjNpc.pas:33](Source/M2Server/ObjNpc.pas#L33)

**优先级**: P1 | **复杂度**: 高

---

### US-NPC-010: 玩家领取升级武器

**作为** 玩家
**我想要** 领取升级完成的武器
**以便于** 使用升级后的武器

**验收标准**:
1. 升级完成后(1小时)可领取
2. 升级结果随机(成功/失败)
3. 成功时属性提升，失败时属性下降
4. 耐久度根据材料品质变化
5. 超过7天未领取的武器自动删除

**技术实现要点**:
- 领取方法: `TMerchant.UserSelectGetBackUpgrade` [ObjNpc.pas:2633-2700](Source/M2Server/ObjNpc.pas#L2633-L2700)
- 过期清理: `VerifyUpgradeList` [ObjNpc.pas:2423-2457](Source/M2Server/ObjNpc.pas#L2423-L2457)

**升级成功率计算**:
- 基础成功率: 10%
- 材料加成: 每点材料属性+7%
- 幸运加成: 武器幸运值+玩家幸运值
- 最大成功率: 85%

**优先级**: P1 | **复杂度**: 高

---

### US-NPC-011: 玩家创建行会

**作为** 玩家
**我想要** 通过行会官员创建行会
**以便于** 组建自己的行会组织

**验收标准**:
1. 需要100万金币和沃玛号角
2. 玩家必须未加入其他行会
3. 行会名称不能重复
4. 创建成功后玩家成为会长

**技术实现要点**:
- 创建方法: `TGuildOfficial.UserBuildGuildNow` [ObjNpc.pas:293](Source/M2Server/ObjNpc.pas#L293)

**优先级**: P1 | **复杂度**: 中

---

### US-NPC-012: 行会宣战

**作为** 行会会长
**我想要** 向其他行会宣战
**以便于** 进行行会战争

**验收标准**:
1. 只有会长可以宣战
2. 需要支付30000金币
3. 宣战后双方成员可自由PK
4. 战争期间击杀对方不增加PK值

**技术实现要点**:
- 宣战方法: `TGuildOfficial.UserDeclareGuildWarNow` [ObjNpc.pas:289](Source/M2Server/ObjNpc.pas#L289)
- 战争费用: GUILDWARFEE = 30000 [ObjNpc.pas:28](Source/M2Server/ObjNpc.pas#L28)

**优先级**: P2 | **复杂度**: 中

---

### US-NPC-013: 申请攻城战

**作为** 行会会长
**我想要** 申请攻城战
**以便于** 争夺沙巴克城堡

**验收标准**:
1. 需要提供攻城令牌
2. 申请成功后加入攻城名单
3. 攻城战在指定时间进行
4. 胜利行会获得城堡所有权

**技术实现要点**:
- 申请方法: `TGuildOfficial.UserRequestCastleWar` [ObjNpc.pas:303](Source/M2Server/ObjNpc.pas#L303)

**优先级**: P2 | **复杂度**: 高

---

### US-NPC-014: 修复城堡设施

**作为** 城堡拥有者
**我想要** 修复城堡的城门和城墙
**以便于** 增强城堡防御能力

**验收标准**:
1. 只有城堡行会成员可操作
2. 修复主门需要200万金币
3. 修复城墙需要50万金币
4. 修复后设施恢复满血

**技术实现要点**:
- 修复主门: `TCastleManager.RepaireCastlesMainDoor` [ObjNpc.pas:334](Source/M2Server/ObjNpc.pas#L334)
- 修复城墙: `TCastleManager.RepaireCoreCastleWall` [ObjNpc.pas:337](Source/M2Server/ObjNpc.pas#L337)
- 主门费用: CASTLEMAINDOORREPAREGOLD = 2000000 [ObjNpc.pas:29](Source/M2Server/ObjNpc.pas#L29)
- 城墙费用: CASTLECOREWALLREPAREGOLD = 500000 [ObjNpc.pas:30](Source/M2Server/ObjNpc.pas#L30)

**优先级**: P2 | **复杂度**: 中

---

### US-NPC-015: 雇佣城堡守卫

**作为** 城堡拥有者
**我想要** 雇佣守卫和弓箭手
**以便于** 增强城堡防御力量

**验收标准**:
1. 只有城堡行会成员可操作
2. 雇佣守卫需要30万金币
3. 雇佣弓箭手需要30万金币
4. 守卫在指定位置刷新

**技术实现要点**:
- 雇佣守卫: `TCastleManager.HireCastleGuard` [ObjNpc.pas:340](Source/M2Server/ObjNpc.pas#L340)
- 雇佣弓箭手: `TCastleManager.HireCastleArcher` [ObjNpc.pas:343](Source/M2Server/ObjNpc.pas#L343)
- 守卫费用: CASTLEGUARDEMPLOYFEE = 300000 [ObjNpc.pas:32](Source/M2Server/ObjNpc.pas#L32)
- 弓箭手费用: CASTLEARCHEREMPLOYFEE = 300000 [ObjNpc.pas:31](Source/M2Server/ObjNpc.pas#L31)

**优先级**: P2 | **复杂度**: 中

---

### US-NPC-016: NPC配置加载

**作为** 游戏系统
**我想要** 从配置文件加载NPC数据
**以便于** 在游戏中创建NPC实例

**验收标准**:
1. 从Npcs.txt加载NPC列表
2. 从Merchant.txt加载商人列表
3. 根据类型创建对应的NPC对象
4. 加载NPC脚本定义文件

**技术实现要点**:
- 加载NPC: `TFrmDB.LoadNpcs` [LocalDB.pas:905-960](Source/M2Server/LocalDB.pas#L905-L960)
- 加载商人: `TFrmDB.LoadMerchants` [LocalDB.pas:737-786](Source/M2Server/LocalDB.pas#L737-L786)

**优先级**: P0 | **复杂度**: 中

---

### US-NPC-017: NPC脚本解析

**作为** 游戏系统
**我想要** 解析NPC脚本文件
**以便于** 实现NPC对话和交互逻辑

**验收标准**:
1. 解析[@标题]定义对话入口
2. 解析#IF条件判断
3. 解析#ACT动作执行
4. 解析#SAY对话内容

**技术实现要点**:
- 加载脚本: `TNormNpc.LoadNpcInfos` [ObjNpc.pas:166](Source/M2Server/ObjNpc.pas#L166)
- 加载定义: `TFrmDB.LoadNpcDef` [LocalDB.pas:121](Source/M2Server/LocalDB.pas#L121)

**优先级**: P0 | **复杂度**: 高

---

### US-NPC-018: 变量标签替换

**作为** 游戏系统
**我想要** 替换对话中的变量标签
**以便于** 显示动态内容

**验收标准**:
1. 替换`<$USERNAME>`为玩家名
2. 替换`<$OWNERGUILD>`为城堡行会名
3. 替换`<$STR(Px)>`为任务参数值

**技术实现要点**:
- 标签替换: `TNormNpc.CheckNpcSayCommand` [ObjNpc.pas:533-588](Source/M2Server/ObjNpc.pas#L533-L588)

**优先级**: P1 | **复杂度**: 中

---

## 脚本命令参考表

### 条件命令(#IF)

| 命令 | 格式 | 说明 |
|------|------|------|
| CHECK | CHECK 索引 值 | 检查任务标记 |
| CHECKITEM | CHECKITEM 物品名 数量 | 检查背包物品 |
| CHECKITEMW | CHECKITEMW [位置] | 检查装备物品 |
| CHECKGOLD | CHECKGOLD 数量 | 检查金币数量 |
| CHECKLEVEL | CHECKLEVEL 等级 | 检查玩家等级 |
| CHECKJOB | CHECKJOB 职业 | 检查玩家职业 |
| GENDER | GENDER MAN/WOMAN | 检查玩家性别 |
| RANDOM | RANDOM 概率 | 随机条件 |
| DAYTIME | DAYTIME 时段 | 检查游戏时间 |
| CHECKPKPOINT | CHECKPKPOINT 点数 | 检查PK点 |
| CHECKBAGGAGE | CHECKBAGGAGE | 检查背包空间 |
| EQUAL | EQUAL Px 值 | 参数等于 |
| LARGE | LARGE Px 值 | 参数大于 |
| SMALL | SMALL Px 值 | 参数小于 |

### 动作命令(#ACT)

| 命令 | 格式 | 说明 |
|------|------|------|
| SET | SET 索引 值 | 设置任务标记 |
| TAKE | TAKE 物品名 数量 | 获取玩家物品 |
| GIVE | GIVE 物品名 数量 | 给予玩家物品 |
| MAPMOVE | MAPMOVE 地图 X Y | 传送到指定位置 |
| MONGEN | MONGEN 怪物名 数量 范围 | 生成怪物 |
| CLOSE | CLOSE | 关闭对话窗口 |
| BREAK | BREAK | 中断脚本执行 |
| MOV | MOV Px 值 | 设置参数值 |
| INC | INC Px [值] | 参数增加 |
| DEC | DEC Px [值] | 参数减少 |

### 变量标签参考表

| 标签 | 说明 | 来源 |
|------|------|------|
| `<$USERNAME>` | 玩家名称 | TNormNpc |
| `<$OWNERGUILD>` | 城堡行会名 | TNormNpc |
| `<$LORD>` | 城主名称 | TNormNpc |
| `<$GUILDWARFEE>` | 行会战争费用 | TNormNpc |
| `<$CASTLEWARDATE>` | 攻城战日期 | TNormNpc |
| `<$LISTOFWAR>` | 攻城名单 | TNormNpc |
| `<$STR(Px)>` | 任务参数(P0-P9) | TNormNpc |
| `<$STR(Gx)>` | 全局参数(G0-G9) | TNormNpc |
| `<$STR(Dx)>` | 骰子参数(D0-D9) | TNormNpc |
| `<$PRICERATE>` | 商人物价比率 | TMerchant |
| `<$UPGRADEWEAPONFEE>` | 武器升级费用 | TMerchant |
| `<$USERWEAPON>` | 玩家武器名称 | TMerchant |

### 装备位置标识

| 标识 | 说明 | 常量 |
|------|------|------|
| [NECKLACE] | 项链 | U_NECKLACE |
| [RING] | 戒指(左右) | U_RINGL/U_RINGR |
| [ARMRING] | 手镯(左右) | U_ARMRINGL/U_ARMRINGR |
| [WEAPON] | 武器 | U_WEAPON |
| [HELMET] | 头盔 | U_HELMET |
| [BUJUK] | 护身符 | U_BUJUK |
| [BELT] | 腰带 | U_BELT |
| [BOOTS] | 鞋子 | U_BOOTS |
| [CHARM] | 魅力 | U_CHARM |

---

## 边界场景处理

### 交易边界场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 背包已满无法购买 | 提示背包已满 | UserBuyItem |
| 金币不足 | 提示金币不足 | UserBuyItem |
| 商品库存不足 | 无法购买 | UserBuyItem |
| 出售非经营类物品 | 拒绝收购 | IsDealingItem |
| 城堡行会成员购买 | 享受8折优惠 | GetSellPrice |

### 武器升级边界场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 已有武器在升级中 | 提示等待完成 | UserSelectUpgradeWeapon |
| 未装备武器 | 无法升级 | UserSelectUpgradeWeapon |
| 未携带黑铁矿石 | 无法升级 | UserSelectUpgradeWeapon |
| 升级费用不足 | 无法升级 | UserSelectUpgradeWeapon |
| 升级未完成就领取 | 提示等待 | UserSelectGetBackUpgrade |
| 超过7天未领取 | 自动删除 | VerifyUpgradeList |

### 行会管理边界场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 已加入行会创建新行会 | 拒绝创建 | UserBuildGuildNow |
| 行会名重复 | 拒绝创建 | UserBuildGuildNow |
| 非会长宣战 | 拒绝操作 | UserDeclareGuildWarNow |
| 向自己行会宣战 | 拒绝操作 | UserDeclareGuildWarNow |

### 城堡管理边界场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 非城堡行会成员操作 | 拒绝操作 | TCastleManager.UserCall |
| 非会长存取金币 | 拒绝操作 | TCastleManager |
| 城堡金库已满 | 无法存入 | Castle.pas |
| 守卫位置已有单位 | 无法雇佣 | HireCastleGuard |

### NPC脚本边界场景

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 脚本文件不存在 | 使用默认对话 | LoadNpcInfos |
| 对话标题不存在 | 显示@main | NpcSayTitle |
| 条件检查失败 | 执行else分支 | CheckSayingCondition |
| 给予物品背包满 | 掉落地上 | GiveItemToUser |

---

## 费用常量汇总

| 常量名 | 值 | 说明 |
|--------|-----|------|
| GUILDWARFEE | 30,000 | 行会战争费用 |
| UPGRADEWEAPONFEE | 10,000 | 武器升级费用 |
| CASTLEMAINDOORREPAREGOLD | 2,000,000 | 城堡主门修复费用 |
| CASTLECOREWALLREPAREGOLD | 500,000 | 城堡城墙修复费用 |
| CASTLEGUARDEMPLOYFEE | 300,000 | 雇佣守卫费用 |
| CASTLEARCHEREMPLOYFEE | 300,000 | 雇佣弓箭手费用 |

> 代码位置: [ObjNpc.pas:28-33](Source/M2Server/ObjNpc.pas#L28-L33)

---

## 附录: NPC脚本示例

```
[@main]
欢迎来到比奇城。我是国王，有什么可以帮助你的？\
 <创建行会./@@buildguildnow>\
 <行会战争./@guildwar>\
 <询问如何创建行会./@buildguildexp>\

[@buildguildexp]
要创建行会，你首先需要证明你是否具有领导资格。\
创建行会需要100万金币和一个沃玛号角。\
 <返回/@main>

[@guildwar]
首先，你需要告诉我你要向哪个行会展开战争。\
 <输入你想要挑战的行会名称/@@guildwar>\
 <返回/@main>\
```

---

*文档生成完成*

