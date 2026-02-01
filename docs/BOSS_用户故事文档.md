# BOSS系统用户故事文档

## 1. 系统概述

BOSS系统是mir2游戏服务器的核心战斗系统之一，负责管理游戏中各类BOSS怪物的生成、AI行为、战斗机制和死亡处理。系统采用面向对象的继承架构，通过种族常量(RC_*)区分不同类型的BOSS，并为每种BOSS实现独特的AI行为和技能系统。

### 1.1 核心文件索引

| 文件路径 | 主要职责 | 关键行号 |
|---------|---------|---------|
| [Grobal2.pas](Source/Common/Grobal2.pas) | 怪物种族常量定义、数据结构 | 1700-1768, 807-850 |
| [ObjBase.pas](Source/Mir200/ObjBase.pas) | TCreature基类、属性系统、死亡处理 | 144-400, 2581-2808 |
| [ObjMon.pas](Source/Mir200/ObjMon.pas) | BOSS类定义、AI行为、特殊技能 | 1-260, 848-1749 |
| [ObjMon2.pas](Source/Mir200/ObjMon2.pas) | 特殊BOSS类(蜈蚣王、蜂后等) | 31-158 |
| [UsrEngn.pas](Source/Mir200/UsrEngn.pas) | BOSS生成管理、刷新机制 | 733-1085 |
| [LocalDB.pas](Source/Mir200/LocalDB.pas) | Monster.DB加载、掉落配置 | 169-272, 323-380 |

### 1.2 系统架构图

```
TCreature (基类)
    │
    ├── TAnimal (动物基类)
    │       │
    │       ├── TBeeQueen (蜂后)
    │       ├── TBigHeartMonster (血巨人王)
    │       ├── TStickMonster (潜伏类基类)
    │       │       └── TCentipedeKingMonster (蜈蚣王)
    │       └── TGuardUnit (守卫基类)
    │
    └── TMonster (怪物基类)
            │
            ├── TATMonster (主动攻击怪物)
            │       ├── TCowKingMonster (牛面王)
            │       ├── TMagCowMonster (魔法牛面怪)
            │       ├── TDoubleCriticalMonster (双重暴击怪)
            │       │       ├── 黑蛇王
            │       │       └── 黑天魔王
            │       └── TSkeletonSoldier (骷髅兵卒)
            │
            ├── TScultureMonster (石像怪基类)
            │       └── TScultureKingMonster (祖玛王)
            │               ├── TSkeletonKingMonster (骷髅半王)
            │               │       ├── TDeadCowKingMonster (死牛天王)
            │               │       └── TBanyaGuardMonster (般若左使/右使)
            │               └── 假祖玛王
            │
            └── TLightingZombi (雷电僵尸)
```

---

## 2. BOSS类型分类

### 2.1 按种族常量分类

| 种族常量 | 值 | BOSS名称 | 类实现 | 特殊能力 |
|---------|---|---------|-------|---------|
| RC_SCULKING | 102 | 祖玛王 | TScultureKingMonster | 石化状态、召唤小怪 |
| RC_SCULKING_2 | 122 | 假祖玛王 | TScultureKingMonster | 石化状态(不召唤) |
| RC_COWFACEKINGMON | 92 | 牛面王 | TCowKingMonster | 狂暴模式、瞬移 |
| RC_CENTIPEDEKING | 107 | 蜈蚣王 | TCentipedeKingMonster | 潜伏攻击、范围伤害 |
| RC_BEEQUEEN | 103 | 蜂后 | TBeeQueen | 召唤蜜蜂 |
| RC_BIGHEARTMON | 115 | 血巨人王 | TBigHeartMonster | 触手攻击 |
| RC_BLACKSNAKEKING | 123 | 黑蛇王 | TDoubleCriticalMonster | 双重暴击、范围攻击 |
| RC_NOBLEPIGKING | 124 | 贵猪王 | TATMonster | 普通攻击 |
| RC_FEATHERKINGOFKING | 125 | 黑天魔王 | TDoubleCriticalMonster | 双重暴击、范围攻击 |
| RC_SKELETONKING | 126 | 骷髅半王 | TSkeletonKingMonster | 远程攻击、召唤小怪 |
| RC_BANYAGUARD | 129 | 般若左使/右使 | TBanyaGuardMonster | 近远程攻击 |
| RC_DEADCOWKING | 130 | 死牛天王 | TDeadCowKingMonster | 近远程攻击、召唤小怪 |

