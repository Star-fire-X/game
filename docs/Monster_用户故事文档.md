# 怪物系统用户故事文档

> 基于 mir2 项目代码分析
> 分析文件: `Source/M2Server/ObjMon.pas`, `Source/M2Server/ObjMon2.pas`
> 生成日期: 2026-01-30

---

## 一、怪物系统概述

### 1.1 类继承体系

```
TCreature (生物基类)
└── TAnimal (动物基类)
    ├── TMonster (怪物基类) [ObjMon.pas:34]
    │   ├── TChickenDeer (鸡鹿-逃跑型) [ObjMon.pas:52]
    │   ├── TATMonster (攻击型怪物基类) [ObjMon.pas:61]
    │   │   ├── TSlowATMonster (慢速攻击) [ObjMon.pas:72]
    │   │   ├── TScorpion (蝎子) [ObjMon.pas:79]
    │   │   ├── TSpitSpider (吐液蜘蛛) [ObjMon.pas:87]
    │   │   │   ├── THighRiskSpider (巨型蜘蛛) [ObjMon.pas:98]
    │   │   │   ├── TBigPoisionSpider (巨型毒蜘蛛) [ObjMon.pas:105]
    │   │   │   └── TElfWarriorMonster (神兽-变身后) [ObjMon.pas:260]
    │   │   ├── TGasAttackMonster (毒气攻击) [ObjMon.pas:112]
    │   │   │   ├── TGasMothMonster (毒气飞蛾) [ObjMon.pas:231]
    │   │   │   └── TGasDungMonster (毒气怪物) [ObjMon.pas:240]
    │   │   ├── TCowMonster (牛魔) [ObjMon.pas:122]
    │   │   ├── TMagCowMonster (魔法牛魔) [ObjMon.pas:129]
    │   │   ├── TCowKingMonster (牛魔王BOSS) [ObjMon.pas:139]
    │   │   ├── TZilKinZombi (复活僵尸) [ObjMon.pas:179]
    │   │   ├── TWhiteSkeleton (白骨骷髅-召唤兽) [ObjMon.pas:192]
    │   │   ├── TCriticalMonster (暴击怪物) [ObjMon.pas:274]
    │   │   ├── TDoubleCriticalMonster (双格暴击) [ObjMon.pas:283]
    │   │   └── TSkeletonSoldier (骷髅士兵) [ObjMon.pas:307]
    │   ├── TLightingZombi (闪电僵尸) [ObjMon.pas:158]
    │   ├── TDigOutZombi (挖地僵尸) [ObjMon.pas:169]
    │   ├── TScultureMonster (石像怪物) [ObjMon.pas:204]
    │   ├── TScultureKingMonster (祖玛王BOSS) [ObjMon.pas:215]
    │   │   ├── TSkeletonKingMonster (骷髅王BOSS) [ObjMon.pas:293]
    │   │   │   ├── TDeadCowKingMonster (死亡牛魔王) [ObjMon.pas:317]
    │   │   │   └── TBanyaGuardMonster (般若护卫) [ObjMon.pas:326]
    │   └── TElfMonster (神兽-变身前) [ObjMon.pas:247]
    │
    ├── TStickMonster (固定型怪物) [ObjMon2.pas:41]
    │   └── TCentipedeKingMonster (蜈蚣王/触龙神BOSS) [ObjMon2.pas:86]
    ├── TBeeQueen (蜂巢怪物) [ObjMon2.pas:69]
    ├── TBigHeartMonster (赤月魔/心脏怪物) [ObjMon2.pas:108]
    ├── TBamTreeMonster (栗子树怪物) [ObjMon2.pas:123]
    ├── TSpiderHouseMonster (蜘蛛巢怪物) [ObjMon2.pas:138]
    ├── TExplosionSpider (自爆蜘蛛) [ObjMon2.pas:155]
    │
    └── TGuardUnit (守卫单位基类) [ObjMon2.pas:178]
        ├── TArcherGuard (弓箭手守卫) [ObjMon2.pas:196]
        │   └── TArcherPolice (弓箭手警察) [ObjMon2.pas:210]
        ├── TCastleDoor (城门) [ObjMon2.pas:219]
        └── TWallStructure (城墙) [ObjMon2.pas:245]
```

