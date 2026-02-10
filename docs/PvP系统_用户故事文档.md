# PvP系统用户故事文档

## 1. 系统概述

PvP系统是mir2游戏服务器的核心战斗系统，负责管理玩家之间的对战机制。系统包含PK模式切换、PK值与红名惩罚、安全区保护、行会战争、攻城战等多个子系统，通过精细的规则设计平衡游戏的自由度与公平性。

### 1.1 核心文件索引

| 文件路径 | 主要职责 | 关键行号 |
|---------|---------|---------|
| [Grobal2.pas](Source/Common/Grobal2.pas) | PK模式常量、区域类型常量、角色类型常量 | 961-971, 1705 |
| [ObjBase.pas](Source/Mir200/ObjBase.pas) | TCreature战斗基类、攻击目标判定、PK值处理、安全区检测、死亡处理 | 3822-3870, 3999-4021, 7922-8097, 2341-2808 |
| [Guild.pas](Source/Mir200/Guild.pas) | 行会战争系统、敌对/同盟行会管理 | 23-100 |
| [UsrEngn.pas](Source/Mir200/UsrEngn.pas) | 玩家管理、PK值持久化 | 1837 |
| [RunDB.pas](Source/Mir200/RunDB.pas) | PK值数据库读写 | 88, 204 |

### 1.2 系统架构图

```
PvP系统
    │
    ├── PK模式系统
    │       ├── 和平模式 (HAM_PEACE)
    │       ├── 全体模式 (HAM_ALL)
    │       ├── 编组模式 (HAM_GROUP)
    │       ├── 行会模式 (HAM_GUILD)
    │       └── 善恶模式 (HAM_PKATTACK)
    │
    ├── PK值系统
    │       ├── PK值获取 (IncPKPoint)
    │       ├── PK值衰减 (DecPKPoint)
    │       ├── 红名等级 (PKLevel)
    │       └── 灰名状态 (BoIllegalAttack)
    │
    ├── 安全区系统
    │       ├── 普通安全区 (InSafeZone)
    │       ├── 行会战安全区 (InGuildWarSafeZone)
    │       └── 自由PK区 (BoInFreePKArea)
    │
    ├── 死亡惩罚系统
    │       ├── 物品掉落 (ScatterBagItems)
    │       ├── 装备掉落 (DropUseItems)
    │       └── 运气值损失 (AddBodyLuck)
    │
    ├── 行会战争系统
    │       ├── 宣战/停战 (DeclareGuildWar)
    │       ├── 敌对行会 (KillGuilds)
    │       └── 同盟行会 (AllyGuilds)
    │
    └── 攻城战系统
            ├── 攻城区域 (IsCastleWarArea)
            └── 攻城状态 (BoCastleUnderAttack)
```

---

## 2. 核心数据结构

### 2.1 PK模式常量