### 2.2 按刷新机制分类

| 类型 | 特征 | 配置方式 | 示例BOSS |
|-----|------|---------|---------|
| 定时刷新BOSS | 固定时间间隔刷新 | MonGen.txt配置ZenTime | 祖玛王、牛面王 |
| 触发生成BOSS | 玩家接近时触发 | 石化状态检测 | 祖玛王(石像状态) |
| 召唤生成BOSS | 由其他BOSS召唤 | CallFollower方法 | 祖玛怪物、骷髅兵 |
| 条件召唤BOSS | 特定条件触发 | 脚本系统 | 活动BOSS |

---

## 3. 核心数据结构

### 3.1 TMonsterInfo - 怪物信息记录

**位置**: [Grobal2.pas:807-832](Source/Common/Grobal2.pas#L807-L832)

```pascal
TMonsterInfo = record
   Name: string[14];     // 怪物名称
   Race: byte;           // 种族类型(服务器AI程序标识)
   RaceImg: byte;        // 种族图像(客户端帧识别)
   Appr: word;           // 外观图像编号
   Level: byte;          // 怪物等级
   LifeAttrib: byte;     // 生命属性(0=生物, 1=亡灵)
   CoolEye: byte;        // 洞察力(看穿隐身概率)
   Exp: word;            // 经验值奖励
   HP: word;             // 生命值
   MP: word;             // 魔法值
   AC: byte;             // 物理防御
   MAC: byte;            // 魔法防御
   DC: byte;             // 最小物理攻击
   MaxDC: byte;          // 最大物理攻击
   MC: byte;             // 魔法攻击
   SC: byte;             // 道术攻击
   Speed: Byte;          // 速度/敏捷
   Hit: Byte;            // 命中率
   WalkSpeed: word;      // 移动速度(毫秒)
   WalkStep: word;       // 移动步数
   WalkWait: word;       // 移动等待时间
   AttackSpeed: word;    // 攻击速度(毫秒)
   ItemList: TList;      // 掉落物品列表
end;
```

### 3.2 TZenInfo - 刷怪点信息记录

**位置**: [Grobal2.pas:837-850](Source/Common/Grobal2.pas#L837-L850)

```pascal
TZenInfo = record
   MapName: string[14];    // 地图名称
   X: integer;             // 中心X坐标
   Y: integer;             // 中心Y坐标
   MonName: string[14];    // 怪物名称
   MonRace: integer;       // 怪物种族
   Area: integer;          // 刷新范围
   Count: integer;         // 刷新数量
   ZenTime: longword;      // 刷新间隔(毫秒)
   StartTime: longword;    // 开始时间
   Mons: TList;            // 已刷新怪物列表
   SmallZenRate: integer;  // 小规模刷新概率
end;
```

### 3.3 TMonItemInfo - 掉落物品信息

**位置**: [Grobal2.pas:855-860](Source/Common/Grobal2.pas#L855-L860)

```pascal
TMonItemInfo = record
   SelPoint: integer;   // 选择点数(概率计算)
   MaxPoint: integer;   // 最大点数
   ItemName: string;    // 物品名称
   Count: integer;      // 掉落数量
end;
```

---

## 4. Epic级别用户故事

### Epic 1: BOSS生成与刷新系统

**描述**: 作为游戏服务器，我需要管理BOSS的生成和刷新机制，确保BOSS按照配置规则在指定地点和时间出现。

**核心文件**: [UsrEngn.pas:733-1085](Source/Mir200/UsrEngn.pas#L733-L1085), [LocalDB.pas:323-380](Source/Mir200/LocalDB.pas#L323-L380)

**关键功能**:
- 从MonGen.txt加载刷怪点配置
- 定时检查并刷新BOSS
- 管理已生成BOSS列表
- 支持区域范围内随机位置生成

---

### Epic 2: BOSS AI行为系统

**描述**: 作为BOSS怪物，我需要具备智能的AI行为，包括目标搜索、仇恨管理、技能释放和阶段转换。

**核心文件**: [ObjMon.pas:268-468](Source/Mir200/ObjMon.pas#L268-L468), [ObjBase.pas:144-400](Source/Mir200/ObjBase.pas#L144-L400)

**关键功能**:
- 视野范围内目标检测
- 仇恨值计算与目标切换
- 攻击间隔和移动控制
- 特殊状态(石化、潜伏)管理

---

### Epic 3: BOSS特殊技能系统

**描述**: 作为高级BOSS，我需要拥有独特的技能系统，包括范围攻击、召唤小怪、狂暴模式等。

**核心文件**: [ObjMon.pas:848-1749](Source/Mir200/ObjMon.pas#L848-L1749), [ObjMon2.pas:31-158](Source/Mir200/ObjMon2.pas#L31-L158)

**关键功能**:
- 祖玛王召唤小怪(CallFollower)
- 牛面王狂暴模式(CrazyKingMode)
- 蜈蚣王潜伏攻击
- 骷髅半王远程攻击

---

### Epic 4: BOSS战斗与伤害系统

**描述**: 作为战斗系统，我需要处理BOSS与玩家之间的伤害计算、命中判定和状态效果。

**核心文件**: [ObjBase.pas:2000-2580](Source/Mir200/ObjBase.pas#L2000-L2580)

**关键功能**:
- 物理/魔法伤害计算
- 命中率与闪避判定
- 中毒、石化等状态效果
- 暴击伤害机制

---

### Epic 5: BOSS死亡与奖励系统

**描述**: 作为奖励系统，我需要在BOSS死亡时正确分配经验值和掉落物品给参与战斗的玩家。

**核心文件**: [ObjBase.pas:2581-2808](Source/Mir200/ObjBase.pas#L2581-L2808), [LocalDB.pas:169-219](Source/Mir200/LocalDB.pas#L169-L219)

**关键功能**:
- 经验值归属判定(ExpHiter)
- 物品掉落概率计算
- 组队经验分配
- 任务系统触发

---

### Epic 6: BOSS配置与数据管理

**描述**: 作为配置系统，我需要支持从数据库和配置文件加载BOSS属性、技能和掉落配置。

**核心文件**: [LocalDB.pas:221-272](Source/Mir200/LocalDB.pas#L221-L272), [Grobal2.pas:807-860](Source/Common/Grobal2.pas#L807-L860)

**关键功能**:
- Monster.DB数据库加载
- MonItems目录掉落配置
- 怪物属性应用(ApplyMonsterAbility)
- 物品随机升级(RandomUpgradeItem)

---

## 5. 详细用户故事

### US-BOSS-001: BOSS定时刷新

**用户故事**: 作为游戏服务器，我需要按照配置的时间间隔自动刷新BOSS，以保证玩家有持续的挑战目标。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 服务器启动时从MonGen.txt加载刷怪点配置
2. 按ZenTime间隔检查并刷新BOSS
3. 刷新位置在配置的Area范围内随机选择
4. 已存在的BOSS不会重复刷新

**技术实现要点**:
- 配置加载: [LocalDB.pas:323-380](Source/Mir200/LocalDB.pas#L323-L380) `LoadZenLists`
- 刷新逻辑: [UsrEngn.pas:1087-1099](Source/Mir200/UsrEngn.pas#L1087-L1099) `RegenMonsters`
- 位置计算: 在(X±Area, Y±Area)范围内随机

```pascal
// MonGen.txt配置格式
// 地图名 X Y "怪物名" 范围 数量 刷新时间(分钟) 小刷新率
D516    150 150 "祖玛教主" 3 1 120 0
```

---

### US-BOSS-002: BOSS种族类型创建

**用户故事**: 作为怪物生成系统，我需要根据种族常量(RC_*)创建对应类型的BOSS实例。

**优先级**: P0 (核心功能)
**复杂度**: 高

**验收标准**:
1. 根据Race值创建正确的BOSS类实例
2. 应用Monster.DB中的属性配置
3. 初始化BOSS特有的状态和技能

**技术实现要点**:
- 创建逻辑: [UsrEngn.pas:733-1020](Source/Mir200/UsrEngn.pas#L733-L1020) `AddCreature`
- 属性应用: [UsrEngn.pas:128](Source/Mir200/UsrEngn.pas#L128) `ApplyMonsterAbility`

```pascal
// 种族到类的映射示例
RC_SCULKING:      cret := TScultureKingMonster.Create;  // 祖玛王
RC_COWFACEKINGMON: cret := TCowKingMonster.Create;      // 牛面王
RC_SKELETONKING:  cret := TSkeletonKingMonster.Create;  // 骷髅半王
```

---

### US-BOSS-003: 祖玛王石化状态与唤醒

**用户故事**: 作为祖玛王，我需要以石像状态等待，当玩家接近时苏醒并开始战斗。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 初始状态为石化(BoStoneMode=TRUE)
2. 玩家进入2格范围内触发苏醒
3. 苏醒时播放RM_DIGUP动画
4. 苏醒后周围石像怪一同苏醒

**技术实现要点**:
- 类定义: [ObjMon.pas:1220-1231](Source/Mir200/ObjMon.pas#L1220-L1231)
- 苏醒逻辑: [ObjMon.pas:1239-1251](Source/Mir200/ObjMon.pas#L1239-L1251) `MeltStone`
- 状态标志: `CharStatusEx := STATE_STONE_MODE`

---

### US-BOSS-004: 祖玛王召唤小怪

**用户故事**: 作为祖玛王，当我的血量降低时，我需要召唤祖玛怪物来协助战斗。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 血量每降低20%触发一次召唤
2. 每次召唤6-12只随机祖玛怪物
3. 召唤的怪物总数不超过30只
4. 血量恢复满时重置召唤计数

**技术实现要点**:
- 召唤逻辑: [ObjMon.pas:1253-1278](Source/Mir200/ObjMon.pas#L1253-L1278) `CallFollower`
- 触发条件: `(WAbil.HP / WAbil.MaxHP * 5) < DangerLevel`
- 召唤怪物: 祖玛护卫、祖玛弓箭手、魔弓手、角蝇

---

### US-BOSS-005: 牛面王狂暴模式

**用户故事**: 作为牛面王，当我受到持续伤害时，我需要进入狂暴模式大幅提升攻击速度。

**优先级**: P1 (重要功能)
**复杂度**: 高

**验收标准**:
1. 血量降低触发蓄力阶段(8秒)
2. 蓄力期间攻击间隔增加到10秒
3. 蓄力结束进入狂暴模式(8秒)
4. 狂暴模式攻击间隔降至500ms，移动间隔400ms

**技术实现要点**:
- 类定义: [ObjMon.pas:848-857](Source/Mir200/ObjMon.pas#L848-L857)
- 狂暴逻辑: [ObjMon.pas:875-925](Source/Mir200/ObjMon.pas#L875-L925)
- 状态变量: `CrazyReadyMode`, `CrazyKingMode`

---

### US-BOSS-006: 牛面王瞬移逃脱

**用户故事**: 作为牛面王，当我被多名玩家包围时，我需要瞬移到目标身后逃脱包围。

**优先级**: P2 (增强功能)
**复杂度**: 中等

**验收标准**:
1. 被4名以上玩家包围超过30秒触发
2. 瞬移到当前目标身后位置
3. 若目标身后不可行走则随机瞬移
4. 瞬移后重置包围计时

**技术实现要点**:
- 瞬移逻辑: [ObjMon.pas:881-893](Source/Mir200/ObjMon.pas#L881-L893)
- 条件判断: `SiegeLockCount >= 5`
- 位置计算: `GetBackPosition(TargetCret, nx, ny)`

---

### US-BOSS-007: 蜈蚣王潜伏攻击

**用户故事**: 作为蜈蚣王，我需要潜伏在地下，当玩家接近时钻出地面发动攻击。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 初始状态为潜伏(HideMode=TRUE)
2. 玩家进入4格范围触发钻出
3. 目标离开6格范围后重新潜伏
4. 潜伏状态下不可被攻击

**技术实现要点**:
- 类定义: [ObjMon2.pas:44-54](Source/Mir200/ObjMon2.pas#L44-L54)
- 钻出逻辑: [ObjMon2.pas:204-208](Source/Mir200/ObjMon2.pas#L204-L208) `ComeOut`
- 范围参数: `DigupRange=4`, `DigdownRange=6`

---

### US-BOSS-008: 蜂后召唤蜜蜂

**用户故事**: 作为蜂后，当我发现敌人时，我需要持续召唤蜜蜂来攻击目标。

**优先级**: P2 (增强功能)
**复杂度**: 低

**验收标准**:
1. 发现目标后每次攻击触发召唤
2. 召唤的蜜蜂自动攻击当前目标
3. 蜜蜂总数不超过15只
4. 蜜蜂死亡后从列表移除

**技术实现要点**:
- 类定义: [ObjMon2.pas:31-42](Source/Mir200/ObjMon2.pas#L31-L42)
- 召唤逻辑: [ObjMon2.pas:305-311](Source/Mir200/ObjMon2.pas#L305-L311) `MakeChildBee`
- 延迟消息: `SendDelayMsg(self, RM_ZEN_BEE, ..., 500)`

---

### US-BOSS-009: 骷髅半王远程攻击

**用户故事**: 作为骷髅半王，我需要具备远程范围攻击能力，对扇形区域内的敌人造成伤害。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 攻击范围为前方扇形区域
2. 范围内所有敌人受到伤害
3. 连续攻击计数达到6次触发强化攻击
4. 攻击时播放RM_HIT动画

**技术实现要点**:
- 类定义: [ObjMon.pas:1701-1708](Source/Mir200/ObjMon.pas#L1701-L1708)
- 范围攻击: [ObjMon.pas:1645-1676](Source/Mir200/ObjMon.pas#L1645-L1676) `RangeAttack`
- 连击计数: `ChainShotCount := 6`

---

### US-BOSS-010: 双重暴击攻击

**用户故事**: 作为黑蛇王/黑天魔王，我需要具备双重暴击能力，对范围内敌人造成高额伤害。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 普通攻击累计5次后触发暴击
2. 10%概率随机触发暴击
3. 暴击伤害 = 基础伤害 × (MaxMP/10)
4. 暴击为扇形范围攻击

**技术实现要点**:
- 类定义: [ObjMon.pas:1576-1580](Source/Mir200/ObjMon.pas#L1576-L1580)
- 暴击逻辑: [ObjMon.pas:1623-1637](Source/Mir200/ObjMon.pas#L1623-L1637)
- 范围攻击: [ObjMon.pas:1582-1621](Source/Mir200/ObjMon.pas#L1582-L1621) `DoubleCriticalAttack`

---

### US-BOSS-011: BOSS目标搜索与仇恨

**用户故事**: 作为BOSS，我需要在视野范围内搜索敌人，并根据仇恨值选择攻击目标。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 每1-8秒执行一次目标搜索
2. 优先攻击最近的有效目标
3. 隐身玩家根据CoolEye概率被发现
4. 目标死亡或离开视野后切换目标

**技术实现要点**:
- 搜索逻辑: [ObjMon.pas:535-544](Source/Mir200/ObjMon.pas#L535-L544) `TATMonster.Run`
- 目标验证: [ObjBase.pas](Source/Mir200/ObjBase.pas) `IsProperTarget`
- 视野范围: `ViewRange`属性(默认5-8格)

---

### US-BOSS-012: BOSS死亡经验分配

**用户故事**: 作为奖励系统，当BOSS死亡时，我需要将经验值正确分配给造成最多伤害的玩家。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 经验归属于ExpHiter(最大伤害者)
2. 召唤兽击杀时经验归主人
3. 组队时触发地图任务检查
4. 经验值根据等级差计算

**技术实现要点**:
- 死亡处理: [ObjBase.pas:2581-2670](Source/Mir200/ObjBase.pas#L2581-L2670) `Die`
- 经验计算: `CalcGetExp(Level, FightExp)`
- 归属判定: `ExpHiter`, `LastHiter`

---

### US-BOSS-013: BOSS物品掉落

**用户故事**: 作为掉落系统，当BOSS死亡时，我需要根据配置的掉落表生成物品。

**优先级**: P0 (核心功能)
**复杂度**: 高

**验收标准**:
1. 从MonItems目录加载掉落配置
2. 根据概率(SelPoint/MaxPoint)计算掉落
3. 物品有10%概率随机升级属性
4. 掉落物品散落在BOSS周围

**技术实现要点**:
- 配置加载: [LocalDB.pas:169-219](Source/Mir200/LocalDB.pas#L169-L219) `LoadMonItems`
- 掉落生成: [UsrEngn.pas:660-731](Source/Mir200/UsrEngn.pas#L660-L731) `MonGetRandomItems`
- 物品散落: [ObjBase.pas:2744-2748](Source/Mir200/ObjBase.pas#L2744-L2748) `ScatterBagItems`

---

### US-BOSS-014: BOSS金币掉落

**用户故事**: 作为掉落系统，BOSS死亡时需要掉落金币，金币数量与BOSS等级相关。

**优先级**: P1 (重要功能)
**复杂度**: 低

**验收标准**:
1. 非动物类BOSS掉落金币
2. 金币散落在BOSS周围
3. 召唤兽击杀不掉落金币
4. 战斗区域内不掉落金币

**技术实现要点**:
- 掉落逻辑: [ObjBase.pas:2747-2748](Source/Mir200/ObjBase.pas#L2747-L2748) `ScatterGolds`
- 条件判断: `RaceServer >= RC_ANIMAL`, `Master = nil`

---

### US-BOSS-015: 雷电僵尸远程攻击

**用户故事**: 作为雷电僵尸，我需要发射雷电攻击远距离的敌人。

**优先级**: P2 (增强功能)
**复杂度**: 中等

**验收标准**:
1. 攻击范围6格内的目标
2. 雷电沿直线穿透攻击
3. 攻击时发送RM_LIGHTING消息
4. 保持2格以上距离时优先远程攻击

**技术实现要点**:
- 类定义: [ObjMon.pas:112-118](Source/Mir200/ObjMon.pas#L112-L118)
- 攻击逻辑: [ObjMon.pas:938-954](Source/Mir200/ObjMon.pas#L938-L954) `LightingAttack`
- 穿透伤害: `MagPassThroughMagic`

---

### US-BOSS-016: 死牛天王多段攻击

**用户故事**: 作为死牛天王，我需要同时具备近战和远程攻击能力。

**优先级**: P1 (重要功能)
**复杂度**: 高

**验收标准**:
1. 近战范围内使用普通攻击
2. 远程范围内使用范围攻击
3. 血量降低时召唤骷髅小怪
4. 继承骷髅半王的连击机制

**技术实现要点**:
- 类定义: [ObjMon.pas:246-252](Source/Mir200/ObjMon.pas#L246-L252)
- 继承关系: TDeadCowKingMonster -> TSkeletonKingMonster

---

### US-BOSS-017: BOSS属性加载

**用户故事**: 作为配置系统，我需要从Monster.DB加载BOSS的基础属性。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 加载HP、MP、AC、MAC等基础属性
2. 加载攻击力DC、MC、SC
3. 加载移动速度和攻击速度
4. 加载洞察力CoolEye

**技术实现要点**:
- 加载逻辑: [LocalDB.pas:221-272](Source/Mir200/LocalDB.pas#L221-L272) `LoadMonsters`
- 数据结构: [Grobal2.pas:807-832](Source/Common/Grobal2.pas#L807-L832) `TMonsterInfo`

---

### US-BOSS-018: 般若左使/右使攻击模式

**用户故事**: 作为般若左使/右使，我需要根据距离切换近战和远程攻击模式。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 近战范围内使用普通攻击
2. 远程范围内使用范围攻击
3. 继承骷髅半王的召唤能力
4. 攻击时播放对应动画

**技术实现要点**:
- 类定义: [ObjMon.pas:254-260](Source/Mir200/ObjMon.pas#L254-L260)
- 继承关系: TBanyaGuardMonster -> TSkeletonKingMonster

---

### US-BOSS-019: BOSS伤害计算

**用户故事**: 作为战斗系统，我需要正确计算BOSS对玩家造成的伤害。

**优先级**: P0 (核心功能)
**复杂度**: 高

**验收标准**:
1. 物理伤害 = DC + Random(MaxDC-DC)
2. 伤害减去目标防御值
3. 命中判定: Random(SpeedPoint) < AccuracyPoint
4. 魔法伤害使用MAC防御

**技术实现要点**:
- 伤害计算: `GetAttackPower(Lobyte(DC), Hibyte(DC)-Lobyte(DC))`
- 命中判定: [ObjMon.pas:600-602](Source/Mir200/ObjMon.pas#L600-L602)

---

### US-BOSS-020: BOSS移动与寻路

**用户故事**: 作为BOSS，我需要能够移动到目标位置并追击敌人。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 按WalkSpeed间隔移动
2. 每次移动WalkStep格
3. 追击目标时自动寻路
4. 遇到障碍物时绕行

**技术实现要点**:
- 移动逻辑: [ObjMon.pas:456-463](Source/Mir200/ObjMon.pas#L456-L463) `GotoTargetXY`, `Wondering`
- 速度控制: `NextWalkTime`, `WalkTime`

---

## 6. BOSS配置参考表

### 6.1 BOSS属性配置示例

| BOSS名称 | 等级 | HP | AC | MAC | DC | 攻击速度 | 移动速度 | 特殊能力 |
|---------|-----|-----|-----|-----|-----|---------|---------|---------|
| 祖玛教主 | 45 | 8000 | 50 | 50 | 35-70 | 1500ms | 500ms | 石化、召唤 |
| 牛面王 | 40 | 6000 | 45 | 40 | 30-60 | 1200ms | 450ms | 狂暴、瞬移 |
| 蜈蚣王 | 35 | 4000 | 40 | 35 | 25-50 | 1800ms | 600ms | 潜伏攻击 |
| 骷髅半王 | 50 | 10000 | 55 | 55 | 40-80 | 1400ms | 500ms | 远程、召唤 |
| 死牛天王 | 55 | 15000 | 60 | 60 | 50-100 | 1300ms | 450ms | 多段攻击 |
| 黑蛇王 | 42 | 5000 | 48 | 45 | 32-65 | 1400ms | 480ms | 双重暴击 |
| 黑天魔王 | 48 | 7000 | 52 | 50 | 38-75 | 1350ms | 460ms | 双重暴击 |

### 6.2 刷怪点配置格式

**文件**: `Envir\MonGen.txt`

```
; 格式: 地图名 X Y "怪物名" 范围 数量 刷新时间(分钟) 小刷新率
; 祖玛寺庙
D516    150 150 "祖玛教主"    3  1  120  0
D516    100 100 "祖玛卫士"    5  10 30   50

; 牛魔洞
D714    200 200 "牛魔王"      3  1  90   0
D714    180 180 "牛头魔"      5  8  20   30
```

### 6.3 掉落配置格式

**目录**: `Envir\MonItems\`
**文件名**: `怪物名.txt`

```
; 格式: 选择点 最大点 "物品名" 数量
; 祖玛教主.txt
1  30  "裁决之杖"     1
1  50  "龙纹剑"       1
1  20  "祖玛头像"     1
5  100 "金币"         5000
10 100 "太阳水"       3
```

**概率计算**: 掉落概率 = SelPoint / MaxPoint × 100%

---

## 7. 边界场景处理表

| 场景 | 处理方式 | 代码位置 |
|-----|---------|---------|
| BOSS刷新位置被占用 | 在Area范围内重试30次寻找可行走位置 | [UsrEngn.pas:1049-1062](Source/Mir200/UsrEngn.pas#L1049-L1062) |
| BOSS目标死亡 | 清除TargetCret，重新搜索目标 | [ObjMon.pas:647-650](Source/Mir200/ObjMon.pas#L647-L650) |
| BOSS目标离开地图 | 检查MapName，不同则LoseTarget | [ObjMon.pas:647-650](Source/Mir200/ObjMon.pas#L647-L650) |
| 召唤小怪数量上限 | 祖玛王30只，蜂后15只，骷髅半王20只 | 各类CallFollower方法 |
| BOSS被石化状态 | StatusArr[POISON_STONE]>0时跳过AI | [ObjMon.pas:537](Source/Mir200/ObjMon.pas#L537) |
| 玩家隐身 | 根据CoolEye概率决定是否可见 | [UsrEngn.pas:1032-1034](Source/Mir200/UsrEngn.pas#L1032-L1034) |
| 经验归属者离线 | ExpHiter为nil时经验归LastHiter | [ObjBase.pas:2661-2669](Source/Mir200/ObjBase.pas#L2661-L2669) |
| 战斗区域死亡 | 不掉落物品和金币 | [ObjBase.pas:2733-2735](Source/Mir200/ObjBase.pas#L2733-L2735) |

---

## 8. 附录

### 8.1 种族常量完整列表

**位置**: [Grobal2.pas:1700-1768](Source/Common/Grobal2.pas#L1700-L1768)

| 常量名 | 值 | 说明 |
|-------|---|------|
| RC_USERHUMAN | 0 | 玩家角色 |
| RC_NPC | 10 | 普通NPC |
| RC_ANIMAL | 50 | 动物基类 |
| RC_MONSTER | 80 | 普通怪物 |
| RC_OMA | 81 | 沃玛怪 |
| RC_SCULKING | 102 | 祖玛王 |
| RC_BEEQUEEN | 103 | 蜂后 |
| RC_CENTIPEDEKING | 107 | 蜈蚣王 |
| RC_SKELETONKING | 126 | 骷髅半王 |
| RC_DEADCOWKING | 130 | 死牛天王 |

### 8.2 状态常量

| 常量名 | 说明 |
|-------|------|
| STATE_STONE_MODE | 石化状态 |
| POISON_STONE | 石化中毒 |
| POISON_DECHEALTH | 持续掉血 |

### 8.3 消息常量

| 常量名 | 说明 |
|-------|------|
| RM_DIGUP | 钻出地面动画 |
| RM_DIGDOWN | 钻入地面动画 |
| RM_HIT | 普通攻击动画 |
| RM_LIGHTING | 雷电攻击动画 |
| RM_DEATH | 死亡动画 |
| RM_ZEN_BEE | 召唤蜜蜂消息 |

---

## 9. 文档修订历史

| 版本 | 日期 | 修订内容 |
|-----|------|---------|
| 1.0 | 2026-01-30 | 初始版本，包含BOSS系统完整分析 |