### 1.2 怪物类型统计

| 分类 | 数量 | 说明 |
|------|------|------|
| 普通怪物 | 15 | 基础攻击型、逃跑型、毒气型等 |
| BOSS怪物 | 6 | 牛魔王、祖玛王、骷髅王、触龙神等 |
| 召唤兽 | 3 | 白骨骷髅、神兽(变身前/后) |
| 特殊怪物 | 8 | 固定型、自爆型、召唤巢穴等 |
| 城堡单位 | 5 | 守卫、城门、城墙等 |
| **总计** | **37** | - |

---

## 二、怪物AI行为模式分类

### 2.1 按攻击行为分类

| AI类型 | 代表怪物 | 行为特征 | 代码位置 |
|--------|----------|----------|----------|
| **被动逃跑型** | TChickenDeer | 遇敌逃跑，不主动攻击 | ObjMon.pas:576-616 |
| **主动攻击型** | TATMonster | 主动搜索并攻击最近敌人 | ObjMon.pas:637-646 |
| **远程攻击型** | TSpitSpider | 吐液远程攻击，可中毒 | ObjMon.pas:689-738 |
| **毒气攻击型** | TGasAttackMonster | 近战毒气，可麻痹 | ObjMon.pas:810-855 |
| **闪电攻击型** | TLightingZombi | 远程闪电，保持距离 | ObjMon.pas:1078-1136 |
| **自爆攻击型** | TExplosionSpider | 接近目标后自爆 | ObjMon2.pas:1055-1086 |
| **范围攻击型** | TBigHeartMonster | 超远范围攻击 | ObjMon2.pas:785-826 |

### 2.2 按特殊机制分类

| 机制类型 | 代表怪物 | 机制说明 | 代码位置 |
|----------|----------|----------|----------|
| **隐藏出现** | TDigOutZombi | 从地下钻出攻击 | ObjMon.pas:1157-1198 |
| **石化解除** | TScultureMonster | 初始石化，接近解除 | ObjMon.pas:1319-1379 |
| **死后复活** | TZilKinZombi | 死后可复活多次 | ObjMon.pas:1220-1247 |
| **召唤小怪** | TScultureKingMonster | HP下降时召唤援军 | ObjMon.pas:1426-1451 |
| **瞬移逃脱** | TCowKingMonster | 被围困时瞬移 | ObjMon.pas:1016-1028 |
| **暴走模式** | TCowKingMonster | HP下降进入暴走 | ObjMon.pas:1030-1056 |
| **变身机制** | TElfMonster | 遇敌变身为战斗形态 | ObjMon.pas:1609-1643 |
| **生成子怪** | TBeeQueen | 持续生成子怪物 | ObjMon2.pas:509-515 |

---

## 三、怪物属性系统

### 3.1 核心属性

| 属性 | 字段名 | 说明 | 代码位置 |
|------|--------|------|----------|
| 生命值 | WAbil.HP / WAbil.MaxHP | 当前/最大生命值 | ObjBase.pas |
| 攻击力 | WAbil.DC | 物理攻击力范围 | ObjBase.pas |
| 防御力 | WAbil.AC | 物理防御力范围 | ObjBase.pas |
| 魔法防御 | WAbil.MAC | 魔法防御力范围 | ObjBase.pas |
| 视野范围 | ViewRange | 搜索敌人范围(格) | ObjMon.pas:348 |
| 移动速度 | NextWalkTime | 移动间隔(ms) | ObjMon.pas:349 |
| 攻击速度 | NextHitTime | 攻击间隔(ms) | ObjBase.pas |
| 搜索频率 | SearchRate | 搜索敌人频率(ms) | ObjMon.pas:350 |
| 命中率 | AccuracyPoint | 攻击命中点数 | ObjBase.pas |
| 敏捷度 | SpeedPoint | 闪避点数 | ObjBase.pas |
| 抗毒性 | AntiPoison | 抗毒等级 | ObjBase.pas |
| 抗魔性 | AntiMagic | 魔法抵抗等级 | ObjBase.pas |

### 3.2 特殊状态标志