**位置**: [Grobal2.pas:961-966](Source/Common/Grobal2.pas#L961-L966)

```pascal
HAM_ALL       = 0;  // 全体攻击模式 - 可攻击除NPC外的所有目标
HAM_PEACE     = 1;  // 和平模式 - 只攻击怪物
HAM_GROUP     = 2;  // 编组模式 - 攻击编组外的任何人
HAM_GUILD     = 3;  // 行会模式 - 攻击行会外的任何人
HAM_PKATTACK  = 4;  // 善恶模式 - 红名攻击白名/白名攻击红名
HAM_MAXCOUNT  = 5;  // 攻击模式数量
```

### 2.2 区域类型常量

**位置**: [Grobal2.pas:969-971](Source/Common/Grobal2.pas#L969-L971)

```pascal
AREA_FIGHT    = $01;  // 战斗区域 - FightZone
AREA_SAFE     = $02;  // 安全区域 - Lawfull地图
AREA_FREEPK   = $04;  // 自由PK区域 - 攻城战区域
```

### 2.3 PK相关属性

**位置**: [ObjBase.pas:162, 356-357](Source/Mir200/ObjBase.pas#L162)

```pascal
PlayerKillingPoint: integer;  // PK值，每100点为1级红名
BoIllegalAttack: Boolean;     // 灰名状态标记(先手攻击白名玩家)
IllegalAttackTime: longword;  // 灰名状态开始时间
```

### 2.4 行会战争记录

**位置**: [Guild.pas:23-28](Source/Mir200/Guild.pas#L23-L28)

```pascal
TGuildWarInfo = record
   WarGuild: TGuild;      // 敌对行会对象
   WarTime: longword;     // 宣战时间
   // 战争持续时间由配置决定
end;
```

---

## 3. PK模式对比表

| 模式 | 常量值 | 可攻击目标 | 不可攻击目标 | 适用场景 |
|-----|-------|-----------|-------------|---------|
| 全体模式 | HAM_ALL(0) | 怪物、所有玩家、召唤兽 | NPC | 自由PK、混战 |
| 和平模式 | HAM_PEACE(1) | 怪物 | 所有玩家、召唤兽 | 安全练级、新手保护 |
| 编组模式 | HAM_GROUP(2) | 怪物、非队友玩家 | 队友、队友召唤兽 | 组队练级 |
| 行会模式 | HAM_GUILD(3) | 怪物、非本会玩家 | 本会成员、同盟行会 | 行会活动 |
| 善恶模式 | HAM_PKATTACK(4) | 怪物、红/白名互攻 | 同色名玩家 | 正义惩恶 |

### 3.1 攻击目标判定流程

**位置**: [ObjBase.pas:7949-8069](Source/Mir200/ObjBase.pas#L7949-L8069)

```
_IsProperTarget(target) 判定流程:
    │
    ├── 自身是怪物(RaceServer >= RC_ANIMAL)?
    │       ├── 有主人 → 判定主人的敌人
    │       └── 无主人 → 攻击玩家和召唤兽
    │
    └── 自身是玩家(RaceServer = RC_USERHUMAN)?
            │
            ├── HAM_ALL → 攻击非NPC目标
            ├── HAM_PEACE → 只攻击怪物
            ├── HAM_GROUP → 排除队友
            ├── HAM_GUILD → 排除本会和同盟
            └── HAM_PKATTACK → 红白名互攻
```

---

## 4. Epic级别用户故事

### Epic 1: PK模式系统

**描述**: 作为游戏服务器，我需要提供多种PK模式供玩家选择，以适应不同的游戏场景和玩家需求。

**核心文件**: [ObjBase.pas:7949-8069](Source/Mir200/ObjBase.pas#L7949-L8069)

**关键功能**:
- 5种PK模式切换(和平/全体/编组/行会/善恶)
- 攻击目标过滤逻辑
- 召唤兽攻击目标继承
- 非PK服务器特殊规则

---

### Epic 2: PK值与红名系统

**描述**: 作为PK惩罚系统，我需要记录玩家的PK行为并给予相应的红名惩罚，维护游戏秩序。

**核心文件**: [ObjBase.pas:3867-3870, 3999-4021](Source/Mir200/ObjBase.pas#L3867-L3870)

**关键功能**:
- PK值获取与累计
- 红名等级计算(PKLevel = PKPoint / 100)
- PK值自然衰减(每2分钟-1点)
- 灰名状态管理(60秒超时)

---

### Epic 3: 安全区保护系统

**描述**: 作为玩家保护系统，我需要在特定区域禁止PK行为，为玩家提供安全的交易和休息场所。

**核心文件**: [ObjBase.pas:3822-3865](Source/Mir200/ObjBase.pas#L3822-L3865)

**关键功能**:
- 安全区范围检测(出生点10格内)
- 行会战安全区(出生点60格内)
- 地图Lawfull属性
- 安全区内攻击限制

---

### Epic 4: 死亡惩罚系统

**描述**: 作为惩罚系统，我需要在玩家死亡时根据其PK状态给予不同程度的物品掉落惩罚。

**核心文件**: [ObjBase.pas:2389-2560](Source/Mir200/ObjBase.pas#L2389-L2560)

**关键功能**:
- 背包物品掉落(红名100%/白名33%)
- 装备掉落(特殊属性装备)
- 金币掉落
- 运气值损失

---

### Epic 5: 行会战争系统

**描述**: 作为行会系统，我需要支持行会之间的宣战和同盟机制，丰富玩家的社交玩法。

**核心文件**: [Guild.pas:67-77](Source/Mir200/Guild.pas#L67-L77)

**关键功能**:
- 行会宣战/停战
- 敌对行会列表管理
- 同盟行会机制
- 行会战击杀不增加PK值

---

### Epic 6: 攻城战系统

**描述**: 作为大型PvP系统，我需要支持攻城战玩法，允许行会争夺城堡控制权。

**核心文件**: [ObjBase.pas:2707-2709, 3919-3951](Source/Mir200/ObjBase.pas#L2707-L2709)

**关键功能**:
- 攻城区域判定
- 攻城状态下的PK规则
- 攻守双方颜色标识
- 攻城战击杀不增加PK值

---

## 5. 详细用户故事

### US-PVP-001: PK模式切换

**用户故事**: 作为玩家，我需要能够切换PK模式，以控制我的攻击目标范围。

**优先级**: P0 (核心功能)
**复杂度**: 低

**验收标准**:
1. 玩家可在5种模式间循环切换
2. 切换后立即生效
3. 客户端显示当前模式图标
4. 模式状态持久化保存

**技术实现要点**:
- 属性: HumAttackMode
- 常量: HAM_ALL(0) ~ HAM_PKATTACK(4)
- 位置: [Grobal2.pas:961-966](Source/Common/Grobal2.pas#L961-L966)

---

### US-PVP-002: 和平模式攻击限制

**用户故事**: 作为和平模式玩家，我只能攻击怪物，无法攻击其他玩家。

**优先级**: P0 (核心功能)
**复杂度**: 低

**验收标准**:
1. 和平模式下攻击玩家无效
2. 和平模式下攻击召唤兽无效
3. 和平模式下可正常攻击怪物
4. 切换模式后立即生效

**技术实现要点**:
- 判定逻辑: [ObjBase.pas:8018-8021](Source/Mir200/ObjBase.pas#L8018-L8021)
```pascal
HAM_PEACE: begin
   if target.RaceServer >= RC_ANIMAL then
      Result := TRUE;
end;
```

---

### US-PVP-003: 编组模式队友保护

**用户故事**: 作为编组模式玩家，我无法攻击同队队友及其召唤兽。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 编组模式下攻击队友无效
2. 编组模式下攻击队友召唤兽无效
3. 编组模式下可攻击非队友玩家
4. 队伍解散后保护立即失效

**技术实现要点**:
- 判定逻辑: [ObjBase.pas:8022-8030](Source/Mir200/ObjBase.pas#L8022-L8030)
- 队友检测: IsGroupMember(target)

---

### US-PVP-004: 行会模式同盟保护

**用户故事**: 作为行会模式玩家，我无法攻击本行会成员及同盟行会成员。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 行会模式下攻击本会成员无效
2. 行会模式下攻击同盟行会成员无效
3. 行会战区域内可攻击同盟行会
4. 无行会玩家可被攻击

**技术实现要点**:
- 判定逻辑: [ObjBase.pas:8031-8045](Source/Mir200/ObjBase.pas#L8031-L8045)
- 同盟检测: TGuild(MyGuild).IsAllyGuild()

---

### US-PVP-005: 善恶模式红白名互攻

**用户故事**: 作为善恶模式玩家，红名只能攻击白名，白名只能攻击红名。

**优先级**: P1 (重要功能)
**复杂度**: 中等

**验收标准**:
1. 红名玩家(PKLevel>=2)只能攻击白名
2. 白名玩家(PKLevel<2)只能攻击红名
3. 同色名玩家互相无法攻击
4. 怪物可正常攻击

**技术实现要点**:
- 判定逻辑: [ObjBase.pas:8046-8061](Source/Mir200/ObjBase.pas#L8046-L8061)
```pascal
HAM_PKATTACK: begin
   if self.PKLevel >= 2 then begin
      if target.PKLevel < 2 then Result := TRUE
      else Result := FALSE;
   end else begin
      if target.PKLevel >= 2 then Result := TRUE
      else Result := FALSE;
   end;
end;
```

---

### US-PVP-006: PK值获取

**用户故事**: 作为玩家，当我杀死白名玩家时，我需要获得100点PK值。

**优先级**: P0 (核心功能)
**复杂度**: 低

**验收标准**:
1. 杀死白名玩家增加100点PK值
2. 杀死红名玩家不增加PK值
3. 杀死灰名玩家不增加PK值
4. 行会战/攻城战击杀不增加PK值

**技术实现要点**:
- 增加函数: [ObjBase.pas:3999-4009](Source/Mir200/ObjBase.pas#L3999-L4009)
- 击杀判定: [ObjBase.pas:2711-2724](Source/Mir200/ObjBase.pas#L2711-L2724)
```pascal
if not LastHiter.IsGoodKilling(self) then begin
   LastHiter.IncPkPoint (100);
   LastHiter.AddBodyLuck (-500);
end;
```

---

### US-PVP-007: PK值自然衰减

**用户故事**: 作为红名玩家，我的PK值需要随时间自然衰减。

**优先级**: P0 (核心功能)
**复杂度**: 低

**验收标准**:
1. 每2分钟PK值减少1点
2. PK值最低为0
3. PK值变化时更新名字颜色
4. 离线时PK值不衰减

**技术实现要点**:
- 衰减逻辑: [ObjBase.pas:7724-7732](Source/Mir200/ObjBase.pas#L7724-L7732)
```pascal
if GetTickCount - time10min > 2 * 1000 * 60 then begin
   time10min := GetTickCount;
   if PlayerKillingPoint > 0 then
      DecPkPoint (1);
end;
```

---

### US-PVP-008: 红名等级计算

**用户故事**: 作为系统，我需要根据PK值计算玩家的红名等级并显示对应颜色。

**优先级**: P0 (核心功能)
**复杂度**: 低

**验收标准**:
1. PKLevel = PlayerKillingPoint div 100
2. PKLevel=0: 白名(默认颜色)
3. PKLevel=1: 黄名(颜色251)
4. PKLevel>=2: 红名(颜色249)

**技术实现要点**:
- 等级计算: [ObjBase.pas:3867-3870](Source/Mir200/ObjBase.pas#L3867-L3870)
- 颜色显示: [ObjBase.pas:3882-3887](Source/Mir200/ObjBase.pas#L3882-L3887)
```pascal
function TCreature.PKLevel: integer;
begin
   Result := PlayerKillingPoint div 100;
end;

function TCreature.MyColor: byte;
begin
   Result := DefNameColor;
   if PKLevel = 1 then Result := 251;  // 黄名
   if PKLevel >= 2 then Result := 249; // 红名
end;
```

---

### US-PVP-009: 灰名状态标记

**用户故事**: 作为玩家，当我先手攻击白名玩家时，我需要进入灰名状态。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 先手攻击白名玩家进入灰名状态
2. 灰名状态持续60秒
3. 灰名状态下被杀不增加对方PK值
4. 灰名显示特殊颜色(47)

**技术实现要点**:
- 灰名设置: [ObjBase.pas:2833-2842](Source/Mir200/ObjBase.pas#L2833-L2842)
- 超时检测: [ObjBase.pas:2870-2875](Source/Mir200/ObjBase.pas#L2870-L2875)
```pascal
if not BoIllegalAttack then begin
   hiter.IllegalAttackTime := GetTickCount;
   hiter.BoIllegalAttack := TRUE;
   hiter.ChangeNameColor;
end;
```

---

### US-PVP-010: 安全区攻击限制

**用户故事**: 作为玩家，当我或目标在安全区内时，无法进行PvP攻击。

**优先级**: P0 (核心功能)
**复杂度**: 低

**验收标准**:
1. 安全区内玩家无法被攻击
2. 安全区内玩家无法攻击其他玩家
3. 安全区内可正常攻击怪物
4. 召唤兽在安全区内也受保护

**技术实现要点**:
- 安全区检测: [ObjBase.pas:3822-3843](Source/Mir200/ObjBase.pas#L3822-L3843)
- 攻击限制: [ObjBase.pas:7927-7929](Source/Mir200/ObjBase.pas#L7927-L7929)
```pascal
if (InSafeZone) or (target.InSafeZone) then begin
   Result := FALSE;
end;
```

---

### US-PVP-011: 安全区范围判定

**用户故事**: 作为系统，我需要正确判定玩家是否在安全区范围内。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. Lawfull地图全图为安全区
2. 出生点10格内为安全区
3. 红名村出生点10格内为安全区
4. 多个出生点均需检测

**技术实现要点**:
- 判定函数: [ObjBase.pas:3822-3843](Source/Mir200/ObjBase.pas#L3822-L3843)
```pascal
function TCreature.InSafeZone: Boolean;
begin
   Result := PEnvir.Lawfull;
   if not Result then begin
      // 检测红名村出生点
      Result := (PEnvir.MapName = BADMANHOMEMAP) and
                ((abs(CX-BADMANSTARTX) <= 10) and (abs(CY-BADMANSTARTY) <= 10));
      // 检测所有出生点
      for i:=0 to StartPoints.Count-1 do begin
         if (map = PEnvir.MapName) and ((abs(CX-sx) <= 10) and (abs(CY-sy) <= 10)) then
            Result := TRUE;
      end;
   end;
end;
```

---

### US-PVP-012: 红名死亡物品掉落

**用户故事**: 作为红名玩家，当我死亡时，背包物品100%掉落。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. PKLevel>=2时背包物品100%掉落
2. 掉落物品散落在周围2格范围
3. 掉落物品有归属权保护
4. 战斗区/行会战区不掉落

**技术实现要点**:
- 掉落逻辑: [ObjBase.pas:2396-2441](Source/Mir200/ObjBase.pas#L2396-L2441)
```pascal
boDropall := TRUE;
if RaceServer = RC_USERHUMAN then begin
   dropwide := 2;
   if PKLevel < 2 then boDropall := FALSE; // 白名1/3概率掉落
end;
```

---

### US-PVP-013: 白名死亡物品掉落

**用户故事**: 作为白名玩家，当我死亡时，背包物品有33%概率掉落。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. PKLevel<2时背包物品33%概率掉落
2. 每件物品独立判定
3. 战斗区/行会战区不掉落
4. 被怪物击杀时正常掉落

**技术实现要点**:
- 概率判定: [ObjBase.pas:2428](Source/Mir200/ObjBase.pas#L2428)
```pascal
if (Random(3) = 0) or boDropall then begin
   // 掉落物品
end;
```

---

### US-PVP-014: 死亡运气值损失

**用户故事**: 作为玩家，当我死亡时，我需要损失运气值作为惩罚。

**优先级**: P1 (重要功能)
**复杂度**: 低

**验收标准**:
1. 死亡时损失运气值
2. 损失量与等级相关
3. 战斗区/行会战区不损失
4. 运气值影响暴击和掉落

**技术实现要点**:
- 损失计算: [ObjBase.pas:2762](Source/Mir200/ObjBase.pas#L2762)
```pascal
AddBodyLuck ( - ((MAXLEVEL-1) - ((MAXLEVEL-1) - Abil.Level * 5)));
```

---

### US-PVP-015: 杀人者运气值惩罚

**用户故事**: 作为杀死白名玩家的凶手，我需要损失500点运气值。

**优先级**: P1 (重要功能)
**复杂度**: 低

**验收标准**:
1. 杀死白名玩家损失500运气值
2. 杀死红名/灰名不损失
3. 行会战/攻城战击杀不损失
4. 运气值影响武器诅咒

**技术实现要点**:
- 惩罚逻辑: [ObjBase.pas:2719](Source/Mir200/ObjBase.pas#L2719)
```pascal
LastHiter.AddBodyLuck (-500);
```

---

### US-PVP-016: 行会战击杀免责

**用户故事**: 作为行会战参与者，击杀敌对行会成员不增加PK值。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 双方行会处于宣战状态
2. 击杀敌对行会成员不增加PK值
3. 击杀同盟行会成员正常增加PK值
4. 行会战安全区内不可攻击

**技术实现要点**:
- 免责判定: [ObjBase.pas:2699-2703](Source/Mir200/ObjBase.pas#L2699-L2703)
```pascal
if (MyGuild <> nil) and (LastHiter.MyGuild <> nil) then begin
   if GetGuildRelation (self, LastHiter) = 2 then
      guildwarkill := TRUE;  // 行会战击杀，不增加PK值
end;
```

---

### US-PVP-017: 攻城战击杀免责

**用户故事**: 作为攻城战参与者，在攻城区域击杀敌方不增加PK值。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 攻城战进行中
2. 在攻城区域或自由PK区内
3. 击杀敌方不增加PK值
4. 攻守双方显示不同颜色

**技术实现要点**:
- 免责判定: [ObjBase.pas:2707-2709](Source/Mir200/ObjBase.pas#L2707-L2709)
```pascal
if UserCastle.BoCastleUnderAttack then
   if (BoInFreePKArea) or (UserCastle.IsCastleWarArea (PEnvir, CX, CY)) then
      guildwarkill := TRUE;
```

---

### US-PVP-018: PvP物理伤害计算

**用户故事**: 作为战斗系统，我需要正确计算玩家间的物理伤害。

**优先级**: P0 (核心功能)
**复杂度**: 高

**验收标准**:
1. 伤害 = 攻击力 - 防御力
2. 防御力在AC范围内随机
3. 伤害最低为0
4. 特殊状态影响伤害

**技术实现要点**:
- 伤害计算: [ObjBase.pas:3431-3447](Source/Mir200/ObjBase.pas#L3431-L3447)
```pascal
function TCreature.GetHitStruckDamage (hiter: TCreature; damage: integer): integer;
begin
   armor := Lobyte(WAbil.AC) + Random(Hibyte(WAbil.AC)-Lobyte(WAbil.AC) + 1);
   damage := _MAX(0, damage - armor);
   Result := damage;
end;
```

---

### US-PVP-019: PvP魔法伤害计算

**用户故事**: 作为战斗系统，我需要正确计算玩家间的魔法伤害。

**优先级**: P0 (核心功能)
**复杂度**: 高

**验收标准**:
1. 伤害 = 魔法攻击力 - 魔法防御力
2. 魔法防御在MAC范围内随机
3. 伤害最低为0
4. 魔法护盾影响伤害

**技术实现要点**:
- 伤害计算: [ObjBase.pas:3449-3466](Source/Mir200/ObjBase.pas#L3449-L3466)
```pascal
function TCreature.GetMagStruckDamage (hiter: TCreature; damage: integer): integer;
begin
   armor := Lobyte(WAbil.MAC) + Random(Hibyte(WAbil.MAC)-Lobyte(WAbil.MAC) + 1);
   damage := _MAX(0, damage - armor);
   Result := damage;
end;
```

---

### US-PVP-020: 新手保护机制

**用户故事**: 作为10级以下新手，我需要受到保护，红名玩家无法攻击我。

**优先级**: P0 (核心功能)
**复杂度**: 中等

**验收标准**:
1. 10级以下白名玩家受保护
2. 红名(PKLevel>=2)无法攻击新手
3. 新手也无法攻击红名
4. 自由PK区内保护失效

**技术实现要点**:
- 保护逻辑: [ObjBase.pas:7931-7940](Source/Mir200/ObjBase.pas#L7931-L7940)

---

### US-PVP-021: 地图移动后攻击限制

**用户故事**: 作为刚传送到新地图的玩家，3秒内我无法攻击或被攻击。

**优先级**: P1 (重要功能)
**复杂度**: 低

**验收标准**:
1. 地图移动后3秒内无法攻击
2. 地图移动后3秒内无法被攻击
3. 3秒后恢复正常
4. 防止传送偷袭

**技术实现要点**:
- 限制逻辑: [ObjBase.pas:7943-7944](Source/Mir200/ObjBase.pas#L7943-L7944)

---

## 6. PK值与红名等级配置表

### 6.1 红名等级定义

| PK值范围 | PKLevel | 名字颜色 | 颜色代码 | 状态描述 |
|---------|---------|---------|---------|---------|
| 0-99 | 0 | 白名 | DefNameColor | 正常状态 |
| 100-199 | 1 | 黄名 | 251 | 轻度PK |
| 200+ | 2+ | 红名 | 249 | 重度PK |

### 6.2 PK值变化规则

| 行为 | PK值变化 | 条件 |
|-----|---------|------|
| 杀死白名玩家 | +100 | 非行会战/攻城战 |
| 杀死红名玩家 | 0 | - |
| 杀死灰名玩家 | 0 | 正当防卫 |
| 自然衰减 | -1 | 每2分钟 |
| GM命令清除 | 归零 | CmdDeletePKPoint |

### 6.3 灰名状态规则

| 属性 | 值 | 说明 |
|-----|---|------|
| 触发条件 | 先手攻击白名 | 非战斗区/行会战区 |
| 持续时间 | 60秒 | IllegalAttackTime |
| 显示颜色 | 47 | 紫色系 |
| 被杀免责 | 是 | IsGoodKilling返回TRUE |

---

## 7. 安全区配置表

### 7.1 安全区类型

| 类型 | 检测函数 | 范围 | 说明 |
|-----|---------|------|------|
| Lawfull地图 | PEnvir.Lawfull | 全图 | 地图属性配置 |
| 出生点安全区 | InSafeZone | 10格 | StartPoints列表 |
| 红名村安全区 | InSafeZone | 10格 | BADMANHOMEMAP |
| 行会战安全区 | InGuildWarSafeZone | 60格 | 行会战时扩大范围 |

### 7.2 区域标志位

| 标志 | 值 | 说明 |
|-----|---|------|
| AREA_FIGHT | $01 | 战斗区域(FightZone) |
| AREA_SAFE | $02 | 安全区域 |
| AREA_FREEPK | $04 | 自由PK区(攻城战) |

---

## 8. 边界场景处理表

| 场景 | 处理方式 | 代码位置 |
|-----|---------|---------|
| 安全区内攻击玩家 | 攻击无效，不触发任何效果 | ObjBase.pas:7927-7929 |
| 红名攻击10级以下新手 | 攻击无效(非自由PK区) | ObjBase.pas:7932-7934 |
| 新手攻击红名 | 攻击无效(非自由PK区) | ObjBase.pas:7936-7938 |
| 地图移动后3秒内攻击 | 攻击无效 | ObjBase.pas:7943-7944 |
| 攻击GM/石化状态玩家 | 攻击无效 | ObjBase.pas:8067-8068 |
| 行会战安全区内攻击 | 攻击无效(60格范围) | ObjBase.pas:3970 |
| 召唤兽攻击主人 | 攻击无效 | ObjBase.pas:7987 |
| 和平模式攻击玩家 | 攻击无效 | ObjBase.pas:8018-8021 |
| PK值溢出 | 无上限，持续累加 | ObjBase.pas:4005 |
| PK值下溢 | 最低为0 | ObjBase.pas:4017 |

---

## 9. 附录

### 9.1 关键常量汇总

| 常量名 | 值 | 说明 | 位置 |
|-------|---|------|------|
| HAM_ALL | 0 | 全体攻击模式 | Grobal2.pas:961 |
| HAM_PEACE | 1 | 和平模式 | Grobal2.pas:962 |
| HAM_GROUP | 2 | 编组模式 | Grobal2.pas:963 |
| HAM_GUILD | 3 | 行会模式 | Grobal2.pas:964 |
| HAM_PKATTACK | 4 | 善恶模式 | Grobal2.pas:965 |
| RC_USERHUMAN | 0 | 玩家角色类型 | Grobal2.pas:1705 |
| RC_ANIMAL | 50+ | 怪物类型起始值 | Grobal2.pas |

### 9.2 颜色代码表

| 颜色代码 | 含义 | 使用场景 |
|---------|------|---------|
| 249 | 红色 | 红名玩家(PKLevel>=2) |
| 251 | 黄色 | 黄名玩家(PKLevel=1) |
| 47 | 紫色 | 灰名玩家(BoIllegalAttack) |
| 180 | 蓝色 | 同行会/同盟成员 |
| 69 | 橙黄色 | 敌对行会成员 |
| 221 | 灰色 | 攻城战中立方 |

---

## 10. 文档修订历史

| 版本 | 日期 | 修订内容 |
|-----|------|---------|
| 1.0 | 2026-01-30 | 初始版本，包含PvP系统完整分析 |