| 标志 | 字段名 | 说明 | 代码位置 |
|------|--------|------|----------|
| 隐藏模式 | HideMode | 是否隐藏不可见 | ObjMon2.pas:302 |
| 固定模式 | StickMode | 是否固定不移动 | ObjMon2.pas:303 |
| 石化模式 | BoStoneMode | 是否石化状态 | ObjMon.pas:1313 |
| 逃跑模式 | BoRunAwayMode | 是否逃跑中 | ObjMon.pas:597 |
| 暴走模式 | CrazyKingMode | BOSS暴走状态 | ObjMon.pas:1047 |
| 不死模式 | NeverDie | 是否永不死亡 | ObjMon2.pas:1703 |

---

## 四、Epic级别用户故事

### Epic 1: 怪物生成与刷新系统

**描述**: 管理怪物在游戏世界中的生成、刷新和消失机制

**涉及模块**: `UserEngine`, `Envir`, `ObjMon.pas`, `ObjMon2.pas`

### Epic 2: 怪物AI行为系统

**描述**: 控制怪物的智能行为，包括目标选择、移动决策、攻击策略

**涉及模块**: `TMonster.Run()`, `TMonster.Think()`, `TMonster.AttackTarget()`

### Epic 3: 怪物攻击系统

**描述**: 实现怪物的各种攻击方式，包括近战、远程、魔法、范围攻击

**涉及模块**: `Attack()`, `SpitAttack()`, `GasAttack()`, `RangeAttack()`

### Epic 4: 怪物掉落系统

**描述**: 管理怪物死亡后的物品掉落机制

**涉及模块**: `TCreature.Die()`, `DropItem`, `FightExp`

### Epic 5: BOSS怪物机制

**描述**: 实现BOSS怪物的特殊机制，如召唤、瞬移、暴走、阶段变化

**涉及模块**: `TCowKingMonster`, `TScultureKingMonster`, `TSkeletonKingMonster`

### Epic 6: 召唤兽系统

**描述**: 实现玩家召唤兽的行为控制和能力成长

**涉及模块**: `TWhiteSkeleton`, `TElfMonster`, `TElfWarriorMonster`

---

## 五、具体用户故事

### Epic 1: 怪物生成与刷新系统

#### US-1.1 怪物初始化生成

```
作为 游戏服务器
我想要 在地图加载时根据配置生成怪物
以便 玩家进入地图时能遇到预设的怪物

验收标准：
- [ ] 根据地图配置文件生成指定类型和数量的怪物
- [ ] 怪物生成在有效的可行走位置
- [ ] 怪物初始属性根据配置正确设置

技术实现要点：
- 涉及文件: UserEngine, Envir
- 关键方法: UserEngine.AddCreatureSysop()
- 数据结构: TCreature, TMonster

优先级：P0
复杂度：M
状态：已实现
```

#### US-1.2 怪物定时刷新

```
作为 游戏服务器
我想要 怪物死亡后按配置时间自动刷新
以便 保持地图怪物数量稳定

验收标准：
- [ ] 怪物死亡后记录死亡时间
- [ ] 达到刷新时间后在原位置或随机位置重生
- [ ] 刷新的怪物属性恢复为初始值

技术实现要点：
- 涉及文件: Envir, MonGen
- 关键方法: MonGen.Run()
- 数据结构: TMonGenInfo

优先级：P0
复杂度：M
状态：已实现
```

### Epic 2: 怪物AI行为系统

#### US-2.1 主动攻击型AI

```
作为 攻击型怪物(TATMonster)
我想要 主动搜索并攻击视野内最近的敌人
以便 对玩家形成威胁

验收标准：
- [ ] 每1-8秒搜索一次视野内敌人
- [ ] 选择距离最近的有效目标
- [ ] 向目标移动并在攻击范围内发起攻击

技术实现要点：
- 涉及文件: ObjMon.pas:637-646
- 关键方法: TATMonster.Run(), MonsterNormalAttack()
- 搜索间隔: SearchRate = 1500 + Random(1500) ms

优先级：P0
复杂度：S
状态：已实现
```

#### US-2.2 逃跑型AI

```
作为 逃跑型怪物(TChickenDeer)
我想要 遇到敌人时向反方向逃跑
以便 模拟胆小动物的行为

验收标准：
- [ ] 检测到敌人后进入逃跑模式
- [ ] 计算远离敌人的方向并移动
- [ ] 敌人离开视野后停止逃跑

技术实现要点：
- 涉及文件: ObjMon.pas:576-616
- 关键方法: TChickenDeer.Run()
- 逃跑距离: 5格

优先级：P1
复杂度：S
状态：已实现
```

#### US-2.3 隐藏出现型AI

```
作为 隐藏型怪物(TDigOutZombi/TStickMonster)
我想要 在玩家接近时从地下钻出攻击
以便 给玩家带来突袭体验

验收标准：
- [ ] 初始状态为隐藏模式(HideMode=TRUE)
- [ ] 玩家进入出现范围(3-4格)时触发出现
- [ ] 播放钻出动画(RM_DIGUP)后进入攻击状态

技术实现要点：
- 涉及文件: ObjMon.pas:1169-1198, ObjMon2.pas:384-409
- 关键方法: ComeOut(), CheckComeOut()
- 出现范围: DigupRange = 3-4格

优先级：P1
复杂度：M
状态：已实现
```

#### US-2.4 石化解除型AI

```
作为 石像怪物(TScultureMonster)
我想要 在玩家接近时解除石化状态并攻击
以便 模拟石像复苏的效果

验收标准：
- [ ] 初始状态为石化模式(BoStoneMode=TRUE)
- [ ] 玩家进入2格范围时解除石化
- [ ] 解除时同时唤醒周围7格内的其他石像

技术实现要点：
- 涉及文件: ObjMon.pas:1350-1379
- 关键方法: MeltStone(), MeltStoneAll()
- 状态标志: CharStatusEx = STATE_STONE_MODE

优先级：P1
复杂度：M
状态：已实现
```

### Epic 3: 怪物攻击系统

#### US-3.1 远程吐液攻击

```
作为 吐液蜘蛛(TSpitSpider)
我想要 向目标方向吐出毒液造成范围伤害
以便 实现远程攻击并附加中毒效果

验收标准：
- [ ] 在吐液范围内(SpitMap定义的5x5区域)攻击
- [ ] 伤害受目标魔法防御影响
- [ ] 5%概率使目标中毒(POISON_DECHEALTH)

技术实现要点：
- 涉及文件: ObjMon.pas:689-738
- 关键方法: SpitAttack()
- 攻击范围: SpitMap[dir, 5, 5]

优先级：P0
复杂度：M
状态：已实现
```

#### US-3.2 毒气麻痹攻击

```
作为 毒气怪物(TGasAttackMonster)
我想要 对前方目标释放毒气造成伤害和麻痹
以便 控制玩家行动

验收标准：
- [ ] 攻击前方1格的目标
- [ ] 5%概率使目标麻痹(POISON_STONE, 5秒)
- [ ] 毒鬼类型改为体力下降毒

技术实现要点：
- 涉及文件: ObjMon.pas:810-855
- 关键方法: GasAttack()
- 麻痹概率: Random(20 + AntiPoison) = 0

优先级：P0
复杂度：M
状态：已实现
```

#### US-3.3 闪电远程攻击

```
作为 闪电僵尸(TLightingZombi)
我想要 向目标发射穿透性闪电攻击
以便 实现远程直线范围伤害

验收标准：
- [ ] 攻击范围6格内的目标
- [ ] 闪电穿透路径上所有敌人
- [ ] 保持与目标的距离，太近时后退

技术实现要点：
- 涉及文件: ObjMon.pas:1078-1136
- 关键方法: LightingAttack(), MagPassThroughMagic()
- 攻击距离: 9格穿透

优先级：P1
复杂度：M
状态：已实现
```

#### US-3.4 自爆攻击

```
作为 自爆蜘蛛(TExplosionSpider)
我想要 接近目标后自爆造成范围伤害
以便 实现自杀式攻击机制

验收标准：
- [ ] 接近目标1格范围内触发自爆
- [ ] 对周围1格内所有敌人造成伤害
- [ ] 伤害分为物理和魔法各50%
- [ ] 存活超过1分钟自动自爆

技术实现要点：
- 涉及文件: ObjMon2.pas:1055-1141
- 关键方法: DoSelfExplosion(), AttackTarget()
- 超时时间: 60秒

优先级：P1
复杂度：M
状态：已实现
```

### Epic 5: BOSS怪物机制

#### US-5.1 牛魔王瞬移逃脱

```
作为 牛魔王(TCowKingMonster)
我想要 被多人围困时瞬移到目标身后
以便 避免被玩家围杀

验收标准：
- [ ] 被5人以上围困时触发瞬移
- [ ] 瞬移到当前目标身后位置
- [ ] 瞬移冷却时间30秒

技术实现要点：
- 涉及文件: ObjMon.pas:1016-1028
- 关键方法: SpaceMove(), GetBackPosition()
- 触发条件: SiegeLockCount >= 5

优先级：P0
复杂度：M
状态：已实现
```

#### US-5.2 牛魔王暴走模式

```
作为 牛魔王(TCowKingMonster)
我想要 HP下降时进入暴走模式
以便 增加战斗难度和紧张感

验收标准：
- [ ] HP每下降约14%触发一次暴走准备
- [ ] 暴走准备期8秒内攻击间隔变为10秒(被动挨打)
- [ ] 暴走期8秒内攻击间隔500ms，移动间隔400ms

技术实现要点：
- 涉及文件: ObjMon.pas:1030-1056
- 关键字段: CrazyReadyMode, CrazyKingMode
- 暴走等级: CrazyCount = 7 - HP/MaxHP*7

优先级：P0
复杂度：L
状态：已实现
```

#### US-5.3 祖玛王召唤小怪

```
作为 祖玛王(TScultureKingMonster)
我想要 HP下降时召唤祖玛系小怪
以便 增加战斗复杂度

验收标准：
- [ ] HP每下降20%触发一次召唤
- [ ] 每次召唤6-12只随机祖玛怪物
- [ ] 最多同时存在30只召唤怪

技术实现要点：
- 涉及文件: ObjMon.pas:1426-1511
- 关键方法: CallFollower()
- 召唤类型: 祖玛护法、祖玛卫士、祖玛弓箭手、祖玛雕像

优先级：P0
复杂度：M
状态：已实现
```

#### US-5.4 触龙神出现机制

```
作为 触龙神(TCentipedeKingMonster)
我想要 从地下出现并进行范围中毒攻击
以便 实现独特的BOSS战斗体验

验收标准：
- [ ] 隐藏10秒后检测玩家进入4格范围
- [ ] 出现时HP恢复满血
- [ ] 攻击视野内所有敌人，25%概率中毒
- [ ] 无敌人10秒后返回地下

技术实现要点：
- 涉及文件: ObjMon2.pas:590-754
- 关键方法: ComeOut(), AttackTarget()
- 中毒类型: 2/3体力下降，1/3石化

优先级：P0
复杂度：L
状态：已实现
```

### Epic 6: 召唤兽系统

#### US-6.1 白骨骷髅召唤

```
作为 道士玩家
我想要 召唤白骨骷髅作为战斗伙伴
以便 辅助战斗

验收标准：
- [ ] 召唤时从地下钻出(RM_DIGUP)
- [ ] 攻击速度和移动速度随召唤等级提升
- [ ] 跟随主人移动，主动攻击敌人

技术实现要点：
- 涉及文件: ObjMon.pas:1255-1300
- 关键方法: ResetSkeleton(), RecalcAbilitys()
- 速度公式: NextHitTime = 3000 - SlaveMakeLevel*600

优先级：P0
复杂度：M
状态：已实现
```

#### US-6.2 神兽变身机制

```
作为 神兽(TElfMonster)
我想要 遇到敌人时变身为战斗形态
以便 进行战斗

验收标准：
- [ ] 初始为非攻击形态
- [ ] 检测到敌人或主人被攻击时变身
- [ ] 变身后60秒无敌人则变回原形态

技术实现要点：
- 涉及文件: ObjMon.pas:1568-1734
- 关键方法: MakeClone(), AppearNow()
- 变身类型: TElfMonster <-> TElfWarriorMonster

优先级：P1
复杂度：L
状态：已实现
```

### 其他重要用户故事

#### US-7.1 僵尸复活机制

```
作为 复活僵尸(TZilKinZombi)
我想要 死后有概率复活继续战斗
以便 增加战斗持久性

验收标准：
- [ ] 33%概率拥有1-3次复活机会
- [ ] 死后4-24秒随机时间复活
- [ ] 每次复活HP和经验值减半

技术实现要点：
- 涉及文件: ObjMon.pas:1206-1247
- 关键字段: LifeCount, RelifeTime
- 复活方法: Alive()

优先级：P1
复杂度：M
状态：已实现
```

#### US-7.2 蜂巢/蜘蛛巢生成子怪

```
作为 蜂巢/蜘蛛巢怪物
我想要 持续生成子怪物攻击敌人
以便 形成群体攻击效果

验收标准：
- [ ] 有目标时每次攻击生成1只子怪
- [ ] 最多同时存在15只子怪
- [ ] 子怪自动攻击母体的目标

技术实现要点：
- 涉及文件: ObjMon2.pas:509-582, 932-1021
- 关键方法: MakeChildBee(), MakeChildSpider()
- 子怪类型: 蜜蜂、自爆蜘蛛

优先级：P1
复杂度：M
状态：已实现
```

#### US-7.3 毒气飞蛾破隐身

```
作为 毒气飞蛾(TGasMothMonster)
我想要 能够看到隐身玩家并解除其隐身
以便 克制隐身战术

验收标准：
- [ ] 可以检测到隐身状态的玩家
- [ ] 攻击命中后33%概率解除目标隐身
- [ ] 使用MonsterDetecterAttack搜索隐身目标

技术实现要点：
- 涉及文件: ObjMon.pas:1515-1548
- 关键方法: GasAttack(), MonsterDetecterAttack()
- 解除条件: Random(3) = 0

优先级：P1
复杂度：S
状态：已实现
```

#### US-7.4 暴击怪物机制

```
作为 暴击怪物(TCriticalMonster)
我想要 积累攻击次数后释放暴击
以便 造成高额伤害

验收标准：
- [ ] 每次攻击积累1点暴击点数
- [ ] 超过5点或10%概率触发暴击
- [ ] 暴击伤害 = 普通伤害 * (MaxMP/10)

技术实现要点：
- 涉及文件: ObjMon.pas:1742-1764
- 关键字段: criticalpoint
- 暴击动画: RM_LIGHTING

优先级：P2
复杂度：S
状态：已实现
```

#### US-7.5 弓箭手守卫远程攻击

```
作为 弓箭手守卫(TArcherGuard)
我想要 远程射箭攻击红名玩家和城堡罪犯
以便 维护城镇治安

验收标准：
- [ ] 视野范围12格，攻击最近的有效目标
- [ ] 攻击红名玩家(PKLevel>=2)和城堡罪犯
- [ ] 击杀不给予经验值

技术实现要点：
- 涉及文件: ObjMon2.pas:1264-1377
- 关键方法: ShotArrow(), IsProperTarget()
- 伤害延迟: 600 + 距离*50 ms

优先级：P0
复杂度：M
状态：已实现
```

#### US-7.6 城门开关与损坏

```
作为 城门(TCastleDoor)
我想要 支持开关操作和被攻击损坏
以便 实现城堡攻防机制

验收标准：
- [ ] 支持打开/关闭状态切换
- [ ] 关闭时阻挡通行，打开时允许通行
- [ ] 根据HP显示不同损坏程度(0-2级)
- [ ] 被摧毁后允许通行

技术实现要点：
- 涉及文件: ObjMon2.pas:1400-1579
- 关键方法: OpenDoor(), CloseDoor(), ActiveDoorWall()
- 状态枚举: TDoorState(dsOpen, dsClose, dsBroken)

优先级：P0
复杂度：L
状态：已实现
```

---

## 六、怪物类型分类表

### 6.1 按AI行为分类

| AI类型 | 怪物类 | 代表怪物 |
|--------|--------|----------|
| 被动逃跑 | TChickenDeer | 鸡、鹿 |
| 主动近战 | TATMonster | 多数普通怪物 |
| 远程吐液 | TSpitSpider | 吐液蜘蛛、巨型蜘蛛 |
| 毒气攻击 | TGasAttackMonster | 蛆虫、毒气飞蛾 |
| 闪电远程 | TLightingZombi | 闪电僵尸 |
| 隐藏突袭 | TDigOutZombi, TStickMonster | 挖地僵尸、食人草 |
| 石化解除 | TScultureMonster | 祖玛雕像 |
| 自爆攻击 | TExplosionSpider | 自爆蜘蛛 |

### 6.2 按攻击方式分类

| 攻击方式 | 怪物类 | 特点 |
|----------|--------|------|
| 普通近战 | TMonster, TATMonster | 1格攻击范围 |
| 范围近战 | TDoubleCriticalMonster | 5x5范围攻击 |
| 远程单体 | TArcherGuard, TSkeletonKingMonster | 飞斧/箭矢 |
| 远程范围 | TSpitSpider, TBigHeartMonster | 吐液/心跳攻击 |
| 穿透攻击 | TLightingZombi | 闪电穿透 |
| 魔法攻击 | TMagCowMonster, TBanyaGuardMonster | 魔法伤害 |

### 6.3 按特殊能力分类

| 特殊能力 | 怪物类 | 能力说明 |
|----------|--------|----------|
| 召唤小怪 | TScultureKingMonster, TBeeQueen | HP下降/持续召唤 |
| 瞬移 | TCowKingMonster | 被围困时瞬移 |
| 暴走 | TCowKingMonster | HP下降加速 |
| 复活 | TZilKinZombi | 死后复活 |
| 变身 | TElfMonster | 遇敌变身 |
| 中毒 | TSpitSpider, TGasAttackMonster | 附加中毒效果 |
| 麻痹 | TGasAttackMonster, TCentipedeKingMonster | 附加石化效果 |
| 破隐身 | TGasMothMonster | 解除隐身状态 |
| 暴击 | TCriticalMonster | 积累暴击 |

---

## 七、边界场景与异常处理

### 7.1 怪物死亡处理

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 普通死亡 | 调用Die()，设置Death=TRUE | ObjBase.pas |
| 复活僵尸死亡 | 记录死亡时间，等待复活 | ObjMon.pas:1220-1228 |
| 召唤兽死亡 | 从主人SlaveList移除 | ObjBase.pas |
| 城门/城墙损坏 | 保持不消失，允许通行 | ObjMon2.pas:1544-1549 |
| 神兽死亡 | 2秒后直接消失(无尸体) | ObjMon.pas:1620-1623 |

### 7.2 目标丢失处理

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 目标切换地图 | 调用LoseTarget()清空目标 | ObjMon.pas:460-461 |
| 目标死亡 | Think()中检测并清空 | ObjMon.pas:425-426 |
| 目标超出视野 | 继续追踪直到超出范围 | ObjMon.pas:458-459 |
| 召唤兽目标丢失 | 跟随主人移动 | ObjMon.pas:506-528 |

### 7.3 位置重叠处理

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 多怪物重叠 | Think()检测后随机移动 | ObjMon.pas:422-436 |
| 召唤兽与主人重叠 | 移动到主人身后 | ObjMon.pas:508-517 |
| 怪物生成位置被占 | 寻找附近可用位置 | UserEngine |

### 7.4 控制状态处理

| 场景 | 处理方式 | 代码位置 |
|------|----------|----------|
| 石化状态 | StatusArr[POISON_STONE]>0时停止行动 | ObjMon.pas:472 |
| 中毒状态 | 持续扣血，显示绿色名字 | ObjBase.pas |
| 麻痹状态 | 无法移动和攻击 | ObjBase.pas |

---

## 八、功能完整性评估

### 8.1 已实现功能

| 功能模块 | 完成度 | 说明 |
|----------|--------|------|
| 基础AI系统 | 100% | 主动攻击、逃跑、跟随等 |
| 攻击系统 | 100% | 近战、远程、范围、魔法 |
| BOSS机制 | 100% | 召唤、瞬移、暴走、阶段 |
| 召唤兽系统 | 100% | 骷髅、神兽变身 |
| 特殊怪物 | 100% | 隐藏、石化、复活、自爆 |
| 城堡单位 | 100% | 守卫、城门、城墙 |

### 8.2 潜在改进点

| 改进项 | 说明 | 优先级 |
|--------|------|--------|
| AI寻路优化 | 当前使用简单直线移动 | P2 |
| 仇恨系统 | 缺少完整的仇恨值管理 | P2 |
| 技能冷却 | BOSS技能缺少独立冷却 | P2 |
| 阶段转换 | 可增加更多BOSS阶段 | P3 |

---

## 九、文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-01-30 | 初始版本，基于代码分析 |

---

**文档结束**
