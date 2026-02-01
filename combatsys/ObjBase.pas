{ ============================================================================
  单元名称: ObjBase
  功能描述: 游戏对象基础类单元
  
  主要功能:
  - 定义游戏中所有生物对象的基类TCreature
  - 定义动物类TAnimal，继承自TCreature
  - 定义玩家类TUserHuman，继承自TAnimal
  - 提供生物对象的基础属性、状态、消息处理等功能
  - 实现战斗、移动、物品、技能等核心游戏逻辑
  
  设计说明:
  - TCreature是所有游戏生物的基类，包含HP/MP、攻防属性、状态等
  - TAnimal扩展了移动和AI相关功能
  - TUserHuman是玩家角色类，包含网络通信、命令处理等功能
  
  修改历史:
  - 2003/02/19 台湾方要求体验版等级以下不删除定量制费用
============================================================================ }
unit ObjBase;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Dialogs,
  ScktComp, syncobjs, MudUtil, HUtil32, Grobal2, Envir, EdCode;


{ ============================================================================
  常量定义区
  功能: 定义游戏中使用的各种常量值
============================================================================ }
const
   // 体力和魔力恢复相关常量
   HEALTHFILLTICK: integer = 300;   // 体力恢复间隔(毫秒)，测试服务器为1500
   SPELLFILLTICK: integer = 800;    // 魔力恢复间隔(毫秒)
   
   // 金币相关常量
   MAXGOLD = 2000000000;            // 最大金币数量(20亿)
   BAGGOLD = 50000000;              // 背包可携带最大金币(5000万)
   
   // 基础属性常量
   DEFHIT = 5;                      // 默认命中值
{$IFDEF FOR_ABIL_POINT}
   // 4/16日起适用
   DEFSPEED = 14;                   // 默认速度(能力点版本)
{$ELSE}
   DEFSPEED = 15;                   // 默认速度(普通版本)
{$ENDIF}
   EXPERIANCELEVEL = 7;             // 经验等级阈值

   // 管理员命令密码常量
   GET_A_CMD  = 6001001;            // 管理员命令码
   GET_SA_CMD = 6001010;            // 超级管理员命令码
   GET_A_PASSWD  = 31490000;        // 管理员密码
   GET_SA_PASSWD = 31490001;        // 超级管理员密码
   CHG_ECHO_PASSWD = 31490100;      // 修改回显密码
   GET_INFO_PASSWD = 31490200;      // 获取信息密码
   KIL_SERVER_PASSWD = 231493149;   // 关闭服务器密码

   // 台湾活动物品(死亡或断线会掉落，不能交易、交换、丢弃、寄存)
   TAIWANEVENTITEM = 51;

   // 默认属性值
   DEFHP = 14;                      // 默认HP基础值
   DEFMP = 11;                      // 默认MP基础值
   
   // 默认出生点坐标
   DEF_STARTX = 334;                // 默认出生点X坐标
   DEF_STARTY = 266;                // 默认出生点Y坐标
   
   // 红名玩家出生点
   BADMANHOMEMAP = '3';             // 红名玩家出生地图
   BADMANSTARTX = 845;              // 红名玩家出生X坐标
   BADMANSTARTY = 674;              // 红名玩家出生Y坐标
   
   // 系统限制常量
   MAXSAVELIMIT = 40;               // 最大保存限制(40个)
   MAXDEALITEM = 10;                // 最大交易物品数量(原为12，2004/12/24修改)
   MAXSLAVE = 1;                    // 最大召唤兽数量
   BODYLUCKUNIT = 5000;             // 身体幸运值单位
   GROUPMAX = 11;                   // 组队最大人数
   ANTI_MUKJA_DELAY = 2 * 60 * 1000; // 防刷延迟(2分钟)

   MAXGUILDMEMBER = 400;            // 行会最大成员数

   // 特殊戒指物品Shape值定义
   RING_TRANSPARENT_ITEM = 111;     // 隐身戒指
   RING_SPACEMOVE_ITEM = 112;       // 传送戒指
   RING_MAKESTONE_ITEM = 113;       // 麻痹戒指
   RING_REVIVAL_ITEM = 114;         // 复活戒指
   RING_FIREBALL_ITEM = 115;        // 火球戒指
   RING_HEALING_ITEM = 116;         // 治愈戒指
   RING_ANGERENERGY_ITEM = 117;     // 愤怒戒指
   RING_MAGICSHIELD_ITEM = 118;     // 护盾戒指
   RING_SUPERSTRENGTH_ITEM = 119;   // 力量戒指
   NECTLACE_FASTTRAINING_ITEM = 120; // 修炼项链
   NECTLACE_SEARCH_ITEM = 121;      // 探索项链

   // 套装物品Shape值定义
   RING_CHUN_ITEM = 122;            // 天套戒指
   NECKLACE_GI_ITEM = 123;          // 地套项链
   ARMRING_HAP_ITEM = 124;          // 合套手镯
   HELMET_IL_ITEM = 125;            // 一套头盔

   // 未知属性物品Shape值
   RING_OF_UNKNOWN   = 130;         // 未知戒指
   BRACELET_OF_UNKNOWN = 131;       // 未知手镯
   HELMET_OF_UNKNOWN = 132;         // 未知头盔

   // 魔力转体力物品Shape值
   RING_OF_MANATOHEALTH = 133;      // 魔力转体力戒指
   BRACELET_OF_MANATOHEALTH = 134;  // 魔力转体力手镯
   NECKLACE_OF_MANATOHEALTH = 135;  // 魔力转体力项链

   // 吸血物品Shape值
   RING_OF_SUCKHEALTH = 136;        // 吸血戒指
   BRACELET_OF_SUCKHEALTH = 137;    // 吸血手镯
   NECKLACE_OF_SUCKHEALTH = 138;    // 吸血项链
   // 2003/01/15 新增套装物品...世轮套、绿翠套、刀斧套
   // HP、MP、HP/MP提升套装物品
   RING_OF_HPUP      = 140;         // HP提升戒指
   BRACELET_OF_HPUP  = 141;         // HP提升手镯
   RING_OF_MPUP      = 142;         // MP提升戒指
   BRACELET_OF_MPUP  = 143;         // MP提升手镯
   RING_OF_HPMPUP    = 144;         // HP/MP提升戒指
   BRACELET_OF_HPMPUP= 145;         // HP/MP提升手镯
   
   // 2003/02/11 新增套装物品...五弦套、超魂套
   NECKLACE_OF_HPPUP = 146;         // HP百分比提升项链
   BRACELET_OF_HPPUP = 147;         // HP百分比提升手镯
   RING_OH_HPPUP     = 148;         // HP百分比提升戒指
   CCHO_WEAPON        = 23;         // 超魂武器
   CCHO_NECKLACE      = 149;        // 超魂项链
   CCHO_RING          = 150;        // 超魂戒指
   CCHO_HELMET        = 151;        // 超魂头盔
   CCHO_BRACELET      = 152;        // 超魂手镯
   
   // 2003/03/04 新增套装物品...破碎套、幻魔石套、灵灵玉套
   PSET_NECKLACE_SHAPE      = 153;  // 破碎套项链
   PSET_BRACELET_SHAPE      = 154;  // 破碎套手镯
   PSET_RING_SHAPE          = 155;  // 破碎套戒指
   HSET_NECKLACE_SHAPE      = 156;  // 幻魔石套项链
   HSET_BRACELET_SHAPE      = 157;  // 幻魔石套手镯
   HSET_RING_SHAPE          = 158;  // 幻魔石套戒指
   YSET_NECKLACE_SHAPE      = 159;  // 灵灵玉套项链
   YSET_BRACELET_SHAPE      = 160;  // 灵灵玉套手镯
   YSET_RING_SHAPE          = 161;  // 灵灵玉套戒指

   // 爱情套装物品
   HELMET_OF_LOVE   = 210;          // 爱情头盔
   NECKLACE_OF_LOVE = 211;          // 爱情项链
   RING_OF_LOVE     = 212;          // 爱情戒指
   BRACELET_OF_LOVE = 213;          // 爱情手镯

   // 药水Shape分类编号
   FASTFILL_ITEM = 1;               // 快速恢复药水
   FREE_UNKNOWN_ITEM = 2;           // 免费未知物品

   // stdmode = 3 的药品类型
   INSTANTABILUP_DRUG = 12;         // 瞬间能力提升药
   INSTANT_EXP_DRUG = 13;           // 瞬间经验药(吃后获得AC*100的经验值)

   // 附加魔法类型
   AM_FIREBALL = 1;                 // 火球术
   AM_HEALING = 2;                  // 治愈术



type
   { TSlaveInfo记录 - 召唤兽信息
     功能: 存储召唤兽的基本信息，用于跨服传输或保存 }
   TSlaveInfo = record
      SlaveName: string[14];      // 召唤兽名称
      SlaveExp: integer;          // 召唤兽经验值
      SlaveExpLevel: byte;        // 召唤兽经验等级
      SlaveMakeLevel: byte;       // 召唤兽制作等级
      RemainRoyalty: integer;     // 剩余忠诚时间(秒)
      HP: integer;                // 当前HP
      MP: integer;                // 当前MP
   end;
   PTSlaveInfo = ^TSlaveInfo;     // 召唤兽信息指针类型

   { TPkHiterInfo记录 - PK攻击者信息
     功能: 记录先攻击我的人，用于正当防卫判定 }
   TPkHiterInfo = record
      hiter: TObject;             // 攻击者对象
      hittime: longword;          // 攻击时间
   end;
   PTPkHiterInfo = ^TPkHiterInfo; // PK攻击者信息指针类型

   { TCreature类 - 生物基类
     功能: 游戏中所有生物对象的基类，包含怪物、NPC、玩家等
     用途: 提供生物的基础属性、状态、消息处理、战斗、移动等功能
     设计说明: 这是整个游戏对象系统的核心基类 }
   TCreature = class
      // ============ 需要保存到数据库的变量 ============
      MapName: string[16];        // 当前所在地图名称
      UserName: string[14];       // 角色名称
      CX: integer;                // 当前X坐标
      CY: integer;                // 当前Y坐标
      Dir: byte;                  // 面向方向(0-7)
      Sex: byte;                  // 性别(0:男 1:女)
      Hair: byte;                 // 发型
      Job: byte;                  // 职业(0:战士 1:法师 2:道士)
      Gold: integer;              // 携带金币数量
      Abil: TAbility;             // 基础能力属性
      CharStatus: integer;        // 角色状态标志
      StatusArr: array[0..11] of word;  // 各状态的剩余时间(秒)
      HomeMap: string[16];        // 出生点地图
      HomeX: integer;             // 出生点X坐标
      HomeY: integer;             // 出生点Y坐标
      NeckName: string[20];       // 称号
      PlayerKillingPoint: integer; // PK值
      AllowGroup: Boolean;        // 是否允许组队
      AllowEnterGuild: Boolean;   // 是否允许加入行会

      FreeGulityCount: byte;      // 免罪次数
      IncHealth: integer;         // 已吃的体力药量
      IncSpell: integer;          // 已吃的魔法药量
      IncHealing: integer;        // 已吃的治愈药量
      FightZoneDieCount: integer; // 行会战地图死亡次数，离开房间时重置
      DBVersion: integer;         // 数据库版本，2001-3-21修改经验和PK值
      BonusApply: byte;           // 是否已应用奖励点
      BonusAbil: TNakedAbility;   // 升级时增加的能力值
      CurBonusAbil: TNakedAbility; // 当前剩余的奖励能力值
      BonusPoint: integer;        // 奖励点数
      HungryState: longword;      // 饥饿状态
      TestServerResetCount: byte; // 测试服务器重置次数
      BodyLuck: Real;             // 身体幸运值
      BodyLuckLevel: integer;     // 计算后的幸运等级(不保存)
      CGHIUseTime: word;          // 天地合一使用时间(秒)
      BoEnableRecall: Boolean;    // 是否可被天地合一召唤

      DailyQuestNumber: word;     // 日常任务编号
      DailyQuestGetDate: word;    // 接取日常任务的日期(mon*31+day)

      QuestIndexOpenStates: array[0..MAXQUESTINDEXBYTE-1] of byte;  // 任务开启状态数组
      QuestIndexFinStates: array[0..MAXQUESTINDEXBYTE-1] of byte;   // 任务完成状态数组
      QuestStates: array[0..MAXQUESTBYTE-1] of byte;                // 任务状态数组

      // ============ 不需要保存的变量 ============
      CharStatusEx: integer;      // 扩展角色状态
      FightExp: integer;          // 战斗获得的经验值
      WAbil: TAbility;            // 工作能力属性(等级/经验用Abil，其他用WAbil)
      AddAbil: TAddAbility;       // 附加能力属性
      ViewRange: integer;         // 视野范围
      StatusTimes: array[0..11] of Longword;  // 状态剩余时间
      ExtraAbil: array[0..5] of byte;         // 额外能力值
      ExtraAbilTimes: array[0..5] of longword; // 额外能力持续时间(破坏/魔力/道力/攻速/体力/魔力)

      Appearance: word;           // 外观(怪物使用)
      RaceServer: byte;           // 服务器端种族类型
      RaceImage: byte;            // 种族图像编号
      AccuracyPoint: byte;        // 命中率，由武功计算
      HitPowerPlus: byte;         // 战士武功力量加成(1/3固定)
      HitDouble: byte;            // 双倍攻击(10=+100%, 25=+250%)
      CGHIstart: longword;        // 天地合一开始时间
      BoCGHIEnable: Boolean;      // 天地合一是否启用
      BoOldVersionUser_Italy: Boolean;  // 意大利旧版本用户修正
      BoReadyAdminPassword: Boolean;    // 管理员密码已就绪
      BoReadySuperAdminPassword: Boolean; // 超级管理员密码已就绪

      HealthRecover: byte;        // 体力恢复速度
      SpellRecover: byte;         // 魔力恢复速度
      AntiPoison: byte;           // 抗毒概率(特殊装备提供)
      PoisonRecover: byte;        // 中毒恢复时间
      AntiMagic: byte;            // 魔法闪避率
      Luck: integer;              // 幸运值
      PerHealth: integer;         // 每次体力恢复量
      PerHealing: integer;        // 每次治愈量
      PerSpell: integer;          // 每次魔力恢复量
      IncHealthSpellTime: longword; // 体力魔力恢复时间
      PoisonLevel: byte;          // 中毒等级(0..3)
      AvailableGold: integer;     // 可携带金币上限(体验版为1万)

      SpeedPoint: byte;           // 闪避率，由武功计算，武器过重会降低
      UserDegree: byte;           // 用户等级
      HitSpeed: shortint;         // 攻击速度(0:默认 -:慢 +:快)
      LifeAttrib: byte;           // 生命属性(0:生物 1:亡灵)
      CoolEye: byte;              // 看穿隐身概率(0:看不到 100:完全看到)

      // 组队相关变量
      GroupOwner: TCreature;      // 队长
      GroupMembers: TStringList;  // 队伍成员列表
      BoHearWhisper: Boolean;     // 是否接收密语
      BoHearCry: Boolean;         // 是否接收喊话
      BoHearGuildMsg: Boolean;    // 是否接收行会消息
      BoExchangeAvailable: Boolean; // 是否可交换
      WhisperBlockList: TStringList; // 密语屏蔽列表
      LatestCryTime: longword;    // 最后喊话时间

      // 召唤兽相关变量
      Master: TCreature;          // 主人(召唤兽使用)
      MasterRoyaltyTime: longword; // 对主人的忠诚维持时间
      SlaveLifeTime: longword;    // 召唤兽生存时间，超时则死亡
      SlaveExp: integer;          // 召唤兽经验值
      SlaveExpLevel: byte;        // 召唤兽等级，经验累积升级
      SlaveMakeLevel: byte;       // 召唤兽制作等级(3级)
      SlaveList: TList;           // 我召唤的怪物列表
      BoSlaveRelax: Boolean;      // 召唤兽模式(TRUE:休息 FALSE:攻击)

      // 攻击和外观相关
      HumAttackMode: byte;        // 攻击模式设置
      DefNameColor: byte;         // 默认名字颜色
      Light: integer;             // 亮度(0..5，人物默认2)
      BoGuildWarArea: Boolean;    // 当前在行会战/攻城战区域

      // 城堡相关
      Castle: TObject;            // 所属城堡(NPC使用)
      BoCrimeforCastle: Boolean;  // 是否攻击过城堡
      CrimeforCastleTime: longword; // 攻击城堡时间

      // 特殊属性标志
      NeverDie: Boolean;          // 永不死亡(NPC用)
      HoldPlace: Boolean;         // 是否占据位置
      BoFearFire: Boolean;        // 怕火属性，有火时不前进
      BoAnimal: Boolean;          // 动物(可屠宰出肉)
      BoNoItem: Boolean;          // 死亡不掉落物品
      HideMode: Boolean;          // 初始隐藏模式
      StickMode: Boolean;         // 不能移动的怪物
      RushMode: Boolean;          // 被魔法击中也能移动
      NoAttackMode: Boolean;      // 被攻击不反击(无攻击程序)
      NoMaster: Boolean;          // 不能被诱惑

      // 尸体相关
      BoSkeleton: Boolean;        // 是否只剩骨头
      MeatQuality: integer;       // 肉的质量
      BodyLeathery: integer;      // 身体韧性

      // 特殊状态
      BoHolySeize: Boolean;       // 被魔法定身，无法移动(仅怪物)
      HolySeizeStart: longword;   // 定身开始时间
      HolySeizeTime: longword;    // 定身持续时间(秒)
      BoCrazyMode: Boolean;       // 疯狂状态
      CrazyModeStart: longword;   // 疯狂开始时间
      CrazyModeTime: longword;    // 疯狂持续时间
      BoOpenHealth: Boolean;      // 体力被探气破研公开
      OpenHealthStart: longword;  // 体力公开开始时间
      OpenHealthTime: longword;   // 体力公开持续时间

      // 重叠状态
      BoDuplication: Boolean;     // 与其他角色重叠状态
      DupStartTime: longword;     // 重叠开始时间

      // 环境和生死状态
      PEnvir: TEnvirnoment;       // 当前所在环境(地图)
      BoGhost: Boolean;           // 是否已变成幽灵(待清除)
      GhostTime: longword;        // 变成幽灵的时间
      Death: Boolean;             // 是否死亡
      DeathTime: longword;        // 死亡时间
      DeathState: byte;           // 死亡状态(0:默认 1:只剩骨头)
      StruckTime: Longword;       // 受击时间
      WantRefMsg: Boolean;        // 是否需要刷新消息
      ErrorOnInit: Boolean;       // 初始化时是否出错
      SpaceMoved: Boolean;        // 是否已传送
      
      // 交易相关
      BoDealing: Boolean;         // 是否正在交易
      DealItemChangeTime: longword; // 最后修改交易物品的时间，交易前1秒内有修改则取消
      DealCret: TCreature;        // 交易对象，需检查nil
      
      // 行会相关
      MyGuild: TObject;           // 所属行会
      GuildRank: integer;         // 行会内排名(1:会长)
      GuildRankName: string;      // 行会内职位名称
      LatestNpcCmd: string;       // 最后与NPC对话的命令
      AttackSkillCount: integer;  // 攻击计数(用于刃风剑法)
      AttackSkillPointCount: integer; // 攻击计数中刃风剑触发的次数

      // 任务相关
      //HasTargetedCount: integer;  // 以我为目标的数量，每10分钟重置
      //StoneTargetFocusCount: integer;  //
      BoHasMission: Boolean;      // 是否有任务(活动用)
      Mission_X: integer;         // 任务目标X坐标
      Mission_Y: integer;         // 任务目标Y坐标
      
      // 隐身和特殊模式
      BoHumHideMode: Boolean;     // 怪物不可见模式
      BoStoneMode: Boolean;       // 石像模式(不可攻击但可见)
      BoViewFixedHide: Boolean;   // 可以看到隐身
      BoNextTimeFreeCurseItem: Boolean; // 下次可以取下诅咒物品

      BoFixedHideMode: Boolean;   // 固定位置隐身模式(移动则解除)
      BoSysopMode: Boolean;       // 系统管理员模式
      BoSuperviserMode: Boolean;  // 监督员模式
      BoEcho: Boolean;            // 是否回显
      BoTaiwanEventUser: Boolean; // 台湾活动用户(持有活动物品)
      TaiwanEventItemName: string; // 台湾活动物品名称

      // 特殊能力标志(来自特殊装备)
      BoAbilSpaceMove: Boolean;   // 可传送
      BoAbilMakeStone: Boolean;   // 麻痹戒指能力
      BoAbilRevival: Boolean;     // 复活戒指能力
      LatestRevivalTime: longword; // 最后复活时间
      BoAddMagicFireball: Boolean; // 火焰戒指，可使用火焰掌
      BoAddMagicHealing: Boolean; // 恢复戒指，可使用恢复术
      BoAbilAngerEnergy: Boolean; // 愤怒戒指能力
      BoMagicShield: Boolean;     // 保护戒指能力
      BoAbilSuperStrength: Boolean; // 力量戒指能力
      BoFastTraining: Boolean;    // 修炼戒指能力
      BoAbilSearch: Boolean;      // 探索能力
      BoAbilSeeHealGauge: Boolean; // 可看血条(道士探气破研1级以上)
      BoAbilMagBubbleDefence: Boolean; // 魔法泡泡防御能力
      MagBubbleDefenceLevel: byte; // 魔法泡泡防御等级

      // 时间控制变量
      SearchRate: longword;       // 搜索频率
      SearchTime: longword;       // 搜索时间
      RunTime: integer;           // 运行时间
      RunNextTick: integer;       // 下次运行Tick时间
      HealthTick: integer;        // 体力恢复Tick
      SpellTick: integer;         // 魔力恢复Tick

      // 目标和攻击者相关
      TargetCret: TCreature;      // 当前攻击目标
      TargetFocusTime: longword;  // 目标锁定时间
      LastHiter: TCreature;       // 最后攻击我的人
      LastHitTime: longword;      // 最后被攻击时间
      ExpHiter: TCreature;        // 获得经验的人
      ExpHitTime: longword;       // 获得经验的时间
      LatestSpaceMoveTime: longword; // 最后传送时间
      LatestSpaceScrollTime: longword; // 最后使用传送卷时间
      LatestSearchWhoTime: longword; // 最后搜索玩家时间
      MapMoveTime: longword;      // 地图移动时间，移动后3秒内不受攻击
      BoIllegalAttack: Boolean;   // 是否非法攻击
      IllegalAttackTime: longword; // 非法攻击时间

      // 特殊效果
      ManaToHealthPoint: integer; // 魔力转体力点数，负数则体力转魔力
      SuckupEnemyHealthRate: integer; // 吸血百分比
      SuckupEnemyHealth: Real;    // 吸取的敌人体力
      // 2003/03/04
      RefObjCount : integer;      // 视野内对象数量，大于0时AI运作

      // 定时器变量
      poisontime: longword;       // 中毒时间
      time10min: longword;        // 10分钟定时器
      time500ms: longword;        // 500毫秒定时器
      time30sec: longword;        // 30秒定时器
      time10sec: longword;        // 10秒定时器
      time5sec: longword;         // 5秒定时器
      ticksec: longword;          // 秒定时器

   private
      // 私有变量
      MsgList: TList;             // 消息列表{synchronize}
      MsgTargetList: TStringList; // 我发送行为的目标对象列表
      VisibleItems:  TList;       // 可见物品列表
      VisibleEvents: TList;       // 可见事件列表
      WatchTime: longword;        // 监视时间
      FBoInFreePKArea: Boolean;   // 当前是否在自由PK区域

      PKHiterList: TList;         // 先攻击我的人列表(仅限玩家)

      { SetBoInFreePKArea - 设置自由PK区域标志 }
      procedure SetBoInFreePKArea (flag: Boolean);

   protected
      // 保护变量
      FindPathRate: integer;      // 寻路频率
      FindpathTime: longword;     // 寻路时间
      HitTime: integer;           // 攻击时间
      WalkTime: integer;          // 行走时间
      SearchEnemyTime: longword;  // 搜索敌人时间

      AreaStateOrNameChanged: Boolean; // 区域状态或名称是否已改变

   public
      // 公共变量
      VisibleActors: TList;       // 可见生物列表(不能在其他线程使用)
      ItemList: TList;            // 物品列表(PTUserItem)，不能在其他线程使用
      DealList: TList;            // 交易中的物品列表(PTUserItem)
      DealGold: integer;          // 交易金币数量
      BoDealSelect: Boolean;      // 是否已点击交换按钮
      MagicList: TList;           // 魔法列表(PTUserMagic)
      // 2003/03/15 物品栏扩展
      UseItems: array[0..12] of TUserItem; // 装备栏(8->12)
      SaveItems: TList;           // 保存物品列表(PTUserItem)
      NextWalkTime: integer;      // 下次行走时间
      WalkStep: integer;          // 行走步数
      WalkCurStep: integer;       // 当前行走步数
      WalkWaitTime: integer;      // 行走等待时间
      WalkWaitCurTime: longword;  // 当前行走等待时间
      BoWalkWaitMode: Boolean;    // 行走等待模式
      NextHitTime: integer;       // 下次攻击时间

      // 武功技能指针
      PSwordSkill: PTUserMagic;   // 基础剑法，魔法删除时注意
      PPowerHitSkill: PTUserMagic; // 刃风剑法(1/3概率触发)
      PLongHitSkill: PTUserMagic; // 刃风剑术
      PWideHitSkill: PTUserMagic; // 半月弯刀
      PFireHitSkill: PTUserMagic; // 烈火剑法
      // 2003/03/15 新武功
      PCrossHitSkill: PTUserMagic; // 狂风斩

      // 武功允许标志
      BoAllowPowerHit: Boolean;   // 允许刃风剑法
      BoAllowLongHit: Boolean;    // 允许刃风剑术
      BoAllowWideHit: Boolean;    // 允许半月弯刀
      BoAllowFireHit: Boolean;    // 允许烈火剑法
      // 2003/03/15 新武功
      BoAllowCrossHit: Boolean;   // 允许狂风斩
      LatestFireHitTime: longword; // 最后烈火剑法时间
      LatestRushRushTime: longword; // 最后冲锋时间

      // ============ 构造/析构方法 ============
      constructor Create;                // 构造函数
      destructor Destroy; override;      // 析构函数
      
      // ============ 消息处理方法 ============
      procedure  SendFastMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);  // 发送快速消息(插入队列头部)
      procedure  SendMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);      // 发送消息
      procedure  SendDelayMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string; delay: integer{ms});  // 发送延迟消息
      procedure  UpdateDelayMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string; delay: integer{ms});  // 更新延迟消息
      procedure  UpdateDelayMsgCheckParam1 (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string; delay: integer{ms});  // 更新延迟消息(检查参数1)
      procedure  UpdateMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);    // 更新消息
      function   GetMsg (var msg: TMessageInfo): Boolean; Dynamic;  // 获取消息
      
      // ============ 视野和地图方法 ============
      function   GetMapCreatures (penv: TEnvirnoment; x, y, area: integer; rlist: TList): Boolean;  // 获取地图区域内的生物
      procedure  SendRefMsg (msg, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);   // 发送刷新消息给周围玩家
      procedure  UpdateVisibleGay (cret: TCreature);        // 更新可见生物
      procedure  UpdateVisibleItems (xx, yy: word; pmi: PTMapItem);  // 更新可见物品
      procedure  UpdateVisibleEvents (xx, yy: integer; mevent: TObject);  // 更新可见事件
      procedure  SearchViewRange;                           // 搜索视野范围
      function   Feature: integer;                          // 获取外观特征
      function   GetRelFeature (who: TCreature): integer;   // 获取相对外观(根据对方可能不同)
      function   GetCharStatus: integer;                    // 获取角色状态
      
      // ============ 初始化和运行方法 ============
      procedure  InitValues;                                // 初始化属性值
      procedure  Initialize; dynamic;                       // 初始化
      procedure  Finalize; dynamic;                         // 结束化
      procedure  RunMsg (msg: TMessageInfo); dynamic;       // 运行消息处理
      procedure  UseLamp;                                   // 使用灯
      procedure  Run; dynamic;                              // 主运行循环
      
      // ============ 状态变化方法 ============
      procedure  FeatureChanged;                            // 外观已改变
      procedure  CharStatusChanged;                         // 角色状态已改变
      procedure  UserNameChanged;                           // 用户名已改变
      function   Appear: Boolean;                           // 出现
      function   Disappear: Boolean;                        // 消失
      procedure  KickException;                             // 踢出异常
      
      // ============ 移动方法 ============
      function   Walk (msg: integer): Boolean;              // 行走
      function   EnterAnotherMap (enterenvir: TEnvirnoment; enterx, entery: integer): Boolean;  // 进入另一地图
      procedure  Turn (dir: byte);                          // 转向
      function   RunTo (dir: integer; allowdup: Boolean): Boolean;   // 跑向指定方向
      function   WalkTo (dir: integer; allowdup: Boolean): Boolean;  // 走向指定方向
      
      // ============ 聊天方法 ============
      procedure  Say (saystr: string); dynamic;             // 说话
      procedure  SysMsg (str: string; mode: integer);       // 系统消息
      procedure  NilMsg (str: string);                      // 空消息
      procedure  GroupMsg (str: string);                    // 组队消息
      
      // ============ 死亡和物品掉落方法 ============
      procedure  MakeGhost;                                 // 变成幽灵(消失)
      procedure  ScatterBagItems (itemownership: TObject);  // 散落背包物品
      procedure  DropEventItems;                            // 掉落活动物品
      procedure  ScatterGolds (itemownership: TObject);     // 散落金币
      procedure  ApplyMeatQuality;                          // 应用肉质量
      function   TakeCretBagItems (target: TCreature): Boolean;  // 拿取目标背包物品
      procedure  DropUseItems (itemownership: TObject);     // 掉落装备物品
      procedure  Die; dynamic;                              // 死亡
      procedure  Alive; dynamic;                            // 复活
      
      // ============ PK相关方法 ============
      procedure  SetLastHiter (hiter: TCreature);           // 设置最后攻击者
      procedure  AddPkHiter (hiter: TCreature);             // 添加PK攻击者(先攻击我的人)
      procedure  CheckTimeOutPkHiterList;                   // 检查超时的PK攻击者列表
      procedure  ClearPkHiterList;                          // 清空PK攻击者列表
      function   IsGoodKilling (target: TCreature): Boolean; // 是否正当防卫
      
      // ============ 武功设置方法 ============
      procedure  SetAllowLongHit (boallow: Boolean);        // 设置允许刃风剑术
      procedure  SetAllowWideHit (boallow: Boolean);        // 设置允许半月弯刀
      function   SetAllowFireHit: Boolean;                  // 设置允许烈火剑法
      // 2003/03/15 新武功
      procedure  SetAllowCrossHit (boallow: Boolean);       // 设置允许狂风斩

      // ============ 体力魔力和传送方法 ============
      procedure  IncHealthSpell (hp, mp: integer);          // 增加体力魔力
      procedure  RandomSpaceMove (mname: string; mtype: integer);  // 随机传送
      procedure  SpaceMove (mname: string; nx, ny, mtype: integer);  // 传送到指定坐标
      procedure  UserSpaceMove (mname, xstr, ystr: string); // 用户传送
      function   UseScroll (Shape: integer): Boolean;       // 使用卷轴
      
      // ============ 武器相关方法 ============
      function   MakeWeaponGoodLock: Boolean;               // 使武器幸运
      function   RepaireWeaponNormaly: Boolean;             // 普通修复武器
      function   RepaireWeaponPerfect: Boolean;             // 完美修复武器
      function   UseLotto: Boolean;                         // 使用彩票
      
      // ============ 特殊状态方法 ============
      procedure  MakeHolySeize (htime: integer);            // 使目标定身
      procedure  BreakHolySeize;                            // 解除定身
      procedure  MakeCrazyMode (csec: integer);             // 进入疯狂模式
      procedure  BreakCrazyMode;                            // 解除疯狂模式
      procedure  MakeOpenHealth;                            // 公开体力
      procedure  BreakOpenHealth;                           // 取消公开体力
      
      // ============ 伤害计算方法 ============
      function   GetHitStruckDamage (hiter: TCreature; damage: integer): integer;   // 计算物理伤害(考虑防御)
      function   GetMagStruckDamage (hiter: TCreature; damage: integer): integer;   // 计算魔法伤害(考虑魔防)
      procedure  StruckDamage (damage: integer; hiter : TCreature = nil );          // 受到伤害处理
      procedure  DamageHealth (damage: integer);            // 扣除体力
      procedure  DamageSpell (val: integer);                // 扣除魔力
      
      // ============ 经验值相关方法 ============
      function   CalcGetExp (targlevel, targhp: integer): integer;  // 计算获得的经验值
      procedure  GainExp (exp: longword);                   // 获得经验
      procedure  GainSlaveExp (exp: integer);               // 召唤兽获得经验
      procedure  ApplySlaveLevelAbilitys;                   // 应用召唤兽等级能力
      procedure  WinExp (exp: longword);                    // 赢得经验
      procedure  HasLevelUp (prevlevel: integer);           // 升级处理
      
      // ============ 金币和负重方法 ============
      function   IncGold (igold: integer): Boolean;         // 增加金币
      function   DecGold (igold: integer): Boolean;         // 减少金币
      function   CalcBagWeight: integer;                    // 计算背包重量
      function   CalcWearWeightEx (windex: integer): integer; // 计算装备重量(排除指定位置)
      
      // ============ 能力重算方法 ============
      procedure  RecalcLevelAbilitys;                       // 重算等级能力
      //procedure  RecalcLevelAbilitys_old;
      procedure  RecalcHitSpeed;                            // 重算攻击速度
      procedure  AddMagicWithItem (magic: integer);         // 通过物品添加魔法
      procedure  DelMagicWithItem (magic: integer);         // 通过物品删除魔法
      procedure  ItemDamageRevivalRing;                     // 复活戒指损伤
      procedure  RecalcAbilitys; dynamic;                   // 重算能力值
      procedure  ApplyItemParameters (uitem: TUserItem; var aabil: TAddAbility);  // 应用物品参数
      // 2003/03/15 物品栏扩展
      procedure  ApplyItemParametersEx (uitem: TUserItem; var AWabil: TAbility);  // 应用物品参数扩展
      function   GetMyAbility: TAbility;                    // 获取我的能力值
      function   GetNextLevelExp (lv: integer): longword;   // 获取下一级所需经验
      procedure  MakeWeaponUnlock;                          // 使武器变为不幸运
      procedure  TrainSkill (pum: PTUserMagic; train: integer);  // 训练技能
      function   GetMyLight: integer;                       // 获取我的亮度

      // ============ 等级和安全区方法 ============
      procedure  ChangeLevel (level: integer);              // 改变等级
      function   InSafeZone: Boolean;                       // 是否在安全区
      function   InGuildWarSafeZone: Boolean;               // 是否在行会战安全区
      
      // ============ PK和名字颜色方法 ============
      function   PKLevel: integer;                          // 获取PK等级
      procedure  ChangeNameColor;                           // 改变名字颜色
      function   MyColor: byte;                             // 获取我的颜色
      function   GetThisCharColor (cret: TCreature): byte;  // 获取指定角色颜色
      function   GetGuildRelation (onecret, twocret: TCreature): integer;  // 获取行会关系
      function   IsGuildMaster: Boolean;                    // 是否是行会会长
      procedure  IncPKPoint (point: integer);               // 增加PK值
      procedure  DecPKPoint (point: integer);               // 减少PK值
      procedure  AddBodyLuck (r: Real);                     // 增加身体幸运值
      function   GetUserName: string;                       // 获取用户名
      function   GetHungryState: integer;                   // 获取饥饿状态
      
      // ============ 任务标记方法 ============
      function   GetQuestMark (idx: integer): integer;      // 获取任务标记(0或非0)
      procedure  SetQuestMark (idx, value: integer);        // 设置任务标记(0或1)
      function   GetQuestOpenIndexMark (idx: integer): integer;   // 获取任务开启标记
      procedure  SetQuestOpenIndexMark (idx, value: integer);     // 设置任务开启标记
      function   GetQuestFinIndexMark (idx: integer): integer;    // 获取任务完成标记
      procedure  SetQuestFinIndexMark (idx, value: integer);      // 设置任务完成标记

      // ============ 攻击相关方法 ============
      procedure  DoDamageWeapon (wdam: integer);            // 武器损伤
      function   GetAttackPower (damage, ranval: integer): integer;  // 获取攻击力
      function   _Attack (hitmode: word; targ: TCreature): Boolean;  // 攻击内部方法
      procedure  HitHit (target: TCreature; hitmode, dir: word);     // 击中处理
      procedure  HitMotion (hitmsg: integer; hitdir: byte; x, y: integer);  // 攻击动作
      procedure  HitHit2 (target: TCreature; hitpwr, magpwr: integer; all: Boolean);  // 击中处理2
      procedure  HitHitEx2 (target: TCreature; rmmsg, hitpwr, magpwr: integer; all: Boolean);  // 击中处理扩展2
      function   CharPushed (ndir, pushcount: integer): integer;     // 角色被推
      function   CharRushRush (ndir, rushlevel: integer): Boolean;   // 角色冲锋
      function   SiegeCount: integer;                       // 被围攻数量
      function   SiegeLockCount: integer;                   // 被困住数量
      
      // ============ 中毒相关方法 ============
      function   MakePoison (poison, sec, poisonlv: integer): Boolean;  // 使中毒
      procedure  ClearPoison (poison: integer);             // 清除中毒
      
      // ============ 目标选择方法 ============
      function   CheckAttackRule2 (target: TCreature): Boolean; dynamic;  // 检查攻击规则2
      function   _IsProperTarget (target: TCreature): Boolean; dynamic;   // 是否合适目标(内部)
      function   IsProperTarget (target: TCreature): Boolean; dynamic;    // 是否合适目标
      function   IsProperFriend (target: TCreature): Boolean; dynamic;    // 是否合适友方
      procedure  SelectTarget (target: TCreature); dynamic; // 选择目标
      procedure  LoseTarget; dynamic;                       // 失去目标

      // ============ 目标范围检测方法 ============
      function   TargetInAttackRange (target: TCreature; var targdir: byte): Boolean;  // 目标是否在攻击范围内
      function   TargetInSpitRange (target: TCreature; var targdir: byte): Boolean;    // 目标是否在喷射范围内
      function   TargetInCrossRange (target: TCreature; var targdir: byte): Boolean;   // 目标是否在十字范围内
      function   GetFrontCret: TCreature;                     // 获取前方生物
      function   GetBackCret: TCreature;                      // 获取后方生物
      function   CretInNearXY (tagcret: TCreature; xx, yy: integer): Boolean;  // 生物是否在附近坐标
      
      // ============ 召唤兽相关方法 ============
      function   MakeSlave (sname: string; slevel, max_slave, royaltysec: integer): TCreature;  // 创建召唤兽
      // 2003/06/12 召唤兽补丁
      procedure  ClearAllSlaves;                              // 清除所有召唤兽
      function   ExistAttackSlaves: Boolean;                  // 是否存在攻击中的召唤兽

      // ============ 组队相关方法 ============
      function   IsGroupMember (cret: TCreature): Boolean;    // 是否是队伍成员
      function   CheckGroupValid: Boolean;                    // 检查队伍是否有效
      procedure  DelGroupMember (who: TCreature);             // 删除队伍成员
      procedure  EnterGroup (gowner: TCreature);              // 加入队伍
      procedure  LeaveGroup;                                  // 离开队伍
      procedure  DenyGroup;                                   // 拒绝组队

      // ============ 物品管理方法 ============
      function   IsEnoughBag: Boolean;                        // 背包是否足够
      procedure  WeightChanged;                               // 负重已改变
      procedure  GoldChanged;                                 // 金币已改变
      procedure  HealthSpellChanged;                          // 体力魔力已改变
      function   IsAddWeightAvailable (addweight: integer): Boolean;  // 是否可以增加负重
      function   FindItemName (iname: string): PTUserItem;    // 按名称查找物品
      function   FindItemNameEx (iname: string; var count, durasum, duratop: integer): PTUserItem;  // 按名称查找物品(扩展)
      function   FindItemWear (iname: string; var count: integer): PTUserItem;  // 查找装备物品
      function   CanAddItem: Boolean;                         // 是否可以添加物品
      function   AddItem (pu: PTUserItem): Boolean;           // 添加物品(失败时需注意)
      function   DelItem (svindex: integer; iname: string): Boolean;  // 删除物品
      function   DelItemIndex (bagindex: integer): Boolean;   // 按索引删除物品
      function   DeletePItemAndSend (pcheckitem: PTUserItem): Boolean;  // 删除物品并发送
      function   CanTakeOn (index: integer; ps: PTStdItem): Boolean;   // 是否可以穿戴
      function   GetDropPosition (x, y, wide: integer; var dx, dy: integer): Boolean;  // 获取掉落位置
      function   GetRecallPosition (x, y, wide: integer; var dx, dy: integer): Boolean;  // 获取召唤位置
      function   DropItemDown (ui: TUserItem; scatterrange: integer; diedrop: Boolean; ownership, droper: TObject): Boolean;  // 掉落物品
      function   DropGoldDown (goldcount: integer; diedrop: Boolean; ownership, droper: TObject): Boolean;  // 掉落金币
      function   UserDropItem (itmname: string; itemindex: integer): Boolean;  // 用户丢弃物品
      function   UserDropGold (dropgold: integer): Boolean;   // 用户丢弃金币
      function   PickUp: Boolean;                             // 拾取
      function   EatItem (std: TStdItem; pu: PTUserItem): Boolean;  // 使用物品
      
      // ============ 魔法相关方法 ============
      function   IsMyMagic (magid: integer): Boolean;         // 是否是我的魔法
      function   ReadBook (std: TStdItem): Boolean;           // 阅读书籍
      function   DoSpell (pum: PTUserMagic; xx, yy: integer; target: TCreature): Boolean;  // 施放魔法
      function   GetSpellPoint (pum: PTUserMagic): integer;   // 获取魔法消耗
      function   MagPassThroughMagic (sx, sy, tx, ty, ndir, magpwr: integer; undeadattack: Boolean): integer;  // 穿透魔法
      function   MagCanHitTarget (sx, sy: integer; target: TCreature): Boolean;  // 魔法是否可以击中目标
      function   MagDefenceUp (sec: integer): Boolean;        // 防御提升魔法
      function   MagMagDefenceUp (sec: integer): Boolean;     // 魔防提升魔法
      function   MagMakeDefenceArea (xx, yy, range, sec: integer; BoMag: Boolean): integer;  // 创建防御区域
      function   MagDcUp (sec: integer; BoSlaveDCUp: Boolean): Boolean;  // 攻击力提升魔法
      function   MagBubbleDefenceUp (mlevel, sec: integer): Boolean;    // 泡泡防御提升
      procedure  DamageBubbleDefence;                         // 泡泡防御损伤
      function   CheckMagicLevelup (pum: PTUserMagic): Boolean;  // 检查魔法升级
      procedure  CheckMagicSpecialAbility (pum: PTUserMagic); // 检查魔法特殊能力

      // ============ 日常任务方法 ============
      function   GetDailyQuest: integer;                      // 获取当前日常任务(过期或无则返回0)
      procedure  SetDailyQuest (qnumber: integer);            // 设置日常任务

      // ============ 属性 ============
      property   BoInFreePKArea: Boolean read FBoInFreePKArea write SetBoInFreePKArea;  // 是否在自由PK区域

      // 2003/03/18
      procedure  DecRefObjCount;                              // 减少引用对象计数
   end;

   { TAnimal类 - 动物类
     功能: 继承自TCreature，扩展了移动和AI相关功能
     用途: 作为怪物和玩家的基类，提供目标追踪、攻击、受击等功能 }
   TAnimal = class (TCreature)
   private
   public
      TargetX: integer;             // 目标X坐标
      TargetY: integer;             // 目标Y坐标
      BoRunAwayMode: Boolean;       // 逃跑模式
      RunAwayStart: longword;       // 逃跑开始时间
      RunAwayTime: integer;         // 逃跑持续时间
      
      constructor Create;                                     // 构造函数
      procedure RunMsg (msg: TMessageInfo); override;         // 运行消息处理
      procedure Run; override;                                // 主运行循环
      procedure MonsterNormalAttack;                          // 怪物普通攻击
      procedure MonsterDetecterAttack;                        // 怪物侦测攻击
      procedure SetTargetXY (x, y: integer); dynamic;         // 设置目标坐标
      procedure GotoTargetXY; dynamic;                        // 移动到目标坐标
      procedure Wondering; dynamic;                           // 徘徊/游荡
      procedure Attack (target: TCreature; dir: byte); dynamic;  // 攻击
      procedure Struck (hiter: TCreature); dynamic;           // 受击
      procedure LoseTarget; override;                         // 失去目标
   end;

   { TUserHuman类 - 玩家角色类
     功能: 继承自TAnimal，实现玩家角色的所有功能
     用途: 处理玩家的网络通信、命令处理、交易、行会等功能 }
   TUserHuman = class (TAnimal)
   private
      Def: TDefaultMessage;         // 默认消息
      SendBuffers: TList;           // 发送缓冲区列表

      // 防刷屏相关
      LatestSayStr: string;         // 最后说的话(防刷屏)
      BombSayCount: integer;        // 刷屏计数
      BombSayTime: longword;        // 刷屏时间
      BoShutUpMouse: Boolean;       // 是否被禁言
      ShutUpMouseTime: longword;    // 禁言时间
      
      // 操作时间控制
      operatetime: longword;        // 操作时间
      operatetime_30sec: longword;  // 30秒操作时间
      operatetime_sec: longword;    // 秒操作时间
      operatetime_500m: longword;   // 500毫秒操作时间
      boGuildwarsafezone: Boolean;  // 是否在行会战安全区

      // 时间同步
      FirstClientTime: longword;    // 首次客户端时间
      FirstServerTime: longword;    // 首次服务器时间

      // ============ 移动和攻击私有方法 ============
      function  TurnXY (x, y, dir: integer): Boolean;         // 在指定坐标转向
      function  WalkXY (x, y: integer): Boolean;              // 走到指定坐标
      function  RunXY (x, y: integer): Boolean;               // 跑到指定坐标
      procedure GetRandomMineral;                             // 获取随机矿石
      procedure GetRandomGems;                                // 获取随机宝石
      function  DigUpMine (x, y: integer): Boolean;           // 挖矿
      function  TargetInSwordLongAttackRange: Boolean;        // 目标是否在长剑攻击范围
      function  HitXY (hitid, x, y, dir: integer): Boolean;   // 在指定坐标攻击
      function  GetMagic (mid: integer): PTUserMagic;         // 获取魔法
      function  SpellXY (magid, targetx, targety, targcret: integer): Boolean;  // 在指定坐标施法
      function  SitdownXY (x, y, dir: integer): Boolean;      // 在指定坐标坐下

      // ============ 服务器处理私有方法 ============
      procedure GetQueryUserName (target: TCreature; x, y: integer);  // 查询用户名
      procedure ServerSendAdjustBonus;                        // 服务器发送调整奖励
      procedure ServerGetOpenDoor (dx, dy: integer);          // 服务器处理开门
      procedure ServerGetTakeOnItem (where: byte; svindex: integer; itmname: string);   // 服务器处理穿戴物品
      procedure ServerGetTakeOffItem (where: byte; svindex: integer; itmname: string);  // 服务器处理脱下物品
      procedure ServerGetEatItem (svindex: integer; itmname: string);  // 服务器处理使用物品
      procedure ServerGetButch (animal: TCreature; x, y, ndir: integer);  // 服务器处理屠宰
      procedure ServerGetMagicKeyChange (magid, key: integer);  // 服务器处理魔法快捷键改变
      procedure ServerGetClickNpc (clickid: integer);         // 服务器处理点击NPC
      procedure ServerGetMerchantDlgSelect (npcid: integer; clickstr: string);  // 服务器处理商人对话选择
      procedure ServerGetMerchantQuerySellPrice (npcid, itemindex: integer; itemname: string);  // 服务器处理查询出售价格
      procedure ServerGetMerchantQueryRepairPrice (npcid, itemindex: integer; itemname: string);  // 服务器处理查询修理价格
      procedure ServerGetUserSellItem (npcid, itemindex: integer; itemname: string);  // 服务器处理用户出售物品
      procedure ServerGetUserRepairItem (npcid, itemindex: integer; itemname: string);  // 服务器处理用户修理物品
      procedure ServerSendStorageItemList (npcid: integer);   // 服务器发送仓库物品列表
      procedure ServerGetUserStorageItem (npcid, itemindex: integer; itemname: string);  // 服务器处理用户存储物品
      procedure ServerGetMakeDrug (npcid: integer; itemname: string);  // 服务器处理制药
      procedure ServerGetUserMenuBuy (msg, npcid, MakeIndex, menuindex: integer; itemname: string);  // 服务器处理用户菜单购买
      
      // ============ 组队私有方法 ============
      procedure RefreshGroupMembers;                          // 刷新队伍成员
      procedure ServerGetCreateGroup (withwho: string);       // 服务器处理创建队伍
      procedure ServerGetAddGroupMember (withwho: string);    // 服务器处理添加队员
      procedure ServerGetDelGroupMember (withwho: string);    // 服务器处理删除队员
      
      // ============ 交易私有方法 ============
      procedure ServerGetDealTry (withwho: string);           // 服务器处理尝试交易
      procedure ServerGetDealAddItem (iidx: integer; iname: string);   // 服务器处理添加交易物品
      procedure ServerGetDealDelItem (iidx: integer; iname: string);   // 服务器处理删除交易物品
      procedure ServerGetDealChangeGold (dgold: integer);     // 服务器处理修改交易金币
      procedure ServerGetDealEnd;                             // 服务器处理交易结束
      procedure ServerGetTakeBackStorageItem (npcid, itemserverindex: integer; iname: string);  // 服务器处理取回仓库物品
      procedure ServerGetWantMiniMap;                         // 服务器处理请求小地图
      
      // ============ 行会私有方法 ============
      procedure SendChangeGuildName;                          // 发送改变行会名
      procedure ServerGetQueryUserState (who: TCreature; xx, yy: integer);  // 服务器处理查询用户状态
      procedure ServerGetOpenGuildDlg;                        // 服务器处理打开行会对话框
      procedure ServerGetGuildHome;                           // 服务器处理行会主页
      procedure ServerGetGuildMemberList;                     // 服务器处理行会成员列表
      procedure ServerGetGuildAddMember (who: string);        // 服务器处理添加行会成员
      procedure ServerGetGuildDelMember (who: string);        // 服务器处理删除行会成员
      procedure ServerGetGuildUpdateNotice (body: string);    // 服务器处理更新行会公告
      procedure ServerGetGuildUpdateRanks (body: string);     // 服务器处理更新行会职位
      procedure ServerGetAdjustBonus (remainbonus: integer; bodystr: string);  // 服务器处理调整奖励
      procedure ServerGetGuildMakeAlly;                       // 服务器处理行会结盟
      procedure ServerGetGuildBreakAlly (gname: string);      // 服务器处理行会解除结盟

      // ============ 其他私有方法 ============
      procedure RmMakeSlaveProc (pslave: PTSlaveInfo);        // 远程创建召唤兽处理
      procedure SendLoginNotice;                              // 发送登录公告
      procedure ServerGetNoticeOk;                            // 服务器处理公告确认
   public
      // ============ 用户身份信息 ============
      UserId: string[10];           // 用户ID
      UserAddress: string;          // 用户地址
      UserHandle: integer;          // 用户句柄
      UserGateIndex: integer;       // 网关中的用户索引(提高网关速度)
      //SocData: string;
      LastSocTime: longword;        // 最后套接字时间
      GateIndex: integer;           // 网关索引
      ClientVersion: integer;       // 客户端版本
      LoginClientVersion: integer;  // 登录时客户端版本
      ClientCheckSum: integer;      // 客户端校验和
      LoginDateTime: TDateTime;     // 首次登录时间
      LoginTime: longword;          // 登录时间(Tick)

      // ============ 用户状态 ============
      ReadyRun: Boolean;            // 是否准备运行
      Certification: integer;       // 认证状态
      ApprovalMode: integer;        // 审批模式(1:体验用户 2:正式用户 3:免费用户)
      AvailableMode: integer;       // 可用模式(1:个人定额 2:个人定量 3:网吧定额 4:网吧定量)

      // ============ 连接状态 ============
      UserConnectTime: longword;    // 用户连接时间
      ChangeToServerNumber: integer; // 切换到的服务器编号
      EmergencyClose: Boolean;      // 是否需要强制断开连接
      UserSocketClosed: Boolean;    // 用户套接字已关闭(战斗中角色保留5秒)
      UserRequestClose: Boolean;    // 用户请求关闭
      SoftClosed: Boolean;          // 软关闭
      BoSaveOk: Boolean;            // 是否保存成功
      // 2003/06/12 召唤兽补丁
      PrevServerSlaves: TList;      // 上一服务器的召唤兽列表(PTSlaveInfo)
      TempStr: string;              // 临时字符串
      BoChangeServerOK: Boolean;    // 切换服务器是否成功

      // ============ 服务器切换相关 ============
      BoChangeServer: Boolean;      // 是否正在切换服务器
      WriteChangeServerInfoCount: integer;  // 写入切换服务器信息计数
      ChangeMapName: string;        // 切换地图名
      ChangeCX, ChangeCY: integer;  // 切换坐标
      BoChangeServerNeedDelay: Boolean;  // 切换服务器是否需要延迟
      ChangeServerDelayTime: Longword;   // 切换服务器延迟时间

      // ============ 反外挂检测 ============
      HumStruckTime: longword;      // 人物受击时间
      ClientMsgCount: integer;      // 客户端消息计数
      ClientSpeedHackDetect: integer;  // 客户端加速检测
      LatestSpellTime: longword;    // 最后施法时间
      LatestSpellDelay: integer;    // 最后施法延迟
      LatestHitTime: longword;      // 最后攻击时间
      LatestWalkTime: longword;     // 最后行走时间
      HitTimeOverCount: integer;    // 攻击时间超时计数
      HitTimeOverSum: integer;      // 攻击时间超时总和
      SpellTimeOverCount: integer;  // 施法时间超时计数
      WalkTimeOverCount: integer;   // 行走时间超时计数
      WalkTimeOverSum: integer;     // 行走时间超时总和
      SpeedHackTimerOverCount: integer;  // 加速定时器超时计数
      MustRandomMove: Boolean;      // 重连时是否随机位置

      // ============ 任务相关 ============
      CurQuest: pointer;            // 当前任务(PTQuestRecord)
      CurQuestNpc: TCreature;       // 当前任务NPC
      CurSayProc: pointer;          // 当前对话处理
      QuestParams: array[0..9] of integer;  // 任务参数
      DiceParams: array[0..9] of integer;   // 骰子参数

      // ============ 时间召回相关 ============
      BoTimeRecall: Boolean;        // 是否时间到后返回原位
      TimeRecallEnd: longword;      // 时间召回结束时间
      TimeRecallMap: string;        // 时间召回地图
      TimeRecallX, TimeRecallY: integer;  // 时间召回坐标

      // ============ 其他状态 ============
      PriviousCheckCode: byte;      // 上一次检查码
      CrackWanrningLevel: integer;  // 破解警告等级
      LastSaveTime: longword;       // 最后保存时间
      Bright: integer;              // 地图亮度
      FirstTimeConnection: Boolean; // 是否首次创建角色登录
      BoSendNotice: Boolean;        // 是否发送公告
      LoginSign: Boolean;           // 登录标志
      BoServerShifted: Boolean;     // 是否服务器转移重连
      BoAccountExpired: Boolean;    // 账号是否过期

      // ============ 公告相关 ============
      LineNoticeTime: longword;     // 滚动公告时间
      LineNoticeNumber: integer;    // 滚动公告编号

      // ============ 构造/析构方法 ============
      constructor Create;                                     // 构造函数
      destructor Destroy; override;                           // 析构函数
      procedure Initialize; override;                         // 初始化
      procedure Finalize; override;                           // 结束化
      
      // ============ 状态重置方法 ============
      procedure ResetCharForRevival;                          // 死亡后重置状态
      procedure Clear_5_9_bugitems;                           // 清除5.9版本BUG物品
      procedure Reset_6_28_bugitems;                          // 重置6.28版本BUG物品
      function  GetUserMassCount: integer;                    // 获取用户质量计数
      procedure WriteConLog;                                  // 写入连接日志
      
      // ============ 网络发送方法 ============
      procedure SendSocket (pmsg: PTDefaultMessage; body: string);  // 发送套接字数据
      procedure SendDefMessage (msg, recog, param, tag, series: integer; addstr: string);  // 发送默认消息
      procedure GuildRankChanged (rank: integer; rname: string);    // 行会职位已改变
      
      // ============ 管理员命令 ============
      procedure ChangeSkillLevel (magname: string; lv: byte);       // 改变技能等级
      procedure CmdMakeFullSkill (magname: string; lv: byte);       // 命令:制作满级技能
      procedure CmdMakeOtherChangeSkillLevel (who, magname: string; lv: byte);  // 命令:修改他人技能等级
      procedure CmdDeletePKPoint (whostr: string);                  // 命令:删除PK值
      procedure CmdSendPKPoint (whostr: string);                    // 命令:发送PK值
      procedure CmdChangeJob (jobname: string);                     // 命令:改变职业
      procedure CmdChangeSex;                                       // 命令:改变性别
      procedure CmdCallMakeMonster (monname, param: string);        // 命令:召唤怪物
      procedure CmdCallMakeSlaveMonster (monname, param: string; momlv: byte);  // 命令:召唤召唤兽怪物
      procedure CmdMissionSetting (xstr, ystr: string);             // 命令:任务设置
      procedure CmdCallMakeMonsterXY (xstr, ystr, monname, countstr: string);   // 命令:在指定坐标召唤怪物
      procedure CmdMakeItem (itmname: string; count: integer);      // 命令:制作物品
      procedure CmdRefineWeapon (dc, mc, sc, acc: integer);         // 命令:精炼武器
      procedure CmdDeleteUserGold (whostr, goldstr: string);        // 命令:删除用户金币
      procedure CmdAddUserGold (whostr, goldstr: string);           // 命令:添加用户金币
      procedure RCmdUserChangeGoldOk (whostr: string; igold: integer);  // 远程命令:用户金币修改成功
      procedure CmdFreeSpaceMove (map, xstr, ystr: string);         // 命令:自由传送
      procedure CmdRushAttack;                                      // 命令:冲锋攻击
      procedure CmdManLevelChange (man: string; level: integer);    // 命令:修改用户等级
      procedure CmdManExpChange (man: string; exp: integer);        // 命令:修改用户经验
      procedure CmdEraseItem (itmname, countstr: string);           // 命令:删除物品
      procedure CmdRecallMan (man: string);                         // 命令:召回用户
      procedure CmdReconnection (saddr, sport: string);             // 命令:重新连接
      procedure CmdReloadGuild (gname: string);                     // 命令:重新加载行会
      procedure CmdReloadGuildAll (gname: string);                  // 命令:重新加载所有行会
      procedure CmdKickUser (uname: string);                        // 命令:踢出用户
      procedure CmdTingUser (uname: string);                        // 命令:禁言用户
      procedure CmdTingRangeUser (uname, rangestr: string);         // 命令:范围禁言用户
      procedure CmdCreateGuild (gname, mastername: string);         // 命令:创建行会
      procedure CmdDeleteGuild (gname: string);                     // 命令:删除行会
      procedure CmdGetGuildMatchPoint (gname: string);              // 命令:获取行会比赛积分
      procedure CmdStartGuildMatch;                                 // 命令:开始行会比赛
      procedure CmdEndGuildMatch;                                   // 命令:结束行会比赛
      procedure CmdAnnounceGuildMembersMatchPoint (gname: string);  // 命令:公布行会成员比赛积分
      function  GetLevelInfoString (cret: TCreature): string;       // 获取等级信息字符串
      procedure CmdSendUserLevelInfos (whostr: string);             // 命令:发送用户等级信息
      procedure CmdSendMonsterLevelInfos;                           // 命令:发送怪物等级信息
      procedure CmdChangeUserCastleOwner (gldname: string; pass: Boolean);  // 命令:修改城堡所有者
      procedure CmdReloadNpc (cmdstr: string);                      // 命令:重新加载NPC
      procedure CmdOpenCloseUserCastleMainDoor (cmdstr: string);    // 命令:开关城堡大门
      procedure CmdAddShutUpList (whostr, minstr: string; pass: Boolean);   // 命令:添加禁言列表
      procedure CmdDelShutUpList (whostr: string; pass: Boolean);   // 命令:删除禁言列表
      procedure CmdSendShutUpList;                                  // 命令:发送禁言列表
      
      // ============ 通用命令 ============
      procedure CmdEraseMagic (magname: string);                    // 命令:删除魔法
      procedure CmdThisManEraseMagic (whostr, magname: string);     // 命令:删除指定用户魔法
      procedure GuildDeclareWar (gname: string);                    // 行会宣战
      
      // ============ 发送物品/魔法方法 ============
      procedure SendAddItem (ui: TUserItem);                        // 发送添加物品
      procedure SendUpdateItem (ui: TUserItem);                     // 发送更新物品
      procedure SendDelItem (ui: TUserItem);                        // 发送删除物品
      procedure SendDelItems (ilist: TStringList);                  // 发送删除多个物品
      procedure SendBagItems;                                       // 发送背包物品
      procedure SendUseItems;                                       // 发送装备物品
      procedure SendAddMagic (pum: PTUserMagic);                    // 发送添加魔法
      procedure SendDelMagic (pum: PTUserMagic);                    // 发送删除魔法
      procedure SendMyMagics;                                       // 发送我的魔法列表
      
      // ============ 聊天方法 ============
      procedure Whisper (whostr, saystr: string);                   // 密语
      procedure WhisperRe (saystr: string);                         // 密语回复
      procedure BlockWhisper (whostr: string);                      // 屏蔽密语
      function  IsBlockWhisper (whostr: string): Boolean;           // 是否屏蔽密语
      procedure Say (saystr: string); override;                     // 说话
      
      // ============ 运行和保存方法 ============
      procedure ThinkEtc;                                           // 思考等
      procedure ReadySave;                                          // 准备保存
      procedure SendLogon;                                          // 发送登录
      procedure SendAreaState;                                      // 发送区域状态
      procedure DoStartupQuestNow;                                  // 立即执行启动任务
      procedure Operate;                                            // 操作处理
      procedure RunNotice;                                          // 运行公告
      procedure GetGetNotices;                                      // 获取公告
      function  GetStartX: integer;                                 // 获取起始X坐标
      function  GetStartY: integer;                                 // 获取起始Y坐标
      procedure CheckHomePos;                                       // 检查出生点位置
      procedure GuildSecession;                                     // 行会退出
      procedure CmdSendTestQuestDiary (unitnum: integer);           // 命令:发送测试任务日志

      // ============ 交易方法 ============
      procedure ResetDeal;                                          // 重置交易
      procedure StartDeal (who: TUserHuman);                        // 开始交易
      procedure BrokeDeal;                                          // 中断交易
      procedure ServerGetDealCancel;                                // 服务器处理取消交易
      procedure AddDealItem (uitem: TUserItem);                     // 添加交易物品
      procedure DelDealItem (uitem: TUserItem);                     // 删除交易物品
      function  IsReservedMakingSlave: Boolean;                     // 是否预约了创建召唤兽(服务器转移时)

   end;

implementation

uses
   svMain, M2Share, ObjNpc, Magic, LocalDB, Guild, UsrEngn, Event,
   IdSrvClient;

procedure InitializeObjBase;
begin
end;

constructor TCreature.Create;
begin
   BoGhost := FALSE;
   GhostTime := 0;
   Death := FALSE;
   DeathTime := 0;
   WatchTime := GetTickCount;
   Dir := DR_DOWN;
   RaceServer := RC_ANIMAL;
   RaceImage := 0;
   Hair := 0;
   Job := 0; //전사 1:술사  2: 도사
   Gold := 0;  //가지도 있는 돈
   Appearance := 0;
   HoldPlace := TRUE;
   ViewRange := 5;
   HomeMap := '0'; //기본맵
   NeckName:= '';
   UserDegree := 0;
   Light := 0;
   DefNameColor := 255;
   HitPowerPlus := 0;
   HitDouble := 0;
   BodyLuck := 0;
   CGHIUseTime := 0;
   CGHIstart := GetTickCount;
   BoCGHIEnable := FALSE;
   BoOldVersionUser_Italy := FALSE;
   BoReadyAdminPassword := FALSE;
   BoReadySuperAdminPassword := FALSE;


   BoFearFire := FALSE;
   BoAbilSeeHealGauge := FALSE;

   BoAllowPowerHit := FALSE;  //true: 다음에 한번 HitPowerPlus가 가능함
   BoAllowLongHit := FALSE;
   BoAllowWideHit := FALSE;
   BoAllowFireHit := FALSE;
   // 2003/03/15 신규무공
   BoAllowCrossHit := FALSE;

   AccuracyPoint := DEFHIT;
   SpeedPoint := DEFSPEED;
   HitSpeed := 0;
   LifeAttrib := LA_CREATURE; //일반 생명있는 몬스터
   AntiPoison := 0;
   PoisonRecover := 0;
   HealthRecover := 0;
   SpellRecover := 0;
   AntiMagic := 0;
   Luck := 0;
   IncSpell := 0;
   IncHealth := 0;
   IncHealing := 0;
   PerHealth := 5;
   PerHealing := 5;
   PerSpell := 5;
   IncHealthSpellTime := GetTickCount;
   PoisonLevel := 0;
   FightZoneDieCount := 0;
   AvailableGold := BAGGOLD;

   CharStatus := 0;
   CharStatusEx := 0;
   FillChar (StatusArr, sizeof(word)*12, #0);
   FillChar (BonusAbil, sizeof(TNakedAbility), #0);
   FillChar (CurBonusAbil, sizeof(TNakedAbility), #0);
   FillChar (ExtraAbil, sizeof(byte) * 6, #0);
   FillChar (ExtraAbilTimes, sizeof(longword) * 6, #0);

   AllowGroup := FALSE;
   AllowEnterGuild := FALSE;
   FreeGulityCount := 0;

   HumAttackMode := HAM_ALL;
   FBoInFreePKArea := FALSE;
   BoGuildWarArea := FALSE;
   BoCrimeforCastle := FALSE;

   NeverDie := FALSE;
   BoSkeleton := FALSE;  //죽어서 뼈만 남았는지 여부
   RushMode := FALSE;
   BoHolySeize := FALSE;
   BoCrazyMode := FALSE;
   BoOpenHealth := FALSE;

   BoDuplication := FALSE;

   //동물인경우 잡아서 고기가 나온다.
   BoAnimal := FALSE;
   BoNoItem := FALSE;
   BodyLeathery := 50; //기본값
   HideMode := FALSE;
   StickMode := FALSE;
   NoAttackMode := FALSE;
   NoMaster := FALSE;
   BoIllegalAttack := FALSE;

   ManaToHealthPoint := 0;
   SuckupEnemyHealthRate := 0;
   SuckupEnemyHealth := 0;

   FillChar (AddAbil, sizeof(TAddAbility), 0);

   MsgList := TList.Create;
   MsgTargetList := TStringList.Create;
   PKHiterList := TList.Create;

   VisibleActors := TList.Create;
   VisibleItems  := TList.Create;
   VisibleEvents := TList.Create;
   ItemList := TList.Create;
   DealList := TList.Create;
   DealGold := 0;
   MagicList := TList.Create;
   SaveItems := TList.Create;
   // 2003/03/15 아이템 인벤토리 확장
   FillChar (UseItems, sizeof(TUserItem)*13, #0);   // 9->13

   PSwordSkill := nil;
   PPowerHitSkill := nil;
   PLongHitSkill := nil;
   PWideHitSkill := nil;
   PFireHitSkill := nil;
   // 2003/03/15 신규무공
   PCrossHitSkill := nil;

   GroupOwner := nil;
   Castle := nil;

   Master := nil;
   SlaveExp := 0;
   SlaveExpLevel := 0;
   BoSlaveRelax := FALSE; //기본 상태, 보이면 공격 모드

   GroupMembers := TStringList.Create;
   BoHearWhisper := TRUE;
   BoHearCry := TRUE;
   BoHearGuildMsg := TRUE;
   BoExchangeAvailable := TRUE;
   BoEnableRecall := FALSE;
   DailyQuestNumber := 0;  ///설정 안되었음    //*dq
   DailyQuestGetDate := 0;

   WhisperBlockList := TStringList.Create;
   SlaveList := TList.Create;
   FillChar (QuestStates, sizeof(QuestStates), #0);
   FillChar (QuestIndexOpenStates, sizeof(QuestIndexOpenStates), #0);
   FillChar (QuestIndexFinStates, sizeof(QuestIndexFinStates), #0);

   with Abil do begin
      Level := 1;
      AC    := 0;
      MAC   := 0;
      DC    := MakeWord(1,4);  //동물의 기본 공격
      MC    := MakeWord(1,2);
      SC    := MakeWord(1,2);
      MP    := 15;
      HP    := 15;
      MaxHP := 15;
      MaxMP := 15;
      Exp   := 0;
      MaxExp := 50;
      Weight := 0;
      MaxWeight := 100;
   end;

   WantRefMsg := FALSE;
   BoDealing := FALSE;
   DealCret := nil;
   MyGuild := nil;
   GuildRank := 0;
   GuildRankName := '';
   LatestNpcCmd := '';

   BoHasMission := FALSE;
   BoHumHideMode := FALSE;
   BoStoneMode := FALSE;
   BoViewFixedHide := FALSE;
   BoNextTimeFreeCurseItem := FALSE;

   BoFixedHideMode := FALSE;
   BoSysopMode := FALSE;
   BoSuperviserMode := FALSE;
   BoEcho := TRUE;
   BoTaiwanEventUser := FALSE;

   RunTime := GetCurrentTime + Random (1500);
   RunNextTick := 250;
   SearchRate := 2000 + longword(Random (2000));
   SearchTime := GetTickCount;
   time10min := GetTickCount;
   time500ms := GetTickCount;
   poisontime := GetTickCount;
   time30sec := GetTickCount;
   time10sec := GetTickCount;
   time5sec := GetTickCount;
   ticksec := GetTickCount;
   LatestCryTime := 0; //GetTickCount;
   LatestSpaceMoveTime := 0;
   LatestSpaceScrollTime := 0;
   LatestSearchWhoTime := 0;
   MapMoveTime := GetTickCount;
   SlaveLifeTime := 0;

   NextWalkTime := 1400;
   NextHitTime := 3000;
   WalkCurStep := 0;
   WalkWaitCurTime := GetTickCount;
   BoWalkWaitMode := FALSE;

   HealthTick := 0;
   SpellTick := 0;

   TargetCret := nil;
   LastHiter := nil;
   ExpHiter := nil;
   // 2003/03/04
   RefObjCount := 0;
end;

destructor TCreature.Destroy;
var
   i: integer;
begin
   try
      for i:=0 to MsgList.Count-1 do begin
         //메모리를 추가로 해제해야 하는 경우
         if PTMessageInfoPtr(MsgList[i]).Ident = RM_DELITEMS then
            if PTMessageInfoPtr(MsgList[i]).lparam1 <> 0 then
               TStringList (PTMessageInfoPtr(MsgList[i]).lparam1).Free;
         if PTMessageInfoPtr(MsgList[i]).Ident = RM_MAKE_SLAVE then
            if PTMessageInfoPtr(MsgList[i]).lparam1 <> 0 then
               Dispose (PTSlaveInfo(PTMessageInfoPtr(MsgList[i]).lparam1));
         if PTMessageInfoPtr(MsgList[i]).descptr <> nil then
            FreeMem (PTMessageInfoPtr(MsgList[i]).descptr);
         Dispose (PTMessageInfoPtr (MsgList[i]));
      end;
      MsgList.Free;
      MsgTargetList.Free;
      for i:=0 to PKHiterList.Count-1 do
         Dispose (PTPkHiterInfo (PKHiterList[i]));
      PKHiterList.Free;
      // 2003/03/18
      i := 0;
      while TRUE do begin
         if i >= VisibleActors.Count then break;
         {
         try
            if(PTVisibleActor(VisibleActors[i]).cret <> nil) then
               TCreature (PTVisibleActor(VisibleActors[i]).cret).DecRefObjCount;
         except
            MainOutMessage ('[Exception] TCreatre.Destroy : Visible Actor Dec RefObjCount');
         end;
         }
         Dispose (PTVisibleActor (VisibleActors[i]));
         VisibleActors.Delete(i);
//       Inc(i);
      end;
      VisibleActors.Free;
      for i:=0 to VisibleItems.Count-1 do
         Dispose (PTVisibleItemInfo (VisibleItems[i]));
      VisibleItems.Free;
      VisibleEvents.Free;

      for i:=0 to ItemList.Count-1 do
         Dispose (PTUserItem (ItemList[i]));
      ItemList.Free;
      for i:=0 to DealList.Count-1 do
         Dispose (PTUserItem (DealList[i]));
      DealList.Free;
      for i:=0 to MagicList.Count-1 do
         Dispose (PTUserMagic (MagicList[i]));
      MagicList.Free;
      for i:=0 to SaveItems.Count-1 do
         Dispose (PTUserItem (SaveItems[i]));
      SaveItems.Free;
      GroupMembers.Free;
      WhisperBlockList.Free;
      SlaveList.Free;
   except
      MainOutMessage ('[Exception] TCreature.Destroy ' + UserName );
   end;

   inherited Destroy;
end;

procedure TCreature.SetBoInFreePKArea (flag: Boolean);
begin
   if FBoInFreePKArea <> flag then begin
      FBoInFreePKArea := flag;
      AreaStateOrNameChanged := TRUE;
   end;
end;

//2003/03/18
procedure TCreature.DecRefObjCount;
var i : integer;
   pva: PTVisibleActor;
begin
   Dec(RefObjCount);
   if(RefObjCount <= 0) and (VisibleActors.Count > 0) then begin
      RefObjCount := 0;
      i := 0;
      while TRUE do begin
         if i >= VisibleActors.Count then break;
         pva := PTVisibleActor(VisibleActors[i]);
         VisibleActors.Delete (i);
         Dispose (pva);
         Inc(i);
      end;
   end;
end;

procedure TCreature.SendFastMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);
var
	pmsg: PTMessageInfoPtr;
begin
   try
      csObjMsgLock.Enter;
      if not BoGhost then begin
         new (pmsg);
         pmsg.Ident 	:= Ident;
         pmsg.wparam  := wparam;
         pmsg.lparam1 := lparam1;
         pmsg.lparam2 := lparam2;
         pmsg.lParam3 := lparam3;
         pmsg.sender	:= sender;
         if str <> '' then begin
            try
               GetMem (pmsg.descptr, Length(str) + 1);
               Move (str[1], pmsg.descptr^, Length(str)+1);
            except
               pmsg.descptr := nil;
            end;
         end else
            pmsg.descptr := nil;
         MsgList.Insert (0, pmsg);
      end;
   finally
      csObjMsgLock.Leave;
   end;
end;

procedure TCreature.SendMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);
var
	pmsg: PTMessageInfoPtr;
begin
   try
      csObjMsgLock.Enter;
      if not BoGhost then begin
         new (pmsg);
         pmsg.Ident 	:= Ident;
         pmsg.wparam  := wparam;
         pmsg.lparam1 := lparam1;
         pmsg.lparam2 := lparam2;
         pmsg.lParam3 := lparam3;
         pmsg.deliverytime := 0;
         pmsg.sender	:= sender;
         if str <> '' then begin
            try
               GetMem (pmsg.descptr, Length(str) + 1);
               Move (str[1], pmsg.descptr^, Length(str)+1);
            except
               pmsg.descptr := nil;
            end;
         end else
            pmsg.descptr := nil;
         MsgList.Add (pmsg);
      end;
   finally
      csObjMsgLock.Leave;
   end;
end;

procedure  TCreature.SendDelayMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string; delay: integer{ms});
var
	pmsg: PTMessageInfoPtr;
begin
   try
      csObjMsgLock.Enter;
      if not BoGhost then begin
         new (pmsg);
         pmsg.Ident 	:= Ident;
         pmsg.wparam  := wparam;
         pmsg.lparam1 := lparam1;
         pmsg.lparam2 := lparam2;
         pmsg.lParam3 := lparam3;
         pmsg.deliverytime := GetTickCount + longword(delay);
         pmsg.sender	:= sender;
         if str <> '' then begin
            try
               GetMem (pmsg.descptr, Length(str) + 1);
               Move (str[1], pmsg.descptr^, Length(str)+1);
            except
               pmsg.descptr := nil;
            end;
         end else
            pmsg.descptr := nil;
         MsgList.Add (pmsg);
      end;
   finally
      csObjMsgLock.Leave;
   end;
end;

procedure TCreature.UpdateDelayMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string; delay: integer{ms});
var
   i: integer;
	pmsg: PTMessageInfoPtr;
begin
   try
      csObjMsgLock.Enter;
      i := 0;
      while TRUE do begin
      	if i >= MsgList.Count then break;
      	if PTMessageInfoPtr (MsgList[i]).Ident = Ident then begin
            pmsg := PTMessageInfoPtr (MsgList[i]);
            MsgList.Delete (i);
            if pmsg.descptr <> nil then
               FreeMem (pmsg.descptr);
            Dispose (pmsg);
         end else
         	Inc (i);
      end;
	finally
      csObjMsgLock.Leave;
	end;
   SendDelayMsg (sender, Ident, wparam, lParam1, lParam2, lParam3, str, delay);
end;

procedure TCreature.UpdateDelayMsgCheckParam1 (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string; delay: integer{ms});
var
   i: integer;
	pmsg: PTMessageInfoPtr;
begin
   try
      csObjMsgLock.Enter;
      i := 0;
      while TRUE do begin
      	if i >= MsgList.Count then break;
      	if (PTMessageInfoPtr (MsgList[i]).Ident = Ident) and (PTMessageInfoPtr (MsgList[i]).lparam1 = lparam1) then begin
            pmsg := PTMessageInfoPtr (MsgList[i]);
            MsgList.Delete (i);
            if pmsg.descptr <> nil then
               FreeMem (pmsg.descptr);
            Dispose (pmsg);
         end else
         	Inc (i);
      end;
	finally
      csObjMsgLock.Leave;
	end;
   SendDelayMsg (sender, Ident, wparam, lParam1, lParam2, lParam3, str, delay);
end;

procedure TCreature.UpdateMsg (sender: TCreature; Ident, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);
var
   i: integer;
	pmsg: PTMessageInfoPtr;
begin
   try
      csObjMsgLock.Enter;
      i := 0;
      while TRUE do begin
      	if i >= MsgList.Count then break;
      	if PTMessageInfoPtr (MsgList[i]).Ident = Ident then begin
            pmsg := PTMessageInfoPtr (MsgList[i]);
            MsgList.Delete (i);
            if pmsg.descptr <> nil then
               FreeMem (pmsg.descptr);
            Dispose (pmsg);
         end else
         	Inc (i);
      end;
	finally
      csObjMsgLock.Leave;
	end;
   SendMsg (sender, Ident, wparam, lParam1, lParam2, lParam3, str);
end;

function TCreature.GetMsg (var msg: TMessageInfo): Boolean;
var
	pmsg: PTMessageInfoPtr;
   n: integer;
begin
	Result := FALSE;
   try
      csObjMsgLock.Enter;
      n := 0;
      msg.Ident := 0;
      while MsgList.Count > n do begin
         pmsg := MsgList[n];
         if pmsg.deliverytime <> 0 then begin
            if GetTickCount < pmsg.deliverytime then begin
               Inc (n);
               continue;
            end;
         end;
         MsgList.Delete (n);
         msg.Ident := pmsg.Ident;
         msg.wparam  := pmsg.wparam;
         msg.lparam1 := pmsg.lparam1;
         msg.lparam2 := pmsg.lparam2;
         msg.lParam3 := pmsg.lparam3;
         msg.sender	:= pmsg.sender;
         if pmsg.descptr <> nil then begin
            msg.Description := StrPas (pmsg.descptr);
            FreeMem (pmsg.descptr);
         end else
            msg.Description := '';
         Dispose (pmsg);
         Result := TRUE;
         break;
      end;
	finally
      csObjMsgLock.Leave;
	end;
end;


function  TCreature.GetMapCreatures (penv: TEnvirnoment; x, y, area: integer; rlist: TList): Boolean;
var
	i, j, k, stx, sty, enx, eny: integer;
   cret: TCreature;
   pm: PTMapInfo;
   inrange: Boolean;
begin
   Result := FALSE;
   if rlist = nil then exit;
   try
      stx := x-area;
      enx := x+area;
      sty := y-area;
      eny := y+area;

      for i:=stx to enx do begin
         for j:=sty to eny do begin
            inrange := PEnvir.GetMapXY (i, j, pm);
            if inrange then begin
               if pm.ObjList <> nil then begin
                  for k:=pm.ObjList.Count-1 downto 0 do begin
                     //creature//
                     if pm.ObjList[k] <> nil then begin
                        if PTAThing (pm.ObjList[k]).Shape = OS_MOVINGOBJECT then begin
                           cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                           if cret <> nil then begin
                              if (not cret.BoGhost) then begin
                                 rlist.Add (cret);
                              end;
                           end;
                        end;
                     end;
                  end;
               end;
            end;
         end;
      end;
   except
      MainOutMessage ('[TCreature] GetMapCreatures exception');
   end;
   Result := TRUE;

end;


{ SendRefMsg - 发送刷新消息
  功能: 向周围视野范围内的所有生物发送消息
  参数:
    msg - 消息类型
    wparam - 字参数
    lParam1, lParam2, lParam3 - 长整型参数
    str - 字符串参数
  实现原理:
    1. 如果是管理员模式或隐身模式则不发送
    2. 每500毫秒重新扫描周围生物并缓存
    3. 遍历周围坐标(CX-12到CX+12, CY-12到CY+12)
    4. 检查并清理超过5分钟的残影对象
    5. 向玩家发送所有消息，向怪物只发送受击/听见/死亡消息
    6. 使用缓存列表提高效率 }
procedure TCreature.SendRefMsg (msg, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);
var
	i, j, k, stx, sty, enx, eny: integer;  // 循环变量和坐标范围
   cret: TCreature;                       // 生物对象
   pm: PTMapInfo;                         // 地图信息指针
   inrange: Boolean;                      // 是否在范围内
begin
   // 管理员模式或隐身模式不发送消息
   if BoSuperviserMode or HideMode then begin
      exit;
   end;
   try
      csObjMsgLock.Enter;  // 进入临界区
      // 每500毫秒或缓存为空时重新扫描周围生物
      if (GetTickCount - WatchTime >= 500) or (MsgTargetList.Count = 0) then begin
         WatchTime := GetTickCount;
         MsgTargetList.Clear;
         // 计算扫描范围(24x24)
         stx := CX-12;
         enx := CX+12;
         sty := CY-12;
         eny := CY+12;
         // 遍历周围所有坐标
         for i:=stx to enx do begin
            for j:=sty to eny do begin
               inrange := PEnvir.GetMapXY (i, j, pm);
               if inrange then begin
                  if pm.ObjList <> nil then begin
                     // 倒序遍历以便安全删除
                     for k:=pm.ObjList.Count-1 downto 0 do begin
                        // 检查生物对象
                        if pm.ObjList[k] <> nil then begin
                           if PTAThing (pm.ObjList[k]).Shape = OS_MOVINGOBJECT then begin
                              // 检查是否超过5分钟的残影
                              if GetTickCount - PTAThing (pm.ObjList[k]).ATime >= 5 * 60 * 1000 then begin
                                 // 检查并清除残影
                                 try // 2003-08-21 PDS - 内存删除时出错的情况
                                 Dispose (PTAThing (pm.ObjList[k]));
                                 except
                                 MainOutMessage ('[Exception] Dispose Error - SendRefMsg');
                                 end;
                                 pm.ObjList.Delete(k);
                                 if pm.ObjList.Count <= 0 then begin
                                    pm.ObjList.Free;
                                    pm.ObjList := nil;
                                    break;
                                 end;
                              end else begin
                                 try
                                    cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                                    if cret <> nil then begin
                                       // 检查是否为有效生物(非幽灵状态)
                                       if (not cret.BoGhost) then begin
                                          // 玩家接收所有消息
                                          if cret.RaceServer = RC_USERHUMAN then begin
                                             cret.SendMsg (self, msg, wparam, lparam1, lparam2, lparam3, str);
                                             MsgTargetList.AddObject ('', cret); // 缓存目标
                                          end else begin
                                             // 怪物只接收受击/听见/死亡消息
                                             if cret.WantRefMsg then
                                                if (msg = RM_STRUCK) or (msg = RM_HEAR) or (msg = RM_DEATH) then begin
                                                   cret.SendMsg (self, msg, wparam, lparam1, lparam2, lparam3, str);
                                                   MsgTargetList.AddObject ('', cret); // 缓存目标
                                                end;
                                          end;
                                       end;
                                    end;
                                 except
                                    // 异常时删除对象
                                    pm.ObjList.Delete (k);
                                    if pm.ObjList.Count <= 0 then begin
                                       pm.ObjList.Free;
                                       pm.ObjList := nil;
                                    end;
                                    MainOutMessage ('[Exception] TCreatre.SendRefMsg');
                                    break;
                                 end;
                              end;
                           end;
                        end;
                     end;
                  end;
               end;
            end;
         end;
      end else begin
         // 使用缓存列表发送消息
      	if MsgTargetList.Count > 0 then
            for i:=0 to MsgTargetList.Count-1 do begin
               cret := TCreature (MsgTargetList.Objects[i]);
               try
                 // 检查目标是否有效且在范围内
                 if not cret.BoGhost then begin
                    if (cret.MapName = self.MapName) and (Abs(cret.CX - self.CX) <= 11) and (Abs(cret.CY - self.CY) <= 11) then begin
                       if cret.RaceServer = RC_USERHUMAN then begin
                         cret.SendMsg (self, msg, wparam, lparam1, lparam2, lparam3, str);
                       end else begin
                          // 怪物只接收特定消息
                          if cret.WantRefMsg and ((msg = RM_STRUCK) or (msg = RM_HEAR) or (msg = RM_DEATH)) then
                             cret.SendMsg (self, msg, wparam, lparam1, lparam2, lparam3, str);
                       end;
                    end;
                 end;

               except
               // 目标列表中的对象损坏时删除
               // 可能持有已删除的内存，所以不删除内存
               // 只从列表中删除
               // 循环可能中断，所以用Break跳出(可能导致消息发送不完整)
               // 2003-08-07 : PDS
                  MainOutMessage ('[Exception] TCreatre.SendRefMsg : Target Wrong :'+Self.UserName);
                  MsgTargetList.Delete(i);
                  break;
               end;

            end; // for end...
      end;
	finally
      csObjMsgLock.Leave;  // 离开临界区
	end;
end;

{ UpdateVisibleGay - 更新可见生物
  功能: 更新可见生物列表中的生物状态
  参数:
    cret - 要更新的生物对象
  实现原理:
    1. 遍历可见生物列表查找目标
    2. 如果找到则标记为更新(check=1)
    3. 如果未找到则新建并添加(check=2)
    4. 非玩家且未死亡时增加引用计数 }
procedure TCreature.UpdateVisibleGay (cret: TCreature);
var
	i: integer;           // 循环变量
   flag: Boolean;        // 是否找到标志
   va: PTVisibleActor;   // 可见生物指针
begin
   flag := FALSE;
   try
      // 遍历查找已存在的生物
      for i:=0 to VisibleActors.Count-1 do
         if cret = TCreature (PTVisibleActor(VisibleActors[i]).cret) then begin
            PTVisibleActor (VisibleActors[i]).check := 1;  // 标记为更新
            flag := TRUE;
            break;
         end;
   except
      MainOutMessage ('[TCreature] UpdateVisibleGay exception');
   end;
   try
      // 未找到则新建并添加
      if not flag then begin
         new (va);
         va.check := 2;     // 标记为新增
         va.cret := cret;
         VisibleActors.Add (va);    // '2' : 新增
         // 2003/04/21 玩家除外
         if (cret.RaceServer <> RC_USERHUMAN) and (not cret.Death) then
         // 2003/03/18 增加引用计数
         Inc(cret.RefObjCount);
      end;
   except
      MainOutMessage ('[TCreature] UpdateVisibleGay-2 exception');
   end;
end;

{ UpdateVisibleItems - 更新可见物品
  功能: 更新可见物品列表中的物品状态
  参数:
    xx, yy - 物品坐标
    pmi - 地图物品指针
  实现原理:
    1. 遍历可见物品列表查找目标
    2. 如果找到则标记为更新(check=1)
    3. 如果未找到则新建并添加(check=2) }
procedure TCreature.UpdateVisibleItems (xx, yy: word; pmi: PTMapItem);
var
   i: integer;                    // 循环变量
   pvitem: PTVisibleItemInfo;     // 可见物品信息指针
   flag: Boolean;                 // 是否找到标志
begin
   flag := FALSE;
   // 遍历查找已存在的物品
   for i:=0 to VisibleItems.Count-1 do begin
      pvitem := PTVisibleItemInfo (VisibleItems[i]);
      if (pvitem.Id = Longint(pmi)) then begin
         pvitem.check := 1; // 标记为更新
         flag := TRUE;
         break;
      end;
   end;
   // 未找到则新建并添加
   if not flag then begin
      New (pvitem);
      pvitem.check := 2;  // 标记为新增
      pvitem.x := xx;
      pvitem.y := yy;
      pvitem.Id := Longint(pmi);
      pvitem.Name := pmi.Name;
      pvitem.looks := pmi.Looks;
      VisibleItems.Add (pvitem);
   end;
end;

{ UpdateVisibleEvents - 更新可见事件
  功能: 更新可见事件列表中的事件状态
  参数:
    xx, yy - 事件坐标
    mevent - 事件对象
  实现原理:
    1. 遍历可见事件列表查找目标
    2. 如果找到则标记为更新(check=1)
    3. 如果未找到则标记为新增(check=2)并添加 }
procedure TCreature.UpdateVisibleEvents (xx, yy: integer; mevent: TObject);
var
   i: integer;        // 循环变量
   event: TEvent;     // 事件对象
   flag: Boolean;     // 是否找到标志
begin
   flag := FALSE;
   // 遍历查找已存在的事件
   for i:=0 to VisibleEvents.Count-1 do begin
      event := TEvent(VisibleEvents[i]);
      if event = mevent then begin
         event.check := 1;  // 标记为更新
         flag := TRUE;
         break;
      end;
   end;
   // 未找到则标记为新增并添加
   if not flag then begin
      TEvent(mevent).check := 2; // 新增
      TEvent(mevent).X := xx;
      TEvent(mevent).Y := yy;
      VisibleEvents.Add (mevent);
   end;
end;

{ SearchViewRange - 搜索视野范围
  功能: 搜索并更新视野范围内的所有可见对象(生物、物品、事件)
  实现原理:
    1. 先将所有可见列表标记为0(待检查)
    2. 遍历视野范围内的所有坐标
    3. 检查并清理超过10分钟的残影对象
    4. 更新可见生物、物品、事件列表
    5. 根据检查标记处理新增/更新/离开的对象 }
procedure TCreature.SearchViewRange;
var
	stx, enx, sty, eny, i, j, k, down: integer;  // 坐标范围和循环变量
   pm: PTMapInfo;                // 地图信息指针
   pvi: PTVisibleItemInfo;       // 可见物品信息指针
   pva: PTVisibleActor;          // 可见生物指针
   pmi: PTMapEventInfo;          // 地图事件信息指针
   pmapitem: PTMapItem;          // 地图物品指针
   pd: PTDoorInfo;               // 门信息指针
   event: TEvent;                // 事件对象
   cret: TCreature;              // 生物对象
   inrange: Boolean;             // 是否在范围内
   uname: string;                // 用户名
label
   err_exception;                // 异常跳转标签
begin
   down := 0;  // 调试标记
	if PEnvir = nil then begin
   	MainOutMessage ('nil PEnvir');
   	exit;
	end;

   try
      // 将所有可见列表标记为0(待检查)
      for i:=0 to VisibleItems.Count-1 do PTVisibleItemInfo (VisibleItems[i]).Check := 0;    // '0' -> 标记
      for i:=0 to VisibleEvents.Count-1 do TEvent(VisibleEvents[i]).Check := 0;    // '0' -> 标记
      for i:=0 to VisibleActors.Count-1 do PTVisibleActor(VisibleActors[i]).Check := 0;
   except
      MainOutMessage ('ObjBase SearchViewRange 0');
      KickException;
   end;

   // 计算视野范围
   stx := CX-ViewRange;
   enx := CX+ViewRange;
   sty := CY-ViewRange;
   eny := CY+ViewRange;
   // 2003/02/11 视野搜索优化
   // 1. 搜索范围超出地图边界的部分不搜索...地图边界时有效
   if(stx < 0) then stx := 0;
   if(enx > PEnvir.MapWidth-1)  then enx := PEnvir.MapWidth-1;
   if(sty < 0) then sty := 0;
   if(eny > PEnvir.MapHeight-1) then eny := PEnvir.MapHeight-1;

   // 2003.02.11 视野搜索优化
   try
      // 遍历视野范围内的所有坐标
      for i:=stx to enx do begin
         for j:=sty to eny do begin
            inrange := PEnvir.GetMapXY (i, j, pm);
            if inrange then begin
               if pm.ObjList <> nil then begin
                  down := 1;
                  k := 0;
                  while TRUE do begin
                     if k >= pm.ObjList.Count then break;
                     if pm.ObjList[k] <> nil then begin

                        // 检查生物对象
                        if PTAThing (pm.ObjList[k]).Shape = OS_MOVINGOBJECT then begin

                           // 检查并清除残影
                           // 2003/01/22 时间从5分钟改为10分钟...防止NPC闪烁
                           if GetTickCount - PTAThing (pm.ObjList[k]).ATime >= 10 * 60 * 1000 then begin
                              Dispose (PTAThing(pm.ObjList[k]));
                              pm.ObjList.Delete(k);
                              down := 2;
                              if pm.ObjList.Count <= 0 then begin
                                 down := 3;
                                 pm.ObjList.Free;
                                 pm.ObjList := nil;
                                 break;
                              end;
                              continue;

                           end;

                           cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                           down := 4;
                           // 检查生物是否有效(非幽灵、非隐身、非管理员模式)
                           if (cret <> nil) and
                              (not cret.BoGhost) and
                              (not cret.HideMode) and
                              (not cret.BoSuperviserMode)
                           then begin
                              down := 5;
                              // 怪物排除条件
                              if (RaceServer < RC_ANIMAL) or  // 不是怪物
                                 (Master <> nil) or  // 有主人
                                 (BoCrazyMode) or  // 暴走状态
                                 (WantRefMsg) or  // 需要消息
                                 ((cret.Master <> nil) and (abs(cret.CX-CX) <= 3) and (abs(cret.CY-CY) <= 3)) or  // 有主人的怪物都看得见(视为玩家)
                                 (cret.RaceServer = RC_USERHUMAN)  // 玩家都看得见
                              then
                                 UpdateVisibleGay (cret);
                           end;
                        end;

                        if RaceServer = RC_USERHUMAN then begin

                           // 检查物品对象
                           down := 6;
                           if PTAThing (pm.ObjList[k]).Shape = OS_ITEMOBJECT then begin
                              down := 7;
                              // 丢弃超过1小时的物品删除
                              if GetTickCount - PTAThing (pm.ObjList[k]).ATime > 60 * 60 * 1000 then begin
                                 Dispose (PTAThing(pm.ObjList[k]));
                                 pm.ObjList.Delete(k);
                                 down := 8;
                                 if pm.ObjList.Count <= 0 then begin
                                    down := 9;
                                    pm.ObjList.Free;
                                    pm.ObjList := nil;
                                    break;
                                 end;
                                 continue;
                              end else begin
                                 down := 10;
                                 pmapitem := PTMapItem (PTAThing (pm.ObjList[k]).AObject);
                                 UpdateVisibleItems (i, j, pmapitem);
                                 // 检查物品归属权
                                 if (pmapitem.Ownership <> nil) or (pmapitem.Droper <> nil) then begin
                                    // 超过保护时间则清除归属
                                    if GetTickCount - pmapitem.Droptime > ANTI_MUKJA_DELAY then begin
                                       pmapitem.Ownership := nil;
                                       pmapitem.Droper := nil;
                                    end else begin
                                       // {注意} 拾取保护时间超过5分钟(死亡角色释放延迟时间)会出现BUG
                                       if pmapitem.Ownership <> nil then
                                          if TCreature (pmapitem.Ownership).BoGhost then
                                             pmapitem.Ownership := nil;
                                       if pmapitem.Droper <> nil then
                                          if TCreature (pmapitem.Droper).BoGhost then
                                             pmapitem.Droper := nil;
                                    end;
                                 end;
                              end;
                           end;

                           // 检查事件对象
                           down := 11;
                           if PTAThing (pm.ObjList[k]).Shape = OS_EVENTOBJECT then begin
                              event := TEvent (PTAThing (pm.ObjList[k]).AObject);
                              if event.Visible then
                                 UpdateVisibleEvents (i, j, TObject (event));
                           end;
                        end;
                     end;

                     Inc (k);

                  end;
               end;
            end;
         end;
      end;
   except
      MainOutMessage (UserName + ' ' + MapName + ',' + IntToStr(CX) + ',' + IntToStr(CY) + ' SearchViewRange 1-' + IntToStr(down));
      KickException;
   end;

   // 处理可见生物列表
   try
      i := 0;
      while TRUE do begin
         if i >= VisibleActors.Count then break;
         pva := PTVisibleActor(VisibleActors[i]);
         // check=0表示生物已离开视野
         if pva.check = 0 then begin
            if RaceServer = RC_USERHUMAN then begin
               cret := TCreature(pva.cret);
               // HideMode的生物会发送RM_DIGDOWN消息
               if not cret.HideMode then
                  SendMsg (cret, RM_DISAPPEAR, 0, 0, 0, 0, '');
               // 2003/03/18
               // 2003/03/10...临时处理
               //cret.DecRefObjCount;
            end;
            VisibleActors.Delete (i);
            Dispose (pva);
            continue;
         end else begin
            if RaceServer = RC_USERHUMAN then begin
               // check=2表示新进入视野的生物
               if pva.check = 2 then begin
                  cret := TCreature (pva.cret);
                  if cret <> self then begin
                     // 死亡生物发送死亡/骸骨消息
                     if cret.Death  then begin
                        if cret.BoSkeleton then
                           SendMsg (cret, RM_SKELETON, cret.Dir, cret.CX, cret.CY, 0, '')
                        else SendMsg (cret, RM_DEATH, cret.Dir, cret.CX, cret.CY, 0, '');
                     end else begin
                        // 活着的生物发送转向消息(首次看到的角色)
                        uname := cret.GetUserName;
                        SendMsg (cret, RM_TURN, cret.Dir, cret.CX, cret.CY, 0, uname);
                     end;
                  end;
               end;
            end;
         end;
         Inc (i);
      end;
   except
      MainOutMessage (MapName + ',' + IntToStr(CX) + ',' + IntToStr(CY) + ' SearchViewRange 2');
      KickException;
   end;

   // 处理可见物品和事件列表(只对玩家)
   try
      if RaceServer = RC_USERHUMAN then begin
         // 处理可见物品
         i := 0;
         while TRUE do begin
            if i >= VisibleItems.Count then break;
            // check=0表示物品已消失
            if PTVisibleItemInfo(VisibleItems[i]).check = 0 then begin
               pvi := PTVisibleItemInfo (VisibleItems[i]);
               SendMsg (self, RM_ITEMHIDE, 0, pvi.Id, pvi.x, pvi.y, '');
               VisibleItems.Delete (i);
               Dispose (pvi);
            end else begin
               // check=2表示新出现的物品
               if PTVisibleItemInfo(VisibleItems[i]).check = 2 then begin
                  pvi := PTVisibleItemInfo (VisibleItems[i]);
                  SendMsg (self, RM_ITEMSHOW, pvi.looks, pvi.Id, pvi.x, pvi.y, pvi.Name);
               end;
               Inc (i);
            end;
         end;

         // 处理可见事件
         i := 0;
         while TRUE do begin
            if i >= VisibleEvents.Count then break;
            event := TEvent(VisibleEvents[i]);
            // check=0表示事件已消失
            if event.Check = 0 then begin
               SendMsg (self, RM_HIDEEVENT, 0, integer(event), event.X, event.Y, '');
               VisibleEvents.Delete (i);
               continue;
            end else begin
               // check=2表示新出现的事件
               if event.Check = 2 then begin
                  SendMsg (self, RM_SHOWEVENT, event.EventType, integer(event), MakeLong(event.x, event.EventParam), event.y, '');
               end;
            end;
            Inc (i);
         end;  
      end;
   except
      MainOutMessage (MapName + ',' + IntToStr(CX) + ',' + IntToStr(CY) + ' SearchViewRange 3');
      KickException;
   end;

end;

{ Feature - 获取外观特征
  功能: 获取生物的外观特征值
  返回值: 外观特征整数 }
function  TCreature.Feature: integer;
begin
   Result := GetRelFeature (nil);
end;

{ GetRelFeature - 获取相对外观特征
  功能: 根据观察者获取生物的外观特征
  参数:
    who - 观察者(可为nil)
  返回值: 外观特征整数
  实现原理:
    1. 玩家: 根据衣服、武器、发型和性别计算
    2. 怪物: 根据种族图像、死亡状态和外观计算 }
function  TCreature.GetRelFeature (who: TCreature): integer;
var
   dress, weapon, face, r, a: integer;  // 衣服、武器、脸型、种族、外观
   ps: PTStdItem;                        // 标准物品指针
   booldversion: Boolean;                // 是否旧版本
begin
   // 玩家外观计算
   if RaceServer = RC_USERHUMAN then begin
      dress := 0;
      // 获取衣服外观
      if UseItems[U_DRESS].Index > 0 then begin
         ps := UserEngine.GetStdItem (UseItems[U_DRESS].Index);
         if ps <> nil then begin
            dress := ps.Shape * 2;  // 男女衣服分开
         end;
      end;
      dress := dress + Sex;
      weapon := 0;
      // 获取武器外观
      if UseItems[U_WEAPON].Index > 0 then begin
         ps := UserEngine.GetStdItem (UseItems[U_WEAPON].Index);
         if ps <> nil then begin
            weapon := ps.Shape * 2;
         end;
      end;
      weapon := weapon + Sex;
      face := Hair * 2 + Sex;  // 计算脸型
      Result := MakeFeature (0, Dress, Weapon, Face);
   end else begin
      // 怪物外观计算
      booldversion := FALSE;
      // 2003/02/11 删除无用逻辑...
{
      if who <> nil then begin
         if who.BoOldVersionUser_Italy then
            booldversion := TRUE;
      end;
      if booldversion then begin
         //이탈리아서버 이전 버젼 사용자 접속이 가능하도록
         r := RaceImage;
         a := Appearance;
         case a of
            160: //닭
               begin
                  r := 10;
                  a := 0;
               end;
            161: //사슴
               begin
                  r := 10;
                  a := 1;
               end;
            163: //침거미
               begin
                  r := 11;
                  a := 3;
               end;
            0: //경비병
               begin
                  r := 12;
                  a := 5;
               end;
            162: //욥
               begin
                  r := 11;
                  a := 6;
               end;
            1: //뭉코
               begin
                  r := 11;
                  a := 9;
               end;
         end;
         Result := MakeFeatureAp (r, DeathState, a);
      end else
}
         Result := MakeFeatureAp (RaceImage, DeathState, Appearance);
   end;
end;

{ GetCharStatus - 获取角色状态
  功能: 获取角色的状态位标志
  返回值: 状态位标志整数
  实现原理:
    1. 遍历0-11的状态数组
    2. 如果状态有效则设置对应位
    3. 合并扩展状态位 }
function   TCreature.GetCharStatus: integer;
var
   i, s: integer;  // 循环变量和状态值
begin
   s := 0;
   // 遍历状态数组
   for i:=0 to 11 do begin
      if StatusArr[i] > 0 then
         s := longword(s) or ($80000000 shr i);  // 设置对应位
   end;
   // 合并扩展状态
   Result := s or (CharStatusEx and $000FFFFF);
end;

{ InitValues - 初始化属性值
  功能: 初始化生物的属性值
  实现原理: 将基础能力复制到工作能力 }
procedure  TCreature.InitValues;
begin
   // 能力值初始化
   WAbil := Abil;
end;

{ Initialize - 初始化
  功能: 初始化生物并让其出现在地图上
  实现原理:
    1. 初始化属性值
    2. 检查并修正魔法等级
    3. 在地图上出现
    4. 更新角色状态 }
procedure  TCreature.Initialize;
var
   i, n: integer;  // 循环变量和魔法等级
begin
   InitValues;
   // 魔法等级检查
   for i:=0 to MagicList.Count-1 do begin
      n := PTUserMagic (MagicList[i]).Level;
      if not (n in [0..3]) then
         PTUserMagic (MagicList[i]).Level := 0;
   end;
   // 在地图上出现
   ErrorOnInit := TRUE;
   if PEnvir.CanWalk (CX, CY, TRUE{允许重叠}) then begin
      if Appear then begin
         ErrorOnInit := FALSE;
      end;
   end;
   CharStatus := GetCharStatus;
   AddBodyLuck (0);
end;

{ Finalize - 结束化
  功能: 生物结束时的清理工作 }
procedure  TCreature.Finalize;
begin

end;

{ FeatureChanged - 外观已改变
  功能: 广播外观改变消息 }
procedure  TCreature.FeatureChanged;
begin
   SendRefMsg (RM_FEATURECHANGED, 0, Feature, 0, 0, '');
end;

{ CharStatusChanged - 角色状态已改变
  功能: 广播角色状态改变消息 }
procedure  TCreature.CharStatusChanged;
begin
   SendRefMsg (RM_CHARSTATUSCHANGED, HitSpeed{wparam}, CharStatus, 0, 0, '');
end;

{ Appear - 出现
  功能: 让生物出现在地图上
  返回值: 是否成功出现
  实现原理:
    1. 将生物添加到地图
    2. 广播转向消息通知周围玩家 }
function   TCreature.Appear: Boolean;
var
   outofrange: pointer;  // 超出范围标志
begin
   outofrange := PEnvir.AddToMap (CX, CY, OS_MOVINGOBJECT, self);
   if outofrange = nil then begin
      Result := FALSE;
   end else
      Result := TRUE;
   if not HideMode then
      SendRefMsg (RM_TURN, Dir, CX, CY, 0, '');
   //레벨에 맞게 입장할 수 있는 맵인지 체크 해야 함
end;

{ Disappear - 消失
  功能: 让生物从地图上消失
  返回值: 始终返回TRUE
  实现原理:
    1. 从地图上删除
    2. 广播消失消息 }
function   TCreature.Disappear: Boolean;
begin
   PEnvir.DeleteFromMap (CX, CY, OS_MOVINGOBJECT, self);
   SendRefMsg (RM_DISAPPEAR, 0, 0, 0, 0, '');
   Result := TRUE;
end;

{ KickException - 异常踢出
  功能: 发生异常时处理生物
  实现原理:
    1. 玩家: 返回出生点并强制关闭
    2. 怪物: 设置死亡并变成幽灵 }
procedure  TCreature.KickException;
var
   hum: TUserHuman;  // 玩家对象
begin
   if RaceServer = RC_USERHUMAN then begin
      // 玩家返回出生点
      MapName := HomeMap;
      CX := HomeX;
      CY := HomeY;
      hum := TUserHuman(self);
      hum.EmergencyClose := TRUE;
   end else begin
      // 怪物死亡
      Death := TRUE;
      DeathTime := GetTickCount;
      MakeGhost;
   end;
end;

{ Walk - 行走
  功能: 处理生物行走后的逻辑(传送门、事件等)
  参数:
    msg - 行走消息类型(0=走, 1=跑)
  返回值: 是否成功
  实现原理:
    1. 检查当前位置的传送门和事件
    2. 如果有事件则触发事件伤害
    3. 如果有传送门则处理地图切换
    4. 广播行走消息 }
function  TCreature.Walk (msg: integer): Boolean;
var
   i: integer;              // 循环变量
   pm: PTMapInfo;           // 地图信息指针
   pat: PTAThing;           // 地图对象指针
   pgate: PTGateInfo;       // 传送门信息指针
   inrange: Boolean;        // 是否在范围内
   newenv: TEnvirnoment;    // 新环境
   newmap: string;          // 新地图名
   hum: TUserHuman;         // 玩家对象
   event: TEvent;           // 事件对象
label
   needholefinish;          // 需要洞完成标签
begin
   Result := TRUE;
   try
      inrange := PEnvir.GetMapXY (CX, CY, pm);
      pgate := nil;
      event := nil;
      // 检查当前位置的对象
      if inrange then begin
         if pm.ObjList <> nil then begin
            for i:=0 to pm.ObjList.Count-1 do begin
               pat := pm.ObjList[i];
               // 检查传送门
               if pat.Shape = OS_GATEOBJECT then begin
                  pgate := PTGateInfo (pat.AObject);
               end;
               // 检查事件
               if pat.Shape = OS_EVENTOBJECT then begin
                  if TEvent(pat.AObject).OwnCret <> nil then
                     event := TEvent (pat.AObject);
                  continue;
               end;
               // 检查地图事件
               if pat.Shape = OS_MAPEVENT then begin
                  {???}
               end;
               // 检查门
               if pat.Shape = OS_DOOR then begin
               end;
               // 检查房间
               if pat.Shape = OS_ROON then begin
               end;

            end;
         end;
      end;

      // 触发事件伤害
      if event <> nil then begin
         if event.OwnCret.IsProperTarget (self) then begin
            SendMsg (event.OwnCret, RM_MAGSTRUCK_MINE, 0, event.Damage, 0, 0, '');
         end;
      end;

      // 处理传送门
      if Result and (pgate <> nil) then begin
         // NPC不能出门
         if RaceServer = RC_USERHUMAN then begin
            if PEnvir.ArroundDoorOpened (CX, CY) then begin
               // 尸王殿需要僵尸挖出的洞才能进入
               if TEnvirnoment (pgate.EnterEnvir).NeedHole then begin
                  if EventMan.FindEvent (PEnvir, CX, CY, ET_DIGOUTZOMBI) = nil then
                     goto needholefinish;
               end;
               // 同服务器切换地图
               if ServerIndex = TEnvirnoment(pgate.EnterEnvir).Server then begin
                  if not EnterAnotherMap (TEnvirnoment(pgate.EnterEnvir), pgate.EnterX, pgate.EnterY) then
                     Result := FALSE;
               end else begin
                  // 跨服务器切换
                  Disappear;
                  SpaceMoved := TRUE;
                  hum := TUserHuman (self);
                  hum.ChangeMapName := TEnvirnoment(pgate.EnterEnvir).MapName;
                  hum.ChangeCX := pgate.EnterX;
                  hum.ChangeCY := pgate.EnterY;
                  hum.BoChangeServer := TRUE;
                  hum.ChangeToServerNumber := TEnvirnoment(pgate.EnterEnvir).Server;
                  hum.EmergencyClose := TRUE;
                  hum.SoftClosed := TRUE;  // 不终止认证
               end;
               needholefinish:
            end; // 门已锁定 Result=true 正常
         end else
            Result := FALSE; // 防止NPC堵门
      end else begin
         // 广播行走消息
         if Result then
            SendRefMsg (msg, Dir, CX, CY, 0, '');
      end;
   except
      MainOutMessage ('[TCreature] Walk exception ' + MapName + ' ' + IntToStr(CX) + ':' + IntToStr(CY));
   end;
end;

{ EnterAnotherMap - 进入另一个地图
  功能: 处理生物进入另一个地图
  参数:
    enterenvir - 目标地图环境
    enterx, entery - 目标坐标
  返回值: 是否成功进入
  实现原理:
    1. 检查等级要求
    2. 检查任务要求
    3. 检查城堡进入权限
    4. 从旧地图消失并在新地图出现 }
function  TCreature.EnterAnotherMap (enterenvir: TEnvirnoment; enterx, entery: integer): Boolean;
var
   i, oldx, oldy: integer;    // 循环变量和旧坐标
   pm: PTMapInfo;             // 地图信息指针
   oldpenvir: TEnvirnoment;   // 旧地图环境
begin
   Result := FALSE;
   try
      // 1) 检查是否可以进入
      if Abil.Level < enterenvir.NeedLevel then exit;  // 等级不足

      // 触发地图任务
      if enterenvir.MapQuest <> nil then begin
         TMerchant (enterenvir.MapQuest).UserCall (self);
      end;

      // 检查任务标记
      if enterenvir.NeedSetNumber >= 0 then begin
         if GetQuestMark (enterenvir.NeedSetNumber) <> enterenvir.NeedSetValue then
            exit;
      end;

      // 检查目标坐标是否有效
      if not enterenvir.GetMapXY (enterx, entery, pm) then exit;

      // 检查城堡内城进入权限
      if enterenvir = UserCastle.CorePEnvir then begin
         if RaceServer = RC_USERHUMAN then
            if not UserCastle.CanEnteranceCoreCastle (CX, CY, TUserHuman(self)) then
               exit;  // 无法进入
      end;

      oldpenvir := PEnvir;
      oldx := CX;
      oldy := CY;

      // 2) 从当前地图离开，初始化变量
      Disappear;

      // 清理消息目标列表
      try
         MsgTargetList.Clear;
      except
         MainOutMessage ('[Exception] MsgTargetList.Clear');
      end;
      // 清理可见物品列表
      try
      for i:=0 to VisibleItems.Count-1 do
         Dispose (PTVisibleItemInfo (VisibleItems[i]));
      except
         MainOutMessage ('[Exception] VisbleItems Dispose(..)');
      end;
      try
      VisibleItems.Clear;
      except
         MainOutMessage ('[Exception] VisbleItems.Clear');
      end;
      try
      VisibleEvents.Clear;
      except
         MainOutMessage ('[Exception] VisbleEvents.Clear');
      end;
      // 2003/03/18
      try
         i := 0;
         while TRUE do begin
            if i >= VisibleActors.Count then break;
            {
            try
               if(PTVisibleActor(VisibleActors[i]).cret <> nil) then
                  TCreature (PTVisibleActor(VisibleActors[i]).cret).DecRefObjCount;
            except
               MainOutMessage ('[Exception] TCreatre.Destroy : Visible Actor Dec RefObjCount');
            end;
            }
            Dispose (PTVisibleActor(VisibleActors[i]));
            VisibleActors.Delete(i);
//          Inc(i);
         end;
      except
         MainOutMessage ('[Exception] VisbleActors Dispose(..)');
      end;
      try
         VisibleActors.Clear;
      except
         MainOutMessage ('[Exception] VisbleActors.Clear');
      end;

      SendMsg (self, RM_CLEAROBJECTS, 0, 0, 0, 0, '');

      // 3) 在新地图出现
      PEnvir := enterenvir;
      MapName := enterenvir.MapName;
      CX := enterx;
      CY := entery;
      SendMsg (self, RM_CHANGEMAP, 0, 0, 0, 0, enterenvir.MapName);

      if Appear then begin
         MapMoveTime := GetTickCount;
         SpaceMoved := TRUE; // 防止WalkTo失败
         Result := TRUE;
      end else begin
         // 失败时返回旧地图
         MapName := oldpenvir.MapName;
         PEnvir := oldpenvir;
         CX := oldx;
         CY := oldy;
         PEnvir.AddToMap (CX, CY, OS_MOVINGOBJECT, self);
      end;

      // 进出门派比武场时
      if PEnvir.Fight3Zone and (PEnvir.Fight3Zone <> oldpenvir.Fight3Zone) then
         UserNameChanged;  // 名字颜色改变

   except
      MainOutMessage ('[TCreature] EnterAnotherMap exception');
   end;
end;

{ Turn - 转向
  功能: 让生物转向指定方向
  参数:
    dir - 方向 }
procedure TCreature.Turn (dir: byte);
begin
   self.Dir := dir;
   SendRefMsg (RM_TURN, Dir, CX, CY, 0, '');
end;

{ Say - 说话
  功能: 广播说话消息
  参数:
    saystr - 说话内容 }
procedure TCreature.Say (saystr: string);
begin
	SendRefMsg (RM_HEAR, 0, clBlack, clWhite, 0, UserName + ': ' + saystr);
end;

{ SysMsg - 系统消息
  功能: 发送系统消息给自己
  参数:
    str - 消息内容
    mode - 消息模式(0=普通, 1=系统2, 2=蓝色) }
procedure  TCreature.SysMsg (str: string; mode: integer);
begin
   case mode of
      1:   SendMsg (self, RM_SYSMESSAGE2, 0, 0, 0, 0, str);
      2:   SendMsg (self, RM_SYSMSG_BLUE, 0, 0, 0, 0, str);
      else SendMsg (self, RM_SYSMESSAGE, 0, 0, 0, 0, str);
   end;
end;

{ GroupMsg - 队伍消息
  功能: 发送消息给队伍所有成员
  参数:
    str - 消息内容 }
procedure  TCreature.GroupMsg (str: string);
var
   i: integer;  // 循环变量
begin
   if GroupOwner <> nil then begin
      for i:=0 to GroupOwner.GroupMembers.Count-1 do begin
         TCreature(GroupOwner.GroupMembers.Objects[i]).SendMsg (self, RM_GROUPMESSAGE, 0, 0, 0, 0, '-' + str);
      end;
   end;
end;

{ NilMsg - 空消息
  功能: 发送无发送者的消息
  参数:
    str - 消息内容 }
procedure  TCreature.NilMsg (str: string);
begin
   SendMsg (nil, RM_HEAR, 0, 0, 0, 0, str);
end;

{ MakeGhost - 变成幽灵
  功能: 让生物完全死亡，准备消失
  实现原理:
    1. 设置幽灵标志
    2. 记录幽灵时间
    3. 从地图消失 }
procedure TCreature.MakeGhost;
begin
   BoGhost := TRUE;
   GhostTime := GetTickCount;
   Disappear;
end;

{ ApplyMeatQuality - 应用肉品质
  功能: 对动物(如鹿)应用肉品质
  实现原理: 遍历物品列表，对肉类物品设置耐久为肉品质 }
procedure TCreature.ApplyMeatQuality;
var
   i: integer;       // 循环变量
   pstd: PTStdItem;  // 标准物品指针
begin
   for i:=0 to ItemList.Count-1 do begin
      pstd := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
      if pstd <> nil then begin
         if pstd.Stdmode = 40 then begin // 肉块类型
            PTUserItem(ItemList[i]).Dura := MeatQuality;
         end;
      end;
   end;
end;

{ TakeCretBagItems - 拿取生物背包物品
  功能: 拿取目标生物背包中的所有物品(仅对怪物使用)
  参数:
    target - 目标生物
  返回值: 是否成功 }
function  TCreature.TakeCretBagItems (target: TCreature): Boolean;
var
   i: integer;
   hum: TUserHuman;
begin
   Result := FALSE;
   while TRUE do begin
      if target.ItemList.Count <= 0 then break;
      if AddItem (PTUserItem (target.ItemList[0])) then begin
         if RaceServer = RC_USERHUMAN then begin
            if self is TUserHuman then begin
               hum := TUserHuman(self);
               TUserHuman(hum).SendAddItem (PTUserItem (target.ItemList[0])^);
               Result := TRUE;
            end;
         end;
         target.ItemList.Delete (0);
      end else
         break;
   end;
end;

{ ScatterBagItems - 散落背包物品
  功能: 死亡时掉落背包中的物品
  参数:
    itemownership - 物品归属者(可拾取者)
  实现原理:
    1. 玩家: PK等级<2时有1/3概率掉落，红名全部掉落
    2. 怪物: 全部掉落
    3. 台湾活动用户只掉落活动物品 }
procedure TCreature.ScatterBagItems (itemownership: TObject);
var
   i, dropwide: integer;     // 循环变量和掉落范围
   pu: PTUserItem;           // 用户物品指针
   pstd: PTStdItem;          // 标准物品指针
   dellist: TStringList;     // 删除列表
   boDropall: Boolean;       // 是否全部掉落
begin
   dellist := nil;

   boDropall := TRUE;
   if RaceServer = RC_USERHUMAN then begin
      dropwide := 2;
      // 玩家PK等级<2时有1/3概率掉落
      if PKLevel < 2 then boDropall := FALSE;
      // 红名全部掉落
   end else
      dropwide := 3;

   try
      for i:=ItemList.Count-1 downto 0 do begin
         pstd := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
         // 台湾要求: stdmode=51的物品死亡时必定掉落，断线时也掉落
         if BoTaiwanEventUser then begin
            // 台湾活动: 活动用户死亡时只掉落活动物品
            if (pstd.StdMode = TAIWANEVENTITEM) then begin
               if DropItemDown (PTUserItem(ItemList[i])^, dropwide, TRUE, itemownership, self) then begin
                  pu := PTUserItem(ItemList[i]);
                  if RaceServer = RC_USERHUMAN then begin
                     if dellist = nil then dellist := TStringList.Create;
                     // 通知客户端掉落的物品
                     dellist.AddObject(UserEngine.GetStdItemName (pu.Index), TObject(pu.MakeIndex));
                  end;
                  Dispose(PTUserItem(ItemList[i]));
                  ItemList.Delete (i);
               end;
            end;
         end else begin
            // 普通掉落逻辑
            if (Random(3) = 0) or boDropall then begin
               if DropItemDown (PTUserItem(ItemList[i])^, dropwide, TRUE, itemownership, self) then begin
                  pu := PTUserItem(ItemList[i]);
                  if RaceServer = RC_USERHUMAN then begin
                     if dellist = nil then dellist := TStringList.Create;
                     // 通知客户端掉落的物品
                     dellist.AddObject(UserEngine.GetStdItemName (pu.Index), TObject(pu.MakeIndex));
                  end;
                  Dispose(PTUserItem(ItemList[i]));
                  ItemList.Delete (i);
               end;
            end;
         end;
      end;
      // 发送删除物品消息
      if dellist <> nil then begin
         SendMsg (self, RM_DELITEMS, 0, integer(dellist), 0, 0, '');
         // dellist在RM_DELITEMS处理中释放
      end;
   except
      MainOutMessage ('[Exception] TCreature.ScatterBagItems');
   end;
end;

{ DropEventItems - 掉落活动物品
  功能: 掉落活动物品(不改变角色颜色)
  实现原理:
    1. 遍历背包查找stdmode=51的活动物品
    2. 掉落的物品无归属者
    3. 通知客户端删除物品 }
procedure  TCreature.DropEventItems;
var
   i, dropwide: integer;     // 循环变量和掉落范围
   pu: PTUserItem;           // 用户物品指针
   pstd: PTStdItem;          // 标准物品指针
   dellist: TStringList;     // 删除列表
begin
   dellist := nil;
   dropwide := 3;
   try
      for i:=ItemList.Count-1 downto 0 do begin
         pstd := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
         // 台湾要求: stdmode=51的物品死亡时必定掉落
         if pstd <> nil then begin
            if (pstd.StdMode = TAIWANEVENTITEM) then begin
               // 此时掉落的物品无归属者
               if DropItemDown (PTUserItem(ItemList[i])^, dropwide, TRUE, nil, self) then begin
                  pu := PTUserItem(ItemList[i]);
                  if RaceServer = RC_USERHUMAN then begin
                     if dellist = nil then dellist := TStringList.Create;
                     // 通知客户端掉落的物品
                     dellist.AddObject(UserEngine.GetStdItemName (pu.Index), TObject(pu.MakeIndex));
                  end;
                  Dispose(PTUserItem(ItemList[i]));
                  ItemList.Delete (i);
               end;
            end;
         end;
      end;
      // 发送删除物品消息
      if dellist <> nil then begin
         SendMsg (self, RM_DELITEMS, 0, integer(dellist), 0, 0, '');
         // dellist在RM_DELITEMS处理中释放
      end;
   except
      MainOutMessage ('[Exception] TCreature.DropEventItems');
   end;
end;

{ ScatterGolds - 散落金币
  功能: 死亡时掉落金币
  参数:
    itemownership - 物品归属者(可拾取者)
  实现原理:
    1. 每次最多掉落2000金币
    2. 最多分成16堆掉落
    3. 掉落失败则停止 }
procedure TCreature.ScatterGolds (itemownership: TObject);
const
   dropmax = 2000;  // 每堆最大金币数
var
   i, ngold: integer;  // 循环变量和金币数
begin
   if Gold > 0 then begin
      // 最多分成16堆掉落
      for i:=0 to 16 do begin
         if Gold > dropmax then begin
            ngold := dropmax;
            Gold := Gold - dropmax;
         end else begin
            ngold := Gold;
            Gold := 0;
         end;
         if ngold > 0 then begin
            // 掉落失败则返还金币
            if not DropGoldDown (ngold, TRUE, itemownership, self) then begin
               Gold := Gold + ngold;
               break;
            end;
         end else break;
      end;
      GoldChanged;
   end;
end;

{ DropUseItems - 掉落装备物品
  功能: 死亡时随机掉落装备中的物品
  参数:
    itemownership - 物品归属者(可拾取者)
  实现原理:
    1. 先处理死亡必破的物品(如超魂系列)
    2. PK等级>2时有1/15概率掉落，否则1/30概率
    3. 永不掉落的物品除外 }
procedure TCreature.DropUseItems (itemownership: TObject);
var
   i, ran: integer;       // 循环变量和随机数
   dellist: TStringList;  // 删除列表
   ps: PTStdItem;         // 标准物品指针
   iname: string;         // 物品名称
begin
   dellist := nil;
   try
      // 处理死亡必破的物品(如超魂系列)
      if (RaceServer = RC_USERHUMAN) and (not BoOldVersionUser_Italy) then begin
         // 2003/03/15 物品栏扩展 8->12
         for i:=0 to 12 do begin
            ps := UserEngine.GetStdItem (UseItems[i].Index);
            if ps <> nil then begin
               // 检查死亡必破标志
               if ps.ItemDesc and IDC_DIEANDBREAK <> 0 then begin
                  if dellist = nil then dellist := TStringList.Create;
                  dellist.AddObject(iname, TObject(UseItems[i].MakeIndex));
                  // 记录日志
                  AddUserLog ('16'#9 + // 祖咆
                              MapName + ''#9 +
                              IntToStr(CX) + ''#9 +
                              IntToStr(CY) + ''#9 +
                              UserName + ''#9 +
                              ps.Name + ''#9 +
                              IntToStr(UseItems[i].MakeIndex) + ''#9 +
                              IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                              '0');

                  UseItems[i].Index := 0;
               end;
            end;
         end;
      end;
      // 随机掉落装备
      if PKLevel > 2 then ran := 15  // 红名1/15概率
      else ran := 30;                 // 普通1/30概率
      // 2003/03/15 物品栏扩展 8->12
      for i:=0 to 12 do begin
         if Random(ran) = 0 then begin
            if DropItemDown (UseItems[i], 2, TRUE, itemownership, self) then begin
               ps := UserEngine.GetStdItem (UseItems[i].Index);
               if ps <> nil then begin
                  // 永不掉落的物品除外
                  if ps.ItemDesc and IDC_NEVERLOSE = 0 then begin
                     if RaceServer = RC_USERHUMAN then begin
                        if dellist = nil then dellist := TStringList.Create;
                        // 通知客户端掉落的物品
                        dellist.AddObject(UserEngine.GetStdItemName (UseItems[i].Index), TObject(UseItems[i].MakeIndex));
                     end;
                     UseItems[i].Index := 0;
                  end;
               end;
            end;
         end;
      end;
      // 发送删除物品消息
      if dellist <> nil then begin
         SendMsg (self, RM_DELITEMS, 0, integer(dellist), 0, 0, '');
         // dellist在RM_DELITEMS处理中释放
      end;
   except
      MainOutMessage ('[Exception] TCreature.DropUseItems');
   end;
end;

{ Die - 死亡
  功能: 处理生物死亡逻辑
  实现原理:
    1. 设置死亡状态和时间
    2. 处理经验值分配
    3. 处理PK惩罚
    4. 掉落物品和金币
  注意: 只有玩家可以复活 }
procedure TCreature.Die;
   // 布尔转字符辅助函数
   function BoolToChar (flag: Boolean): Char;
   begin
      if flag then Result := 'T'
      else Result := 'F';
   end;
var
   i, exp: integer;              // 循环变量和经验值
   guildwarkill, flag: Boolean;  // 行会战击杀标志
   str: string;                  // 字符串
   ehiter, cret: TCreature;      // 经验击中者和生物
   boBadKill, bogroupcall: Boolean;  // 恶意击杀和组队调用标志
   questnpc: TMerchant;          // 任务NPC
begin
   if NeverDie then exit;  // 不死标志
   Death := TRUE;
   DeathTime := GetTickCount;
   ClearPkHiterList;  // 清理PK击中者列表
   // 召唤兽死亡时清除击中者
   if Master <> nil then begin
      ExpHiter := nil;
      LastHiter := nil;
   end;
   // 清除增益效果
   IncSpell := 0;
   IncHealth := 0;
   IncHealing := 0;

   try
      // 怪物被杀死时处理经验值
      if (RaceServer <> RC_USERHUMAN) and (LastHiter <> nil) then begin
         // 经验值归属者处理
         if ExpHiter <> nil then begin
            if ExpHiter.RaceServer = RC_USERHUMAN then begin
               // 根据等级和战斗经验计算经验值
               exp := ExpHiter.CalcGetExp (self.Abil.Level, self.FightExp);
               if not BoVentureServer then begin
                  ExpHiter.GainExp (exp);
               end else begin
                  // 冒险服务器中增加分数
               end;

               // 检查地图任务
               if PEnvir.HasMapQuest then begin
                  if ExpHiter.GroupOwner <> nil then begin
                     // 组队时对所有队员应用
                     for i:=0 to ExpHiter.GroupOwner.GroupMembers.Count-1 do begin
                        cret := TCreature(ExpHiter.GroupOwner.GroupMembers.Objects[i]);
                        if not cret.Death and
                          (ExpHiter.PEnvir = cret.PEnvir) and
                          (abs(ExpHiter.CX-cret.CX) <= 12) and
                          (abs(ExpHiter.CY-cret.CY) <= 12)
                        then begin
                           if cret = ExpHiter then bogroupcall := FALSE
                           else bogroupcall := TRUE;
                           questnpc := TMerchant (PEnvir.GetMapQuest (cret, self.UserName{死亡怪物名}, '', bogroupcall));
                           if questnpc <> nil then
                              questnpc.UserCall (cret);
                        end;
                     end;
                  end else begin
                     // 未组队时只对本人应用
                     questnpc := TMerchant (PEnvir.GetMapQuest (ExpHiter, UserName, '', FALSE));
                     if questnpc <> nil then
                        questnpc.UserCall (ExpHiter);
                  end;
               end;

            end else begin
               // 召唤兽击杀时
               if ExpHiter.Master <> nil then begin
                  // 召唤兽也获得经验
                  ExpHiter.GainSlaveExp (self.Abil.Level);
                  // 主人获得经验
                  exp := ExpHiter.Master.CalcGetExp (self.Abil.Level, self.FightExp);
                  if not BoVentureServer then begin
                     ExpHiter.Master.GainExp (exp);
                  end else begin
                     // 冒险服务器中增加分数
                  end;
               end;
            end;
         end else
            // 最后击中者是玩家时
            if LastHiter.RaceServer = RC_USERHUMAN then begin
               // 根据等级和战斗经验计算经验值
               exp := LastHiter.CalcGetExp (self.Abil.Level, self.FightExp);
               if not BoVentureServer then begin
                  LastHiter.GainExp (exp);
               end else begin
                  // 冒险服务器中增加分数
               end;
            end;
      end;
      Master := nil;
   except
      MainOutMessage ('[Exception] TCreature.Die 1');
   end;

   try
      boBadKill := FALSE;
      if (not BoVentureServer) and (not PEnvir.FightZone) and (not PEnvir.Fight3Zone) then begin
         //PK금지 구역인 경우
         if (RaceServer = RC_USERHUMAN) and (LastHiter <> nil) and (PKLevel < 2) then begin
            //죽은자가 사람, 때린자 있음, 죽은자가 PK아님
            if LastHiter.RaceServer = RC_USERHUMAN then begin
               boBadKill := TRUE;
               if BoTaiwanEventUser then boBadKill := FALSE;  //대만 이벤트 인경우 죽여되 됨
            end;
            if LastHiter.Master <> nil then
               if LastHiter.Master.RaceServer = RC_USERHUMAN then begin
                  LastHiter := LastHiter.Master; //주인이 때린 것으로 간주
                  boBadKill := TRUE;
               end;
         end;
      end;

      if boBadKill and (LastHiter <> nil) then begin
         // 사람을 죽인 경우, 모험서버에서는 해당안됨
         // 사람을 선량한 사람을 죽임.

         // 문파전으로 죽음
         guildwarkill := FALSE;
         if (MyGuild <> nil) and (LastHiter.MyGuild <> nil) then begin
            // 둘다 문파에 가입된 상태에서
            if GetGuildRelation (self, LastHiter) = 2 then  // 문파전 중임
               guildwarkill := TRUE;  // 문파전으로 죽음, 빨갱이 안됨
         end;

         // 공성전으로 죽음
         if UserCastle.BoCastleUnderAttack then
            if (BoInFreePKArea) or (UserCastle.IsCastleWarArea (PEnvir, CX, CY)) then
               guildwarkill := TRUE;

         if not guildwarkill then begin // 문파전으로 죽음
            if not LastHiter.IsGoodKilling(self) then begin
               LastHiter.IncPkPoint (100); //

               LastHiter.SysMsg ('콱렇죄캇�鯉.', 0);
               SysMsg ('[콱굳' + LastHiter.UserName + '�벧죄.]', 0);

               // 살인한 사람 행운 감소
               LastHiter.AddBodyLuck (-500);
               if PkLevel < 1 then // 죽은 사람이 착한 사람
                  if Random(5) = 0 then // 살인을 하면 무기가 저주를 받는다.
                     LastHiter.MakeWeaponUnlock;
            end else
               LastHiter.SysMsg ('[퀭삔肝돕攣뎠렝括돨방橙괏빱]', 1);
         end;

      end;
   except
      MainOutMessage ('[Exception] TCreature.Die 2');
   end;

   // 处理物品掉落
   try
      // 非比武场且非动物(动物需要切割才出肉)
      if (not PEnvir.FightZone) and
         (not PEnvir.Fight3Zone) and
         (not BoAnimal) then begin

         // 获取经验值归属者(召唤兽算主人)
         ehiter := ExpHiter;
         if ExpHiter <> nil then
            if ExpHiter.Master <> nil then
               ehiter := ExpHiter.Master;

         if (RaceServer <> RC_USERHUMAN) then begin
            // 怪物掉落
            DropUseItems (ehiter);
            // 有主人的怪物不掉落物品
            if (Master = nil) and (not BoNoItem) then
               ScatterBagItems (ehiter);
            // 怪物掉落金币(玩家不掉金币)
            if (RaceServer >= RC_ANIMAL) and (Master = nil) and (not BoNoItem) then
               ScatterGolds (ehiter);
         end else begin
            // 玩家掉落
            // 被玩家杀死时不掉落装备
            if ehiter <> nil then begin
               if ehiter.RaceServer <> RC_USERHUMAN then
                  DropUseItems (nil);
            end else
               DropUseItems (nil);

            ScatterBagItems (nil);
            // 死亡时运气减少
            // 2003/02/11 最大等级变更
            AddBodyLuck ( - ((MAXLEVEL-1) - ((MAXLEVEL-1) - Abil.Level * 5)));
         end;
      end;

      // 门派比武中
      if PEnvir.Fight3Zone then begin
         // 可以死3次的比武
         Inc (FightZoneDieCount);
         if MyGuild <> nil then begin
            TGuild(MyGuild).TeamFightWhoDead (UserName);
         end;

         // 计算分数
         if (LastHiter <> nil) then begin
            if (LastHiter.MyGuild <> nil) and (MyGuild <> nil) then begin
               // 增加比赛分数，记录个人成绩
               TGuild(LastHiter.MyGuild).TeamFightWhoWinPoint (LastHiter.UserName, 100);
               str := TGuild(LastHiter.MyGuild).GuildName + ':' +
                      IntToStr(TGuild(LastHiter.MyGuild).MatchPoint) + '  ' +
                      TGuild(MyGuild).GuildName + ':' +
                      IntToStr(TGuild(MyGuild).MatchPoint);
               // 向当前地图全体广播
               UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000, '- ' + str);
            end;
         end;
      end;

      // 记录日志
      if RaceServer = RC_USERHUMAN then begin
         if LastHiter <> nil then begin
            if LastHiter.RaceServer = RC_USERHUMAN then str := LastHiter.UserName
            else str := '#' + LastHiter.UserName;
         end else str := '######';
         AddUserLog ('19'#9 + // 死亡日志
                     MapName + ''#9 +
                     IntToStr(CX) + ''#9 +
                     IntToStr(CY) + ''#9 +
                     UserName + ''#9 +
                     'FZ-' + BoolToChar(PEnvir.FightZone) + '_F3-' + BoolToChar(PEnvir.Fight3Zone) + ''#9 +
                     '0'#9 +
                     '1'#9 +
                     str); 
      end;

      // 广播死亡消息
      SendRefMsg (RM_DEATH, Dir, CX, CY, 1, '');
   except
      MainOutMessage ('[Exception] TCreature.Die 3');
   end;
end;

{ Alive - 复活
  功能: 让生物复活
  实现原理: 设置死亡标志为FALSE并广播复活消息 }
procedure TCreature.Alive;
begin
   Death := FALSE;
   SendRefMsg (RM_ALIVE, Dir, CX, CY, 0, '');
end;

{ SetLastHiter - 设置最后击中者
  功能: 设置最后击中自己的生物
  参数:
    hiter - 击中者
  实现原理:
    1. 记录最后击中者和时间
    2. 如果没有经验值归属者则设置为当前击中者 }
procedure TCreature.SetLastHiter (hiter: TCreature);
begin
   LastHiter := hiter;
   LastHitTime := GetTickCount;
   if ExpHiter = nil then begin
      ExpHiter := hiter;
      ExpHitTime := GetTickCount;
   end else
      if ExpHiter = hiter then ExpHitTime := GetTickCount;
end;

{ AddPkHiter - 添加PK击中者
  功能: 被玩家攻击时调用，记录先攻击者
  参数:
    hiter - 击中者(需先确认是玩家)
  实现原理:
    1. 只对善良玩家应用
    2. 非比武场时标记先攻击者
    3. 先攻击者名字颜色改变 }
procedure  TCreature.AddPkHiter (hiter: TCreature);
var
   i: integer;
   pk: PTPkHiterInfo;
begin
   if (PkLevel < 2) and (hiter.PkLevel < 2) then begin //선량한 사람들한테만 적용 (자신이 선량한 사람)
      if (not PEnvir.FightZone) and (not PEnvir.Fight3Zone) then begin
         if not BoIllegalAttack then begin  //자신이 선재공격을 하지 않은 경우
            hiter.IllegalAttackTime := GetTickCount;
            if not hiter.BoIllegalAttack then begin
               hiter.BoIllegalAttack := TRUE;
               hiter.ChangeNameColor;
            end;
         end;
      end;
      {end else begin
         for i:=0 to hiter.PKHiterList.Count-1 do begin //상대방을 내가 먼저 때렸는지 검사
            if PTPkHiterInfo(hiter.PKHiterList[i]).hiter = self then begin
               exit;  //내가 먼저 때린 경우
            end;
         end;
         for i:=0 to PKHiterList.Count-1 do begin
            if PTPkHiterInfo(PKHiterList[i]).hiter = hiter then begin
               PTPkHiterInfo(PKHiterList[i]).hittime := GetTickCount;
               exit;
            end;
         end;
         new (pk);
         pk.hiter := hiter;
         pk.hittime := GetTickCount;
         PKHiterList.Add (pk);
         hiter.ChangeNameColor;  //나와 정당방위인 경우 색이 바뀜
      end; }
   end;
end;

{ CheckTimeOutPkHiterList - 检查PK击中者列表超时
  功能: 检查并清理超时的PK击中者记录
  实现原理: 60秒后清除先攻击标志并恢复名字颜色 }
procedure  TCreature.CheckTimeOutPkHiterList;
var
   i: integer;       // 循环变量
   hum: TUserHuman;  // 玩家对象
begin
   // 检查先攻击标志超时
   if BoIllegalAttack then begin
      if GetTickCount - IllegalAttackTime > 60 * 1000 then begin
         BoIllegalAttack := FALSE;
         ChangeNameColor;
      end;
   end;
   {end else begin
      for i:=0 to PKHiterList.Count-1 do begin
         if GetTickCount - PTPkHiterInfo(PKHiterList[i]).hittime > 60 * 1000 then begin
            hum := TUserHuman (PTPkHiterInfo(PKHiterList[i]).hiter);
            hum.ChangeNameColor;  //정방방위 해제..
            Dispose (PTPkHiterInfo(PKHiterList[i]));
            PKHiterList.Delete (i);
            break;
         end;
      end;
   end;}
end;

{ ClearPkHiterList - 清理PK击中者列表
  功能: 清空所有PK击中者记录 }
procedure  TCreature.ClearPkHiterList;
var
   i: integer;  // 循环变量
begin
   for i:=0 to PKHiterList.Count-1 do 
      Dispose (PTPkHiterInfo(PKHiterList[i]));
   PKHiterList.Clear;
end;

{ IsGoodKilling - 是否正当击杀
  功能: 判断击杀目标是否为正当防卫
  参数:
    target - 目标生物
  返回值: 是否为正当击杀(目标先攻击则为正当) }
function  TCreature.IsGoodKilling (target: TCreature): Boolean;
var
   i: integer;  // 循环变量
begin
   Result := FALSE;
   // 目标有先攻击标志则为正当击杀
   if target.BoIllegalAttack then
      Result := TRUE;
   {end else begin
      for i:=0 to PKHiterList.Count-1 do begin
         if PTPkHiterInfo(PKHiterList[i]).hiter = target then begin
            Result := TRUE;
            break;
         end;
      end;
   end;}
end;

procedure  TCreature.SetAllowLongHit (boallow: Boolean);
begin
   BoAllowLongHit := boallow;
   if BoAllowLongHit then
      SysMsg ('开启刺杀剑法', 1)
   else
      SysMsg ('关闭刺杀剑法', 1);
end;

procedure  TCreature.SetAllowWideHit (boallow: Boolean);
begin
   BoAllowWideHit := boallow;
   if BoAllowWideHit then
      SysMsg ('开启半月弯刀', 1)
   else
      SysMsg ('关闭半月弯刀', 1);
end;

procedure  TCreature.SetAllowCrossHit (boallow: Boolean);
begin
   BoAllowCrossHit := boallow;
   if BoAllowCrossHit then
      SysMsg ('开启攻杀剑法', 1)
   else
      SysMsg ('关闭攻杀剑法', 1);
end;

function  TCreature.SetAllowFireHit: Boolean;   //염화결
begin
   Result := FALSE;
   if GetTickCount - LatestFireHitTime > 10 * 1000 then begin
      LatestFireHitTime := GetTickCount;
      BoAllowFireHit := TRUE;
      SysMsg ('你的武器发出一道耐人的光芒', 1);
      Result := TRUE;
   end else begin
      SysMsg ('你还没有准备好', 0);
   end;
end;


{ IncHealthSpell - 增加生命和魔法
  功能: 增加生命值和魔法值(只允许正数)
  参数:
    hp - 生命值增加量
    mp - 魔法值增加量 }
procedure  TCreature.IncHealthSpell (hp, mp: integer);
begin
   if (hp >= 0) and (mp >= 0) then begin
      // 增加HP，不超过最大值
      if WAbil.HP + hp < WAbil.MaxHP then WAbil.HP := WAbil.HP + hp
      else WAbil.HP := WAbil.MaxHP;
      // 增加MP，不超过最大值
      if WAbil.MP + mp < Wabil.MaxMP then WAbil.MP := WAbil.MP + mp
      else WAbil.MP := WAbil.MaxMP;
      HealthSpellChanged;
   end;
end;

{ RandomSpaceMove - 随机空间移动
  功能: 随机传送到指定地图的随机位置
  参数:
    mname - 目标地图名
    mtype - 移动类型
  实现原理: 根据地图大小计算边缘距离，生成随机坐标 }
procedure TCreature.RandomSpaceMove (mname: string; mtype: integer);
var
   nx, ny, egdey: integer;       // 坐标和边缘距离
   nenvir, oldenvir: TEnvirnoment;  // 新旧环境
   hum: TUserHuman;              // 玩家对象
begin
   oldenvir := PEnvir;
   nenvir := GrobalEnvir.GetEnvir (mname);
   if nenvir <> nil then begin
      // 根据地图高度计算边缘距离
      if nenvir.MapHeight < 150 then begin
         if nenvir.MapHeight < 30 then egdey := 2
         else egdey := 20;
      end else egdey := 50;
      // 生成随机坐标
      nx := egdey + Random(nenvir.MapWidth-egdey-1);
      ny := egdey + Random(nenvir.MapHeight-egdey-1);
      SpaceMove (mname, nx, ny, mtype);
   end;
end;

{ SpaceMove - 空间移动
  功能: 传送到指定地图的指定位置
  参数:
    mname - 目标地图名
    nx, ny - 目标坐标
    mtype - 移动类型(0=普通, 1=特殊效果)
  实现原理:
    1. 同服务器: 从旧地图消失，清理可见列表，在新地图出现
    2. 跨服务器: 设置服务器切换标志 }
procedure TCreature.SpaceMove (mname: string; nx, ny, mtype: integer);
   // 随机查找可行走坐标
   function RandomEnvXY (env: TEnvirnoment; var nnx, nny: integer): Boolean;
   var
      i, step, edge: integer;  // 循环、步长、边缘
   begin
      Result := FALSE;
      if env.MapWidth < 80 then step := 3
      else step := 10;
      if env.MapHeight < 150 then begin
         if env.MapHeight < 50 then edge := 2
         else edge := 15;
      end else edge := 50;
      // 最多尝试200次
      for i:=0 to 200 do begin
         if env.CanWalk (nnx, nny, TRUE{允许重叠}) then begin
            Result := TRUE;
            break;
         end else begin
            if nnx < env.MapWidth-edge-1 then Inc (nnx,step)
            else begin
               nnx := Random(env.MapWidth);
               if nny < env.MapHeight-edge-1 then Inc (nny,step)
               else nny := Random(env.MapHeight);
            end;
         end;
      end;
   end;
var
   i, oldx, oldy, step, edge: integer;  // 循环、旧坐标、步长、边缘
   nenvir, oldenvir: TEnvirnoment;       // 新旧环境
   outofrange: pointer;                  // 超出范围标志
   success: Boolean;                     // 成功标志
   hum: TUserHuman;                      // 玩家对象
begin
   nenvir := GrobalEnvir.GetEnvir (mname);
   if nenvir <> nil then begin
      // 同服务器传送
      if ServerIndex = nenvir.Server then begin
         oldenvir := PEnvir;
         oldx := CX;
         oldy := CY;
         success := FALSE;

         // 从旧地图消失
         PEnvir.DeleteFromMap (CX, CY, OS_MOVINGOBJECT, self);

         // 清理可见列表
         MsgTargetList.Clear;
         for i:=0 to VisibleItems.Count-1 do
            Dispose (PTVisibleItemInfo (VisibleItems[i]));
         VisibleItems.Clear;
         // 2003/03/18
         i := 0;
         while TRUE do begin
            if i >= VisibleActors.Count then break;
            Dispose (PTVisibleActor(VisibleActors[i]));
            VisibleActors.Delete(i);
         end;
         VisibleActors.Clear;

         // 在新地图出现
         PEnvir := nenvir;
         MapName := nenvir.MapName;
         CX := nx;
         CY := ny;
         if RandomEnvXY (PEnvir, CX, CY) then begin
            PEnvir.AddToMap (CX, CY, OS_MOVINGOBJECT, self);
            SendMsg (self, RM_CLEAROBJECTS, 0, 0, 0, 0, '');
            SendMsg (self, RM_CHANGEMAP, 0, 0, 0, 0, MapName);
            // 根据类型发送不同的出现消息
            case mtype of
               1:    SendRefMsg (RM_SPACEMOVE_SHOW2, Dir, CX, CY, 0, '');
               else  SendRefMsg (RM_SPACEMOVE_SHOW, Dir, CX, CY, 0, '');
            end;
            MapMoveTime := GetTickCount;
            SpaceMoved := TRUE;  // 防止WalkTo失败
            success := TRUE;
         end;

         // 失败时返回旧地图
         if not success then begin
            PEnvir := oldenvir;
            CX := oldx;
            CY := oldy;
            PEnvir.AddToMap (CX, CY, OS_MOVINGOBJECT, self);
         end;

      end else begin
         // 跨服务器传送
         if RandomEnvXY (nenvir, nx, ny) then begin
            if RaceServer = RC_USERHUMAN then begin
               Disappear;
               SpaceMoved := TRUE;
               hum := TUserHuman (self);
               hum.ChangeMapName := nenvir.MapName;
               hum.ChangeCX := nx;
               hum.ChangeCY := ny;
               hum.BoChangeServer := TRUE;  // 服务器切换标志
               hum.ChangeToServerNumber := nenvir.Server;
               hum.EmergencyClose := TRUE;
               hum.SoftClosed := TRUE;  // 不终止认证
            end else begin
               KickException;  // 怪物消失
            end;
         end;
      end;
   end;
end;

{ UserSpaceMove - 用户空间移动
  功能: 用户传送到指定地图
  参数:
    mname - 目标地图名(空则使用当前地图)
    xstr, ystr - 目标坐标字符串(空则随机)
  实现原理:
    1. 有坐标则传送到指定位置
    2. 无坐标则随机传送
    3. 切换地图后取消时间回归 }
procedure  TCreature.UserSpaceMove (mname, xstr, ystr: string);
var
   xx, yy: integer;          // 目标坐标
   oldenvir: TEnvirnoment;   // 旧环境
   hum: TUserHuman;          // 玩家对象
begin
   oldenvir := PEnvir;
   if mname = '' then mname := MapName;
   // 有坐标则传送到指定位置
   if (xstr <> '') and (ystr <> '') then begin
      xx := Str_ToInt (xstr, 0);
      yy := Str_ToInt (ystr, 0);
      SpaceMove (mname, xx, yy, 0);
   end else
      // 无坐标则随机传送
      RandomSpaceMove (mname, 0);

   // 切换地图后取消时间回归
   if oldenvir <> PEnvir then begin
      if RaceServer = RC_USERHUMAN then begin
         hum := TUserHuman (self);
         hum.BoTimeRecall := FALSE;
      end;
   end;
end;

{ UseScroll - 使用卷轴
  功能: 使用各种卷轴物品
  参数:
    Shape - 卷轴类型
  返回值: 是否成功使用
  卷轴类型:
    1 - 随机传送卷: 传送到出生点
    2 - 地牢传送卷: 当前地图随机传送
    3 - 回城卷: 传送到出生点(红名去红名村)
    4 - 祝福油: 武器增加幸运
    5 - 回城传书: 传送到城堡
    9 - 修复油: 普通修复武器
    10 - 武神油: 特殊修复武器
    11 - 彩票 }
function  TCreature.UseScroll (Shape: integer): Boolean;
begin
   Result := FALSE;
   case Shape of
      1: // 随机传送卷: 传送到出生点
         if not BoTaiwanEventUser then begin
            SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
            UserSpaceMove (HomeMap, '', '');  // 随机空间移动
            Result := TRUE;
         end else
            SysMsg ('无法使用', 0);

      2: // 地牢传送卷: 当前地图随机传送
         if not BoTaiwanEventUser then begin
            if not PEnvir.NoRandomMove then begin
               // 攻城战中的内城有冷却时间
               if (UserCastle.BoCastleUnderAttack) and (PEnvir = UserCastle.CorePEnvir) then begin
                  if GetTickCount - LatestSpaceScrollTime > 10 * 1000 then begin
                     LatestSpaceScrollTime := GetTickCount;
                     SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
                     UserSpaceMove (MapName, '', '');  // 随机空间移动
                     Result := TRUE;
                  end else begin
                     SysMsg (IntToStr(10 - (GetTickCount - LatestSpaceScrollTime) div 1000) + '秒后可使用.', 0);
                     Result := FALSE;
                  end;
               end else begin
                  SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
                  UserSpaceMove (MapName, '', '');  // 随机空间移动
                  Result := TRUE;
               end;
            end;
         end else
            SysMsg ('无法使用', 0);

      3: // 回城卷
         if not BoTaiwanEventUser then begin
            SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
            // 红名去红名村
            if PKLevel < 2 then begin
               UserSpaceMove (HomeMap, IntToStr(HomeX), IntToStr(HomeY));
            end else begin
               UserSpaceMove (BADMANHOMEMAP, IntToStr(BADMANSTARTX), IntToStr(BADMANSTARTY));
            end;
            Result := TRUE;
         end else
            SysMsg ('无法使用', 0);

      4: // 祝福油: 武器增加幸运
         begin
            if MakeWeaponGoodLock then begin
               Result := TRUE;
            end;
         end;

      5: // 回城传书: 传送到城堡
         if not BoTaiwanEventUser then begin
            if MyGuild <> nil then begin
               if not BoInFreePKArea then begin
                  // 检查是否占领城堡
                  if UserCastle.IsOurCastle(TGuild(MyGuild)) then begin
                     UserSpaceMove (UserCastle.CastleMap,
                                    IntToStr(UserCastle.GetCastleStartX),
                                    IntToStr(UserCastle.GetCastleStartY));
                  end else
                     SysMsg ('无效。', 0);
                  Result := TRUE;
               end else
                  SysMsg ('当前位置无法使用。', 0);
            end;
         end else
            SysMsg ('无法使用', 0);

      9: // 修复油: 普通修复武器耐久
         begin
            if RepaireWeaponNormaly then begin
               Result := TRUE;
            end;
         end;

      10: // 武神油: 特殊修复武器耐久
         begin
            if RepaireWeaponPerfect then begin
               Result := TRUE;
            end;
         end;

      11: // 彩票
         begin
            if UseLotto then begin
               Result := TRUE;
            end;
         end;

   end;
end;

{ MakeWeaponGoodLock - 武器增加幸运
  功能: 使用祝福油给武器增加幸运
  返回值: 是否成功
  实现原理:
    1. 1/20概率受诅咒
    2. 先消除诅咒，再增加幸运
    3. 武器攻击范围越大越难增加幸运 }
function   TCreature.MakeWeaponGoodLock: Boolean;
var
   difficulty: integer;  // 难度
   flag: Boolean;        // 标志
   pstd: PTStdItem;      // 标准物品指针
begin
   Result := FALSE;
   if UseItems[U_WEAPON].Index <> 0 then begin
      difficulty := 0;
      pstd := UserEngine.GetStdItem (UseItems[U_WEAPON].Index);
      // 攻击范围越大越难增加幸运
      if pstd <> nil then begin
         difficulty := abs(Hibyte(pstd.DC) - Lobyte(pstd.DC)) div 5;
      end;

      // 1/20概率受诅咒
      if Random (20) = 1 then begin
         MakeWeaponUnlock;
      end else begin
         flag := FALSE;
         // 先消除诅咒
         if UseItems[U_WEAPON].Desc[4] > 0 then begin
            UseItems[U_WEAPON].Desc[4] := UseItems[U_WEAPON].Desc[4] - 1;
            SysMsg ('你的武器发出了光芒', 1);
            flag := TRUE;
         end else begin
            if (UseItems[U_WEAPON].Desc[3] < 1) then begin
               UseItems[U_WEAPON].Desc[3] := UseItems[U_WEAPON].Desc[3] + 1; //行운
               SysMsg ('你的武器获得了幸运', 1);
               flag := TRUE;
            end else begin
               if (UseItems[U_WEAPON].Desc[3] < 3) and (Random(6 + difficulty) = 1) then begin
                  UseItems[U_WEAPON].Desc[3] := UseItems[U_WEAPON].Desc[3] + 1; //행운
                  SysMsg ('콱돨嶠포삿돤죄龍르', 1);
                  flag := TRUE;
               end else begin
                  if (UseItems[U_WEAPON].Desc[3] < 7) and (Random(30 + difficulty * 5) = 1) then begin
                     UseItems[U_WEAPON].Desc[3] := UseItems[U_WEAPON].Desc[3] + 1; //행운
                     SysMsg ('콱돨嶠포삿돤죄龍르', 1);
                     flag := TRUE;
                  end;
               end;
            end;
         end;
         if RaceServer = 0 then begin
            RecalcAbilitys;
            SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
            SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
         end;
         if not flag then
            SysMsg ('轟槻', 1);
      Result := TRUE;
   end;
end;

{ RepaireWeaponNormaly - 普通修复武器
  功能: 使用修复油普通修复武器耐久
  返回值: 是否成功
  实现原理:
    1. 最多修复5000点耐久
    2. 修复时会损失最大耐久(1/30) }
function   TCreature.RepaireWeaponNormaly: Boolean;
var
   repair: integer;  // 修复量
begin
   Result := FALSE;
   if UseItems[U_WEAPON].Index > 0 then begin
      // 计算修复量，最多5000
      repair := _MIN(5000, (UseItems[U_WEAPON].DuraMax - UseItems[U_WEAPON].Dura));
      if repair > 0 then begin
         // 普通修复会损失最大耐久
         UseItems[U_WEAPON].DuraMax := UseItems[U_WEAPON].DuraMax - (repair div 30);
         UseItems[U_WEAPON].Dura := UseItems[U_WEAPON].Dura + repair;
         SendMsg (self, RM_DURACHANGE, U_WEAPON, UseItems[U_WEAPON].Dura, UseItems[U_WEAPON].DuraMax, 0, '');
         SysMsg ('武器已修复', 1);
         Result := TRUE;
      end;
   end;
end;

{ RepaireWeaponPerfect - 特殊修复武器
  功能: 使用武神油完美修复武器耐久
  返回值: 是否成功
  实现原理: 直接恢复到最大耐久，不损失最大耐久 }
function   TCreature.RepaireWeaponPerfect: Boolean;
begin
   Result := FALSE;
   if UseItems[U_WEAPON].Index > 0 then begin
      // 完美修复，直接恢复到最大耐久
      UseItems[U_WEAPON].Dura := UseItems[U_WEAPON].DuraMax;
      SendMsg (self, RM_DURACHANGE, U_WEAPON, UseItems[U_WEAPON].Dura, UseItems[U_WEAPON].DuraMax, 0, '');
      SysMsg ('武器已完美修复', 1);
      Result := TRUE;
   end;
end;

{ UseLotto - 使用彩票
  功能: 刮彩票
  返回值: 是否成功
  实现原理:
    1. 随机数决定中奖等级
    2. 只有当中奖总额<未中奖总额时才能中奖
    3. 奖金直接加入背包或掉落 }
function   TCreature.UseLotto: Boolean;
var
   ngold, grade: integer;  // 奖金和等级
begin
   ngold := 0;
   grade := 0;
   // 随机数决定中奖等级
   case Random(30000) of
      0..4999:      // 六等奖: 500金币
         if LottoSuccess < LottoFail then begin
            ngold := 500;
            grade := 6;
            Inc (Lotto6);
         end;
      14000..15999: // 五等奖: 1000金币
         if LottoSuccess < LottoFail then begin
            ngold := 1000;
            grade := 5;
            Inc (Lotto5);
         end;
      16000..16149: // 四等奖: 10000金币
         if LottoSuccess < LottoFail then begin
            ngold := 10000;
            grade := 4;
            Inc (Lotto4);
         end;
      16150..16169: // 三等奖: 100000金币
         if LottoSuccess < LottoFail then begin
            ngold := 100000;
            grade := 3;
            Inc (Lotto3);
         end;
      16170..16179: // 二等奖: 200000金币
         if LottoSuccess < LottoFail then begin
            ngold := 200000;
            grade := 2;
            Inc (Lotto2);
         end;
      18000:        // 一等奖: 1000000金币
         if LottoSuccess < LottoFail then begin
            ngold := 1000000;
            grade := 1;
            Inc (Lotto1);
         end;
   end;
   // 处理中奖
   if ngold > 0 then begin
      LottoSuccess := LottoSuccess + ngold;
      case grade of
         1: SysMsg ('恭喜你中了一等奖。', 1);
         2: SysMsg ('恭喜你中了二等奖。', 1);
         3: SysMsg ('恭喜你中了三等奖。', 1);
         4: SysMsg ('恭喜你中了四等奖。', 1);
         5: SysMsg ('恭喜你中了五等奖。', 1);
         6: SysMsg ('恭喜你中了六等奖。', 1);
         7: SysMsg ('恭喜你中了七等奖。', 1);
         8: SysMsg ('恭喜你中了八等奖。', 1);
      end;
      // 奖金加入背包或掉落
      if IncGold (ngold) then begin
         GoldChanged;
      end else begin
         DropGoldDown (ngold, TRUE, nil, nil);
      end;
   end else begin
      // 未中奖
      LottoFail := LottoFail + 500;
      SysMsg ('很遗憾没有中奖~', 0);
   end;
   Result := TRUE;
end;


{ MakeHolySeize - 进入神圣战甲状态
  功能: 设置神圣战甲状态
  参数:
    htime - 持续时间(毫秒) }
procedure  TCreature.MakeHolySeize (htime: integer);
begin
   BoHolySeize := TRUE;
   HolySeizeStart := GetTickCount;
   HolySeizeTime := htime;
   ChangeNameColor;
end;

{ BreakHolySeize - 解除神圣战甲状态
  功能: 解除神圣战甲状态 }
procedure  TCreature.BreakHolySeize;
begin
   BoHolySeize := FALSE;
   ChangeNameColor;
end;

{ MakeCrazyMode - 进入狂暴模式
  功能: 设置狂暴模式状态
  参数:
    csec - 持续时间(秒) }
procedure  TCreature.MakeCrazyMode (csec: integer);
begin
   BoCrazyMode := TRUE;
   CrazyModeStart := GetTickCount;
   CrazyModeTime := csec * 1000;
   ChangeNameColor;
end;

{ BreakCrazyMode - 解除狂暴模式
  功能: 解除狂暴模式状态 }
procedure  TCreature.BreakCrazyMode;
begin
   if BoCrazyMode then begin
      BoCrazyMode := FALSE;
      ChangeNameColor;
   end;
end;

{ MakeOpenHealth - 开启血量显示
  功能: 开启向周围显示血量
  实现原理: 设置状态标志并广播血量信息 }
procedure  TCreature.MakeOpenHealth;
begin
   BoOpenHealth := TRUE;
   CharStatusEx := CharStatusEx or STATE_OPENHEATH;
   CharStatus := GetCharStatus;
   SendRefMsg (RM_OPENHEALTH, 0, WAbil.HP, WAbil.MaxHP, 0, '');
end;

{ BreakOpenHealth - 关闭血量显示
  功能: 关闭向周围显示血量 }
procedure  TCreature.BreakOpenHealth;
begin
   if BoOpenHealth then begin
      BoOpenHealth := FALSE;
      CharStatusEx := CharStatusEx xor STATE_OPENHEATH;
      CharStatus := GetCharStatus;
      SendRefMsg (RM_CLOSEHEALTH, 0, 0, 0, 0, '');
   end;
end;

{ GetHitStruckDamage - 计算物理伤害
  功能: 根据自身防御力计算实际受到的物理伤害
  参数:
    hiter - 攻击者(可为nil)
    damage - 原始伤害值
  返回值: 实际伤害值
  实现原理:
    1. 伤害 = 原始伤害 - 随机防御
    2. 亡灵类怪物受额外伤害
    3. 魔法盾可减少伤害 }
function   TCreature.GetHitStruckDamage (hiter: TCreature; damage: integer): integer;
var
   armor: integer;  // 防御值
begin
   // 计算随机防御值
   armor := Lobyte(WAbil.AC) + Random(ShortInt(Hibyte(WAbil.AC)-Lobyte(WAbil.AC)) + 1);
   damage := _MAX(0, damage - armor);
   // 亡灵类怪物受额外伤害
   if (LifeAttrib = LA_UNDEAD) and (hiter <> nil) then begin
      damage := damage + hiter.AddAbil.UndeadPower;
   end;
   // 魔法盾减伤
   if damage > 0 then begin
      if BoAbilMagBubbleDefence then begin
         damage := Round (damage / 100 * (MagBubbleDefenceLevel + 2) * 8);
         DamageBubbleDefence;
      end;
   end;
   Result := damage;
end;

{ GetMagStruckDamage - 计算魔法伤害
  功能: 根据自身魔法防御计算实际受到的魔法伤害
  参数:
    hiter - 攻击者(可为nil)
    damage - 原始伤害值
  返回值: 实际伤害值
  实现原理:
    1. 伤害 = 原始伤害 - 随机魔防
    2. 亡灵类怪物受额外伤害
    3. 魔法盾可减少伤害 }
function   TCreature.GetMagStruckDamage (hiter: TCreature; damage: integer): integer;
var
   armor: integer;  // 魔防值
begin
   // 计算随机魔防值
   armor := Lobyte(WAbil.MAC) + Random(ShortInt(Hibyte(WAbil.MAC)-Lobyte(WAbil.MAC)) + 1);
   damage := _MAX(0, damage - armor);
   // 亡灵类怪物受额外伤害
   if (LifeAttrib = LA_UNDEAD) and (hiter <> nil) then begin
      damage := damage + hiter.AddAbil.UndeadPower;
   end;
   // 魔法盾减伤
   if damage > 0 then begin
      if BoAbilMagBubbleDefence then begin
         damage := Round (damage / 100 * (MagBubbleDefenceLevel + 2) * 8);
         DamageBubbleDefence;
      end;
   end;
   Result := damage;
end;

{ StruckDamage - 受到伤害
  功能: 处理受到伤害的逻辑
  参数:
    damage - 伤害值
    hiter - 攻击者
  实现原理:
    1. 设置最后击中者
    2. 装备耐久损耗
    3. 中毒时伤害增加20%
    4. 扣除生命值 }
procedure  TCreature.StruckDamage (damage: integer; hiter : TCreature );
var
   i, wdam, adura, old: integer;  // 循环、装备伤害、耐久、旧值
   bocalc: Boolean;               // 是否重算能力
   hum: TUserHuman;               // 玩家对象
begin
   if damage > 0 then begin
      // 设置最后击中者
      if(hiter <> nil) then SetLastHiter(hiter);

      // 计算装备耐久损耗
      wdam := Random (10) + 5;

      // 中毒时装备损耗和伤害增加20%
      if StatusArr[POISON_DAMAGEARMOR] > 0 then begin
         wdam := Round(wdam * 1.2);
         damage := Round(damage * 1.2);
      end;

      bocalc := FALSE;
      // 衣服基本损耗
      if (UseItems[U_DRESS].Index > 0) and (UseItems[U_DRESS].Dura > 0) then begin
         adura := UseItems[U_DRESS].Dura;
         old := Round (adura / 1000);
         adura := adura - wdam;
         if adura <= 0 then begin
            (*if RaceServer = RC_USERHUMAN then begin
               hum := TUserHuman(self);
               hum.SendDelItem (UseItems[U_DRESS]); //클라이언트에 없어진거 보냄
               //닳아 없어진거 로그 남김
               AddUserLog ('3'#9 +  //닳음_ +
                           MapName + ''#9 +
                           IntToStr(CX) + ''#9 +
                           IntToStr(CY) + ''#9 +
                           UserName + ''#9 +
                           UserEngine.GetStdItemName (UseItems[U_DRESS].Index) + ''#9 +
                           IntToStr(UseItems[U_DRESS].MakeIndex) + ''#9 +
                           IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                           '0');

               SysMsg (UserEngine.GetStdItemName (UseItems[U_DRESS].Index) + '넣씹綠苟슉逞0.', 0);
               UseItems[U_DRESS].Index := 0;
               hum.FeatureChanged;
            end;
            UseItems[U_DRESS].Dura := 0;
            UseItems[U_DRESS].Index := 0;    *)

            SysMsg (UserEngine.GetStdItemName (UseItems[U_DRESS].Index) + '넣씹綠苟슉逞0.', 0);
            // SysMsg (UserEngine.GetStdItemName (UseItems[U_DRESS].Index) + ' 아이템이 다 닳았습니다.', 0);
            UseItems[U_DRESS].Dura := 0;
            bocalc := TRUE;
         end else
            UseItems[U_DRESS].Dura := adura;

         if old <> Round (adura / 1000) then begin
            //내구성이 변함
            SendMsg (self, RM_DURACHANGE, U_DRESS, adura, UseItems[U_DRESS].DuraMax, 0, '');
         end;
      end;
      // 2003/03/15 아이템 인벤토리 확장
      // 2003/04/01 아이템 내구 조정
      for i:=1 to 11 do begin  // 8->11...수호석은 내구 없음, 부적도 내구 않닳음
         if (UseItems[i].Index > 0) and (UseItems[i].Dura > 0) and (Random(8) = 0) and (i<>U_BUJUK) then begin
            adura := UseItems[i].Dura;
            old := Round (adura / 1000);
            adura := adura - wdam;
            if adura <= 0 then begin
               (*if RaceServer = RC_USERHUMAN then begin
                  hum := TUserHuman(self);
                  hum.SendDelItem (UseItems[i]); //클라이언트에 없어진거 보냄
                  //닳아 없어진거 로그 남김
                  AddUserLog ('3'#9 +  //닳음_ +
                           MapName + ''#9 +
                           IntToStr(CX) + ''#9 +
                           IntToStr(CY) + ''#9 +
                           UserName + ''#9 +
                           UserEngine.GetStdItemName (UseItems[i].Index) + ''#9 +
                           IntToStr(UseItems[i].MakeIndex) + ''#9 +
                           IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                           '0');
                  SysMsg (UserEngine.GetStdItemName (UseItems[i].Index) + '넣씹綠苟슉逞0.', 0);
                  UseItems[i].Index := 0;
                  hum.FeatureChanged;
               end;
               UseItems[i].Dura := 0;
               UseItems[i].Index := 0; *)

               SysMsg (UserEngine.GetStdItemName (UseItems[i].Index) + '넣씹綠苟슉逞0.', 0);
               //SysMsg (UserEngine.GetStdItemName (UseItems[i].Index) + '넣씹綠苟슉逞0.', 0);
               UseItems[i].Dura := 0;
               bocalc := TRUE;
            end else
               UseItems[i].Dura := adura;

            if old <> Round (adura / 1000) then begin
               //내구성이 변함
               SendMsg (self, RM_DURACHANGE, i, adura, UseItems[i].DuraMax, 0, '');
            end;
         end;
      end;
      if bocalc then begin
         RecalcAbilitys; //무기가 다 닳았으면 능력치 다시 계산
         SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
         SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
      end;
      //체력이 닳는다.

      DamageHealth (damage);
   end;
end;

{ DamageHealth - 伤害生命值
  功能: 处理生命值变化
  参数:
    damage - 伤害值(正数为伤害，负数为恢复)
  实现原理:
    1. 有魔法盾时用MP抵消伤害(1.5倍换算)
    2. 扣除或恢复生命值 }
procedure TCreature.DamageHealth (damage: integer);
var
   spdam: integer;  // 魔法盾消耗
 begin
   // 魔法盾抵消伤害
   if BoMagicShield and (damage > 0) and (WAbil.MP > 0) then begin
      spdam := Round (damage * 1.5);  // 1.5倍MP换算
      if integer(WAbil.MP) >= spdam then begin
         WAbil.MP := WAbil.MP - spdam;
         spdam := 0;
      end else begin
         spdam := spdam - WAbil.MP;
         WAbil.MP := 0;
      end;
      damage := Round (spdam / 1.5);
      HealthSpellChanged;
   end;
   // 扣除或恢复生命值
   if damage > 0 then begin
      if integer(WAbil.HP) - damage > 0 then WAbil.HP := WAbil.HP - damage
      else WAbil.HP := 0;
   end else begin
      if integer(WAbil.HP) - damage < WAbil.MaxHP then WAbil.HP := WAbil.HP - damage
      else WAbil.HP := WAbil.MaxHP;
   end;
end;

{ DamageSpell - 伤害魔法值
  功能: 处理魔法值变化
  参数:
    val - 变化值(正数为消耗，负数为恢复) }
procedure TCreature.DamageSpell (val: integer);
begin
   if val > 0 then begin
      // 消耗MP
      if WAbil.MP - val > 0 then WAbil.MP := WAbil.MP - val
      else WAbil.MP := 0;
   end else begin
      // 恢复MP
      if WAbil.MP - val < WAbil.MaxMP then WAbil.MP := WAbil.MP - val
      else WAbil.MP := WAbil.MaxMP;
   end;
end;

{ CalcGetExp - 计算获得经验值
  功能: 根据目标等级和血量计算经验值
  参数:
    targlevel - 目标等级
    targhp - 目标血量(战斗经验)
  返回值: 经验值
  实现原理: 等级差距超过10级时经验减少 }
function  TCreature.CalcGetExp (targlevel, targhp: integer): integer;
begin
   // 等级差距在10级内时获得全额经验
   if Abil.Level < (targlevel+10) then Result := targhp
   // 等级差距超过10级时经验递减
   else Result := targhp - Round ((targhp / 15) * (Abil.Level - (targlevel+10)));
   if Result <= 0 then Result := 1;
end;

{ GainExp - 获得经验值
  功能: 处理经验值获取和分配
  参数:
    exp - 经验值
  实现原理:
    1. 组队时经验按等级比例分配
    2. 组队人数越多经验加成越高(1.2-2.2倍)
    3. 只有同地图12格内的队员参与分配 }
procedure TCreature.GainExp (exp: longword);
var
   i, n, sumlv: integer;   // 循环、人数、等级总和
   dexp, iexp: longword;   // 分配经验、个人经验
   cret: TCreature;        // 生物对象
const
   // 组队经验加成
   bonus: array[0..GROUPMAX] of Real = (1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2, 2.1, 2.2);
begin
   try
      // 组队时分配经验
      if GroupOwner <> nil then begin
         sumlv := 0;
         n := 0;
         // 统计附近队员等级总和
         for i:=0 to GroupOwner.GroupMembers.Count-1 do begin
            cret := TCreature(GroupOwner.GroupMembers.Objects[i]);
            if not cret.Death and (PEnvir = cret.PEnvir) and (abs(CX-cret.CX) <= 12) and (abs(CY-cret.CY) <= 12) then begin
               sumlv := sumlv + cret.Abil.Level;
               Inc (n);
            end;
         end;
         // 多人时按等级比例分配
         if (sumlv > 0) and (n > 1) then begin
            dexp := 0;
            if n in [0..GROUPMAX] then
               dexp := Round (exp * bonus[n]);  // 应用组队加成
            for i:=0 to GroupOwner.GroupMembers.Count-1 do begin
               cret := TCreature(GroupOwner.GroupMembers.Objects[i]);
               if not cret.Death and (PEnvir = cret.PEnvir) and (abs(CX-cret.CX) <= 12) and (abs(CY-cret.CY) <= 12) then begin
                  iexp := Round (dexp / sumlv * cret.Abil.Level);  // 按等级比例
                  if iexp > exp then iexp := exp;
                  cret.WinExp (iexp);
               end;
            end;
         end else
            WinExp (exp);
      end else
         WinExp (exp);
   except
      MainOutMessage ('[Exception] TCreature.GainExp');
   end;
end;

{ GainSlaveExp - 召唤兽获得经验
  功能: 召唤兽获得经验并升级
  参数:
    exp - 经验值
  实现原理:
    1. 累加经验值
    2. 达到升级经验时升级
    3. 最高等级为召唤等级*2+1 }
procedure  TCreature.GainSlaveExp (exp: integer);
   // 计算下一级所需经验
   function NextExp: integer;
   const
      slaveupexp: array[0..6] of integer = (0, 0, 50, 100, 200, 300, 600);
   var
      more: integer;
   begin
      if SlaveExpLevel in [0..6] then more := slaveupexp[SlaveExpLevel]
      else more := 0;
      Result := 100 + (Abil.Level * 15) + more;
   end;
begin
   SlaveExp := SlaveExp + exp;
   // 检查是否升级
   if SlaveExp > NextExp then begin
      SlaveExp := SlaveExp - NextExp;
      // 最高等级限制
      if SlaveExpLevel < (SlaveMakeLevel * 2 + 1) then begin
         Inc (SlaveExpLevel);
         RecalcAbilitys;
procedure  TCreature.ApplySlaveLevelAbilitys;
var
   i, chp: integer;  // 循环、计算血量
begin
   WAbil.DC := MakeWord(Lobyte(WAbil.DC), Hibyte(Abil.DC));
   chp := 0;
   // 白骨/神兽类召唤兽
   if (RaceServer = RC_WHITESKELETON) or
      (RaceServer = RC_ELFMON) or
      (RaceServer = RC_ELFWARRIORMON)
   then begin
      // 攻击力增加
      WAbil.DC := MakeWord(Lobyte(WAbil.DC), Round(Hibyte(WAbil.DC) + (3 * (0.3 + SlaveExpLevel * 0.1) * SlaveExpLevel)));
      // 血量增加
      chp := chp + Round(Abil.MaxHP * (0.3 + SlaveExpLevel * 0.1)) * SlaveExpLevel;
      chp := Abil.MaxHP + chp;
      if SlaveExpLevel > 0 then begin
         WAbil.MaxHP := chp;
      end else
         WAbil.MaxHP := Abil.MaxHP;
      // 2003/03/15 新技能加成
      WAbil.DC := MakeWord(
                     Lobyte(WAbil.DC),
                     Hibyte(WAbil.DC) + ExtraAbil[EABIL_DCUP]
                  );
   end else begin
      // 驯服怪物
      chp := Abil.MaxHP;
      // 攻击力增加较少
      WAbil.DC := MakeWord(Lobyte(WAbil.DC), Round(Hibyte(WAbil.DC) + (2 * SlaveExpLevel)));
      chp := chp + Round(Abil.MaxHP * 0.15) * SlaveExpLevel;
      WAbil.MaxHP := _MIN(Round(Abil.MaxHP + 60 * SlaveExpLevel), chp);
      WAbil.MAC := 0;  // 驯服怪物魔防为0
   end;
   // 精确度固定为15
   AccuracyPoint := 15;
end;

{ WinExp - 获得经验值
  功能: 获得经验值并检查升级
  参数:
    exp - 经验值
  实现原理:
    1. 经验值上限60000
    2. 测试服务器经验翻倍
    3. 获得经验时增加运气
    4. 升级时恢复生命魔法 }
procedure TCreature.WinExp (exp: longword);
begin
   // 经验值上限
   if exp >= 60000 then exp := 60000;
   // 测试服务器经验翻倍
   if BoTestServer then
      Abil.Exp := Abil.Exp + exp + exp
   else
      Abil.Exp := Abil.Exp + exp;

   // 获得经验时增加运气
   AddBodyLuck (exp * 0.002);

   // 发送经验值消息
   if BoTestServer then
      SendMsg (self, RM_WINEXP, 0, exp + exp, 0, 0, '')
   else
      SendMsg (self, RM_WINEXP, 0, exp, 0, 0, '');
   // 检查升级
   if Abil.Exp >= Abil.MaxExp then begin
      Abil.Exp := Abil.Exp - Abil.MaxExp;
      Inc (Abil.Level);
      HasLevelUp (Abil.Level-1);
      AddBodyLuck (100);  // 升级增加运气
      // 记录升级日志
      AddUserLog ('12'#9 +
                  MapName + ''#9 +
                  IntToStr(Abil.Level) + ''#9 +
                  IntToStr(Abil.Exp) + ''#9 +
                  UserName + ''#9 +
                  '0'#9 +
                  '0'#9 +
                  '1'#9 +
                  '0');
      // 升级恢复生命魔法
      IncHealthSpell (2000, 2000);
   end;
end;

{ HasLevelUp - 升级处理
  功能: 处理升级后的能力值计算
  参数:
    prevlevel - 升级前的等级
  实现原理:
    1. 计算下一级所需经验
    2. 重算等级能力
    3. 分配奖励点数 }
procedure TCreature.HasLevelUp (prevlevel: integer);
begin
   // 计算下一级所需经验
   Abil.MaxExp := GetNextLevelExp (Abil.Level);
   // 重算等级能力
   RecalcLevelAbilitys;
   //end else
      //RecalcLevelAbilitys_old;

{$IFDEF FOR_ABIL_POINT}
//4/16일 부터 적용
   if prevlevel + 1 = Abil.Level then begin
      BonusPoint := BonusPoint + GetBonusPoint (Job, Abil.Level);  //렙업에 따른 보너스
      SendMsg (self, RM_ADJUST_BONUS, 0, 0, 0, 0, '');
   end else begin
      if prevlevel <> Abil.Level then begin
         //보너스 포인트를 처음부터 다시 계산한다.
         BonusPoint := GetLevelBonusSum (Job, Abil.Level);
         FillChar (BonusAbil, sizeof(TNakedAbility), #0);
         FillChar (CurBonusAbil, sizeof(TNakedAbility), #0);
         //if prevlevel <> 0 then begin
         RecalcLevelAbilitys;  //레벨에 따른 능력치를 계산한다.
         //end else begin
         //   RecalcLevelAbilitys_old;
         //   BonusPoint := 0;
         //end;
         SendMsg (self, RM_ADJUST_BONUS, 0, 0, 0, 0, '');
      end;
   end;
{$ENDIF}

   RecalcAbilitys;
   SendMsg (self, RM_LEVELUP, 0, Abil.Exp, 0, 0, '');
end;

{ GetNextLevelExp - 获取下一级所需经验
  功能: 获取指定等级升级所需经验值
  参数:
    lv - 等级
  返回值: 所需经验值 }
function TCreature.GetNextLevelExp (lv: integer): longword;
begin
   if lv in [1..MAXLEVEL] then Result := NEEDEXPS[lv]
   else Result := $FFFFFFFF;
end;

{ ChangeLevel - 改变等级
  功能: 设置生物等级
  参数:
    level - 目标等级(1-40) }
procedure  TCreature.ChangeLevel (level: integer);
begin
   if level in [1..40] then
      Abil.Level := level;
end;

{ InSafeZone - 是否在安全区
  功能: 判断是否在不能战斗的安全区
  返回值: 是否在安全区
  实现原理:
    1. 检查地图是否为安全地图
    2. 检查是否在红名村附近
    3. 检查是否在出生点附近 }
function  TCreature.InSafeZone: Boolean;
var
   map: string;       // 地图名
   i, sx, sy: integer;  // 循环、坐标
begin
   // 检查地图是否为安全地图
   Result := PEnvir.Lawfull;
   if not Result then begin
      // 检查是否在红名村附近
      Result := (PEnvir.MapName = BADMANHOMEMAP) and
                  ((abs(CX-BADMANSTARTX) <= 10) and (abs(CY-BADMANSTARTY) <= 10));
      if not Result then begin
         // 检查是否在出生点附近
         for i:=0 to StartPoints.Count-1 do begin
            map := StartPoints[i];
            sx := Loword(integer(StartPoints.Objects[i]));
            sy := Hiword(integer(StartPoints.Objects[i]));
            if (map = PEnvir.MapName) and ((abs(CX-sx) <= 10) and (abs(CY-sy) <= 10)) then begin
               Result := TRUE;
               break;
            end;
         end;
      end;
   end;
end;

{ InGuildWarSafeZone - 是否在行会战安全区
  功能: 判断是否在不能进行行会战的安全区
  返回值: 是否在行会战安全区
  实现原理: 出生点60格内为行会战安全区 }
function   TCreature.InGuildWarSafeZone: Boolean;
var
   map: string;       // 地图名
   i, sx, sy: integer;  // 循环、坐标
begin
   Result := PEnvir.Lawfull;
   if not Result then begin
      if not Result then begin
         // 检查是否在出生点60格内
         for i:=0 to StartPoints.Count-1 do begin
            map := StartPoints[i];
            sx := Loword(integer(StartPoints.Objects[i]));
            sy := Hiword(integer(StartPoints.Objects[i]));
            if (map = PEnvir.MapName) and ((abs(CX-sx) <= 60) and (abs(CY-sy) <= 60)) then begin
               Result := TRUE;
               break;
            end;
         end;
      end;
   end;
end;

{ PKLevel - 获取PK等级
  功能: 获取当前PK等级
  返回值: PK等级(1以上为PK犯) }
function  TCreature.PKLevel: integer;
begin
   Result := PlayerKillingPoint div 100;
end;

{ UserNameChanged - 用户名改变
  功能: 广播用户名改变消息 }
procedure  TCreature.UserNameChanged;
begin
   SendRefMsg (RM_USERNAME, 0, 0, 0, 0, GetUserName);
end;

{ ChangeNameColor - 改变名字颜色
  功能: 广播名字颜色改变消息 }
procedure TCreature.ChangeNameColor;
begin
   SendRefMsg (RM_CHANGENAMECOLOR, 0, 0, 0, 0, '');
end;

{ MyColor - 获取自身颜色
  功能: 获取自身名字颜色
  返回值: 颜色值
  实现原理:
    - PK等级1: 黄色
    - PK等级>=2: 红色 }
function  TCreature.MyColor: byte;
begin
   Result := DefNameColor;
   if PKLevel = 1 then Result := 251;   // 黄色
   if PKLevel >= 2 then Result := 249;  // 红色
end;

{ GetThisCharColor - 获取目标角色颜色
  功能: 获取目标角色在自己视角中的颜色
  参数:
    cret - 目标生物
  返回值: 颜色值
  实现原理:
    1. 玩家: 根据行会关系、攻城战等决定颜色
    2. 怪物: 根据召唤兽等级、狂暴状态等决定颜色 }
function  TCreature.GetThisCharColor (cret: TCreature): byte;
const
   // 召唤兽等级颜色
   SlaveColors : array[0..7] of byte = (255, 254, 147, 154, 229, 168, 180, 252);
var
   relat          : integer;   // 行会关系
   pkarea         : Boolean;   // PK区域
   CheckAllyGuild : Boolean;   // 检查同盟行会
begin
   Result := cret.MyColor;
   // 玩家颜色处理
   if cret.RaceServer = RC_USERHUMAN then begin
      // 非红名玩家
      if PKLevel < 2 then begin
         // 先攻击者显示棕色
         if cret.BoIllegalAttack then
            Result := 47;

         // 根据行会关系设置颜色
         relat := GetGuildRelation (self, cret);
         case relat of
            1, 3: Result := 180;  // 蓝色(我方)
            2: Result := 69;      // 橙色(敌方)
         end;

         // 门派比武场内
         if cret.PEnvir.Fight3Zone then begin
            if MyGuild = cret.MyGuild then
               Result := 180  // 蓝色(同门派)
            else
               Result := 69;  // 橙色(不同门派)
         end;
      end;

      // 攻城战相关颜色
      if UserCastle.BoCastleUnderAttack then begin
         if BoInFreePKArea and cret.BoInFreePKArea then begin
            Result := 221;  // 绿色(中立)
            BoGuildWarArea := TRUE;

            // 检查同盟行会
            if ( UserCastle.OwnerGuild <> nil ) and ( MyGuild <> nil ) then
                CheckAllyGuild := TGuild(UserCastle.OwnerGuild).IsAllyGuild(TGuild(MyGuild))
            else
                CheckAllyGuild := false;

            // 守城方
            if UserCastle.IsOurCastle (TGuild(MyGuild)) or CheckAllyGuild then begin
               if (MyGuild = cret.MyGuild) or TGuild(MyGuild).IsAllyGuild(TGuild(cret.MyGuild)) then
                  Result := 180  // 蓝色(我方)
               else if UserCastle.IsRushCastleGuild (TGuild(cret.MyGuild)) then
                     Result := 69   // 橙色(攻城方)
            end else begin
               // 攻城方
               if UserCastle.IsRushCastleGuild (TGuild(MyGuild)) then begin
                  if (MyGuild = cret.MyGuild) or TGuild(MyGuild).IsAllyGuild(TGuild(cret.MyGuild)) then begin
                     Result := 180  // 蓝色(我方)
                  end else begin
                     if UserCastle.IsCastleMember(TUserHuman(cret)) then
                        Result := 69;  // 橙色(守城方)
                  end;
               end;
            end;
         end;
      end;

   end else begin
      // 怪物颜色处理
      if cret.SlaveExpLevel in [0..7] then
         Result := SlaveColors[cret.SlaveExpLevel];
      if cret.BoCrazyMode then Result := 249;  // 红色(狂暴)
      if cret.BoHolySeize then Result := 125;  // 神圣战甲
   end;
end;

{ GetGuildRelation - 获取行会关系
  功能: 获取两个生物之间的行会关系
  参数:
    onecret - 生物1
    twocret - 生物2
  返回值:
    0 - 无关系
    1 - 同行会
    2 - 敌对关系
    3 - 同盟关系 }
function  TCreature.GetGuildRelation (onecret, twocret: TCreature): integer;
begin
   Result := 0;
   BoGuildWarArea := FALSE;
   if (onecret.MyGuild <> nil) and (twocret.MyGuild <> nil) then begin
      // 行会战安全区内无关系
      if onecret.InGuildWarSafeZone or twocret.InGuildWarSafeZone then begin
         Result := 0;
      end else begin
         if TGuild(onecret.MyGuild).KillGuilds.Count > 0 then begin
            BoGuildWarArea := TRUE;
            // 检查敌对关系
            if TGuild(onecret.MyGuild).IsHostileGuild (TGuild(twocret.MyGuild)) and
               TGuild(twocret.MyGuild).IsHostileGuild (TGuild(onecret.MyGuild))
            then begin
               Result := 2;  // 敌对
            end;
            // 检查同行会
            if TGuild(onecret.MyGuild) = TGuild(twocret.MyGuild) then begin
               Result := 1;  // 同行会
            end;
            // 检查同盟关系
            if (TGuild(onecret.MyGuild).IsAllyGuild(TGuild(twocret.MyGuild))) and
               (TGuild(twocret.MyGuild).IsAllyGuild(TGuild(onecret.MyGuild))) then
               Result := 3;  // 同盟
         end;
      end;
   end;
end;

{ IsGuildMaster - 是否行会掌门
  功能: 判断是否为行会掌门人
  返回值: 是否为掌门 }
function  TCreature.IsGuildMaster: Boolean;
begin
   if (MyGuild <> nil) and (GuildRank = 1) then
      Result := TRUE
   else
      Result := FALSE;      
end;

{ IncPKPoint - 增加PK点数
  功能: 增加PK点数
  参数:
    point - 增加的点数
  实现原理: PK等级变化时改变名字颜色 }
procedure TCreature.IncPKPoint (point: integer);
var
   old: integer;  // 旧PK等级
begin
   old := PKLevel;
   PlayerKillingPoint := PlayerKillingPoint + point;
   // PK等级变化时改变名字颜色
   if (old <> PKLevel) and (PKLevel <= 2) then begin
      ChangeNameColor;
   end;
end;

{ DecPKPoint - 减少PK点数
  功能: 减少PK点数
  参数:
    point - 减少的点数
  实现原理: PK等级变化时改变名字颜色 }
procedure TCreature.DecPKPoint (point: integer);
var
   old: integer;  // 旧PK等级
begin
   old := PKLevel;
   PlayerKillingPoint := PlayerKillingPoint - point;
   if PlayerKillingPoint < 0 then PlayerKillingPoint := 0;
   // PK等级变化时改变名字颜色
   if (old <> PKLevel) and (old > 0) and (old <= 2) then begin
      ChangeNameColor;
   end;
end;

{ AddBodyLuck - 增加身体运气
  功能: 增加或减少身体运气值
  参数:
    r - 运气变化值(正数增加，负数减少)
  实现原理: 运气等级范围-10到55 }
procedure  TCreature.AddBodyLuck (r: Real);
var
   n: integer;  // 运气等级
begin
   // 增加运气(有上限)
   if (r > 0) and (BodyLuck < 5 * BODYLUCKUNIT) then
      BodyLuck := BodyLuck + r;
   // 减少运气(有下限)
   if (r < 0) and (BodyLuck > -(5 * BODYLUCKUNIT)) then
      BodyLuck := BodyLuck + r;
   // 计算运气等级
   n := Trunc (BodyLuck / BODYLUCKUNIT);
   if n > 5 then n := 5;
   if n < -10 then n := -10;
   BodyLuckLevel := n;
end;

{ IncGold - 增加金币
  功能: 增加金币数量
  参数:
    igold - 增加的金币数
  返回值: 是否成功(超过上限则失败) }
function  TCreature.IncGold (igold: integer): Boolean;
begin
   Result := FALSE;
   if Int64(Gold) + igold <= AvailableGold then begin
      Gold := Gold + igold;
      Result := TRUE;
   end;
end;

{ DecGold - 减少金币
  功能: 减少金币数量
  参数:
    igold - 减少的金币数
  返回值: 是否成功(不足则失败) }
function  TCreature.DecGold (igold: integer): Boolean;
begin
   Result := FALSE;
   if Int64(Gold) - igold >= 0 then begin
      Gold := Gold - igold;
      Result := TRUE;
   end;
end;                                        

{ CalcBagWeight - 计算背包重量
  功能: 计算背包中所有物品的总重量
  返回值: 总重量 }
function  TCreature.CalcBagWeight: integer;
var
   i, w: integer;   // 循环、重量
   ps: PTStdItem;   // 标准物品指针
begin
   w := 0;
   for i:=0 to ItemList.Count-1 do begin
      ps := UserEngine.GetStdItem (PTUserItem(Itemlist[i]).Index);
      if ps <> nil then begin
         w := w + ps.Weight;
      end;
   end;
   Result := w;
end;

{ CalcWearWeightEx - 计算穿戴重量(扩展)
  功能: 计算装备的总重量(排除指定位置)
  参数:
    windex - 排除的装备位置(-1表示不排除)
  返回值: 总重量 }
function  TCreature.CalcWearWeightEx (windex: integer): integer;
var
   i, w: integer;   // 循环、重量
   ps: PTStdItem;   // 标准物品指针
begin
   w := 0;
   // 2003/03/15 物品栏扩展 8->12
   for i:=0 to 12 do begin
      // 排除指定位置、武器和副手
      if (windex = -1) or (i <> windex) and (i <> U_WEAPON) and (i <> U_RIGHTHAND) then begin
         ps := UserEngine.GetStdItem (UseItems[i].Index);
         if ps <> nil then
            w := w + ps.Weight;
      end;
   end;
   Result := w;
end;

{$IFDEF FOR_ABIL_POINT}
// 4/16日开始应用

{ RecalcLevelAbilitys - 重算等级能力值
  功能: 根据等级计算基础能力值
  实现原理:
    1. 根据职业计算不同的能力值
    2. 战士: 高HP、高负重、高攻击
    3. 法师: 高MP、高魔法
    4. 道士: 平衡型 }
procedure TCreature.RecalcLevelAbilitys;
var
   n, mlevel: integer;  // 临时变量、调整等级
begin
   // 等级超过调整等级时使用调整等级
   if Abil.Level > ADJ_LEVEL then mlevel := ADJ_LEVEL
   else mlevel := Abil.Level;
   case Job of
      0: // 战士
         begin
            Abil.MaxWeight := 50 + Round((Abil.Level / 3) * Abil.Level);
            Abil.MaxWearWeight := 15 + Round((Abil.Level / 20) * Abil.Level);
            // 2003/02/11 最大重量限制255
            if( (12 + Round((Abil.Level / 13) * Abil.Level)) > 255 ) then Abil.MaxHandWeight := 255
            else
               Abil.MaxHandWeight := 12 + Round((Abil.Level / 13) * Abil.Level);
            Abil.MaxHP := DEFHP + Round((mlevel / 4 + 4) * mlevel);
            Abil.MaxMP := DEFMP + mlevel * 2;
            Abil.DC := MakeWord(_MAX(mlevel div 7 - 1, 1), _MAX(1, mlevel div 5));
            Abil.SC := 0;
            Abil.MC := 0;
            Abil.AC := MakeWord(0, mlevel div 7);
            Abil.MAC := 0;
         end;
      1: // 法师
         begin
            Abil.MaxWeight := 50 + Round((Abil.Level / 5) * Abil.Level);
            Abil.MaxWearWeight := 15 + Round((Abil.Level / 100) * Abil.Level);
            Abil.MaxHandWeight := 12 + Round((Abil.Level / 90) * Abil.Level);
            Abil.MaxHP := DEFHP + Round((mlevel / 15 + 1.8) * mlevel);
            Abil.MaxMP := DEFMP + Round((mlevel / 5 + 2)*2.2 * mlevel);
            n := mlevel div 7;
            Abil.DC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.MC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.SC := 0;
            Abil.AC := 0;
            Abil.MAC := 0;
         end;
      2: // 道士
         begin
            Abil.MaxWeight := 50 + Round((Abil.Level / 4) * Abil.Level);
            Abil.MaxWearWeight := 15 + Round((Abil.Level / 50) * Abil.Level);
            Abil.MaxHandWeight := 12 + Round((Abil.Level / 42) * Abil.Level);
            Abil.MaxHP := DEFHP + Round((mlevel / 6 + 2.5) * mlevel);
            Abil.MaxMP := DEFMP + Round((mlevel / 8)*2.2 * mlevel);
            n := mlevel div 7;
            Abil.DC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.MC := 0;
            Abil.SC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.AC := 0;
            n := Round(mlevel / 6);
            Abil.MAC := MakeWord(n div 2, n+1);
         end;
   end;
   Abil.MaxHP := Abil.MaxHP + BonusAbil.HP;
   Abil.MaxMP := Abil.MaxMP + BonusAbil.MP;
   Abil.DC := MakeWord(Lobyte(Abil.DC) + Lobyte(BonusAbil.DC), Hibyte(Abil.DC) + Hibyte(BonusAbil.DC));
   Abil.SC := MakeWord(Lobyte(Abil.SC) + Lobyte(BonusAbil.SC), Hibyte(Abil.SC) + Hibyte(BonusAbil.SC));
   Abil.MC := MakeWord(Lobyte(Abil.MC) + Lobyte(BonusAbil.MC), Hibyte(Abil.MC) + Hibyte(BonusAbil.MC));
   Abil.AC  := MakeWord(Lobyte(Abil.AC) + Lobyte(BonusAbil.AC), Hibyte(Abil.AC) + Hibyte(BonusAbil.AC));
   Abil.MAC := MakeWord(Lobyte(Abil.MAC) + Lobyte(BonusAbil.MAC), Hibyte(Abil.MAC) + Hibyte(BonusAbil.MAC));

   if Abil.HP > Abil.MaxHP then Abil.HP := Abil.MaxHP;
   if Abil.MP > Abil.MaxMP then Abil.MP := Abil.MaxMP;

end;

{$ELSE}

{ RecalcLevelAbilitys - 重算等级能力值(旧版)
  功能: 根据等级计算基础能力值
  实现原理:
    1. 根据职业计算不同的能力值
    2. 战士: 高HP、高负重、高攻击
    3. 法师: 高MP、高魔法
    4. 道士: 平衡型 }
procedure TCreature.RecalcLevelAbilitys;
var
   n: integer;  // 临时变量
begin
   // 根据职业计算能力值
   case Job of
      0: // 战士
         begin
            Abil.MaxHP := 14 + Round((Abil.Level / 4 + 4.5 + (Abil.Level / 20)) * Abil.Level);
            Abil.MaxMP := 11 + Round(Abil.Level * 3.5);
            Abil.MaxWeight := 50 + Round((Abil.Level / 3) * Abil.Level);
            Abil.MaxWearWeight := 15 + Round((Abil.Level / 20) * Abil.Level);
            // 2003/02/11 最大重量限制255
            if( (12 + Round((Abil.Level / 13) * Abil.Level)) > 255 ) then Abil.MaxHandWeight := 255
            else
               Abil.MaxHandWeight := 12 + Round((Abil.Level / 13) * Abil.Level);
            Abil.DC := MakeWord(_MAX(Abil.Level div 5 - 1, 1), _MAX(1, Abil.Level div 5));
            Abil.SC := 0;
            Abil.MC := 0;
            Abil.AC := MakeWord(0, Abil.Level div 7);
            Abil.MAC := 0;
         end;
      1: // 法师
         begin
            Abil.MaxHP := 14 + Round((Abil.Level / 15 + 1.8) * Abil.Level);
            Abil.MaxMP := 13 + Round((Abil.Level / 5 + 2)*2.2 * Abil.Level);
            Abil.MaxWeight := 50 + Round((Abil.Level / 5) * Abil.Level);
            Abil.MaxWearWeight := 15 + Round((Abil.Level / 100) * Abil.Level);
            Abil.MaxHandWeight := 12 + Round((Abil.Level / 90) * Abil.Level);
            n := Abil.Level div 7;
            Abil.DC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.MC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.SC := 0;
            Abil.AC := 0;
            Abil.MAC := 0;
         end;
      2: // 道士
         begin
            Abil.MaxHP := 14 + Round((Abil.Level / 6 + 2.5) * Abil.Level);
            Abil.MaxMP := 13 + Round((Abil.Level / 8)*2.2 * Abil.Level);
            Abil.MaxWeight := 50 + Round((Abil.Level / 4) * Abil.Level);
            Abil.MaxWearWeight := 15 + Round((Abil.Level / 50) * Abil.Level);
            Abil.MaxHandWeight := 12 + Round((Abil.Level / 42) * Abil.Level);
            n := Abil.Level div 7;
            Abil.DC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.MC := 0;
            Abil.SC := MakeWord(_MAX(n-1, 0), _MAX(1, n));
            Abil.AC := 0;
            n := Round(Abil.Level / 6);
            Abil.MAC := MakeWord(n div 2, n+1);
         end;
   end;

   // 确保HP/MP不超过最大值
   if Abil.HP > Abil.MaxHP then Abil.HP := Abil.MaxHP;
   if Abil.MP > Abil.MaxMP then Abil.MP := Abil.MaxMP;

end;

{$ENDIF}

{ RecalcHitSpeed - 重算命中和速度
  功能: 重新计算命中率和闪避率
  实现原理:
    1. 从魔法列表中查找基础剑法
    2. 根据技能等级计算命中率
    3. 道士基础敏捷较高 }
procedure TCreature.RecalcHitSpeed;
var
   i: integer;        // 循环变量
   pum: PTUserMagic;  // 用户魔法指针
   fin: Boolean;      // 完成标志
begin
   fin := FALSE;
   AccuracyPoint := DEFHIT + BonusAbil.Hit;
   HitPowerPlus := 0;
   HitDouble := 0;
   // 道士基础敏捷较高
   case Job of
      2: SpeedPoint := DEFSPEED + BonusAbil.Speed + 3;
      else SpeedPoint := DEFSPEED + BonusAbil.Speed;
   end;
   // 初始化技能指针
   PSwordSkill := nil;
   PPowerHitSkill := nil;
   PLongHitSkill := nil;
   PWideHitSkill := nil;
   PFireHitSkill := nil;
   PCrossHitSkill := nil;
   // 遍历魔法列表
   for i:=0 to MagicList.Count-1 do begin
      pum := PTUserMagic (MagicList[i]);
      case pum.MagicId of
         3: // 基础剑法(战士基础)
         begin
            PSwordSkill := pum;
            if pum.Level > 0 then
               AccuracyPoint := AccuracyPoint + Round(9 / 3 * pum.Level);
         end;

         7: // 攻杀剑法(战士攻击剑法)
         begin
            PPowerHitSkill := pum;
            if pum.Level > 0 then
               AccuracyPoint := AccuracyPoint + Round(3 / 3 * pum.Level);
            HitPowerPlus := 5 + pum.Level;  // 力量加成 5-8
            AttackSkillCount := 7 - PPowerHitSkill.Level;
            AttackSkillPointCount := Random(AttackSkillCount);
         end;

         12: // 刺杀剑法
         begin
            PLongHitSkill := pum;
         end;

         25: // 半月弯刀
         begin
            PWideHitSkill := pum;
         end;

         26: // 烈火剑法
         begin
            PFireHitSkill := pum;
            HitDouble := 4 + pum.Level * 4;  // +40% ~ +160%
         end;
         // 2003/03/15 新技能
         34: // 狂风斩
         begin
            HitPowerPlus := 5 + pum.Level;  // 力量加成 5-8
            PCrossHitSkill := pum;
         end;

         4: // 道士基础剑法
         begin
            PSwordSkill := pum;
            if pum.Level > 0 then
               AccuracyPoint := AccuracyPoint + Round(8 / 3 * pum.Level);
         end;
      end;
   end;
end;

{ AddMagicWithItem - 通过物品添加魔法
  功能: 穿戴物品时获得魔法
  参数:
    magic - 魔法类型(AM_FIREBALL/AM_HEALING) }
procedure TCreature.AddMagicWithItem (magic: integer);
var
   pdm: PTDefMagic;   // 魔法定义指针
   pum: PTUserMagic;  // 用户魔法指针
   hum: TUserHuman;   // 玩家对象
begin
   pdm := nil;
   // 火球术
   if magic = AM_FIREBALL then begin
      pdm := UserEngine.GetDefMagic ('火球术');
   end;
   // 治疗术
   if magic = AM_HEALING then begin
      pdm := UserEngine.GetDefMagic ('治疗术');
   end;
   if pdm <> nil then begin
      // 检查是否已学会
      if not IsMyMagic (pdm.MagicId) then begin
         new (pum);
         pum.pDef := pdm;
         pum.MagicId := pdm.MagicId;
         pum.Key := #0;
         pum.Level := 1;
         pum.CurTrain := 0;
         MagicList.Add (pum);  // 添加魔法
         if RaceServer = RC_USERHUMAN then begin
            hum := TUserHuman (self);
            hum.SendAddMagic (pum);  // 通知客户端
         end;
      end;
   end;
end;

{ DelMagicWithItem - 通过物品删除魔法
  功能: 卸下物品时删除魔法
  参数:
    magic - 魔法类型 }
procedure TCreature.DelMagicWithItem (magic: integer);
   // 根据名称删除魔法
   procedure DelMagicByName (mname: string);
   var
      i: integer;
      hum: TUserHuman;
   begin
      for i:=MagicList.Count-1 downto 0 do begin
         if PTUserMagic(MagicList[i]).pDef.MagicName = mname then begin
            hum := TUserHuman (self);
            hum.SendDelMagic (PTUserMagic(MagicList[i]));
            Dispose (PTUserMagic(MagicList[i]));
            MagicList.Delete (i);
            break;
         end;
      end;
   end;
begin
   if RaceServer <> RC_USERHUMAN then exit;
   // 非法师删除火球术
   if magic = AM_FIREBALL then begin
      if (Job <> 1) then begin
         DelMagicByName ('火球术');
      end;
   end;
   // 非道士删除治疗术
   if magic = AM_HEALING then begin
      if (Job <> 2) then begin
         DelMagicByName ('治疗术');
      end;
   end;
end;

{ ItemDamageRevivalRing - 复活戒指耐久损耗
  功能: 复活戒指使用时耐久损耗
  实现原理:
    1. 每次使用损耗1000点耐久
    2. 耐久为0时戒指消失 }
procedure TCreature.ItemDamageRevivalRing;
var
   i, idura, olddura: integer;  // 循环、耐久、旧耐久
   pstd: PTStdItem;             // 标准物品指针
   hum: TUserHuman;             // 玩家对象
begin
   // 2003/03/15 物品栏扩展 8->12
   for i:=0 to 12 do begin
      if UseItems[i].Index > 0 then begin
         pstd := UserEngine.GetStdItem (UseItems[i].Index);
         if pstd <> nil then begin
            // 检查是否为复活戒指
            if (i = U_RINGR) or (i = U_RINGL) then begin
               if pstd.Shape = RING_REVIVAL_ITEM then begin
                  idura := UseItems[i].Dura;
                  olddura := Round (idura / 1000);
                  idura := idura - 1000;  // 每次损耗1000
                  if idura <= 0 then begin
                     idura := 0;
                     UseItems[i].Dura := idura;
                     // 耐久为0时消失
                     if RaceServer = RC_USERHUMAN then begin
                        hum := TUserHuman(self);
                        hum.SendDelItem (UseItems[i]);
                     end;
                     UseItems[i].Index := 0;
                     RecalcAbilitys;
                  end else
                     UseItems[i].Dura := idura;
                  // 耐久变化时通知客户端
                  if olddura <> Round(idura / 1000) then 
                     SendMsg (self, RM_DURACHANGE, i, idura, UseItems[i].DuraMax, 0, '');
                  break;
               end;
            end;
         end;
      end;
   end;
end;

{ RecalcAbilitys - 重算能力值
  功能: 根据装备重新计算能力值
  实现原理:
    1. 计算装备属性加成
    2. 检查套装效果
    3. 计算特殊属性(毒抗、恢复等) }
procedure TCreature.RecalcAbilitys;
var
   i, oldlight, n, m: integer;  // 循环、旧亮度、临时变量
   cghi: array[0..3] of Boolean;  // 套装标志
   pstd: PTStdItem;               // 标准物品指针
   temp: TAbility;                // 临时能力值
   oldhmode: Boolean;             // 旧马模式
   // 套装检测标志
   mh_ring, mh_bracelet, mh_necklace: Boolean;
   sh_ring, sh_bracelet, sh_necklace: Boolean;
   // 2003/01/15 新增套装
   hp_ring, hp_bracelet : Boolean;
   mp_ring, mp_bracelet : Boolean;
   hpmp_ring, hpmp_bracelet : Boolean;
   // 2003/02/11 新增套装
   hpp_necklace, hpp_bracelet, hpp_ring : Boolean;
   cho_weapon, cho_necklace, cho_ring, cho_helmet, cho_bracelet : Boolean;
   // 2003/03/04 新增套装
   pset_necklace, pset_bracelet, pset_ring : Boolean;
   hset_necklace, hset_bracelet, hset_ring : Boolean;
   yset_necklace, yset_bracelet, yset_ring : Boolean;
   // 2003/03/15 爱情套装
   lv_ring, lv_bracelet, lv_necklace, lv_helmet : Boolean;
begin
   // 初始化附加能力
   FillChar (AddAbil, sizeof(TAddAbility), 0);
   temp := WAbil;
   WAbil := Abil;
   WAbil.HP := temp.HP;
   WAbil.MP := temp.MP;
   WAbil.Weight := 0;
   WAbil.WearWeight := 0;
   WAbil.HandWeight := 0;
   // 初始化特殊属性
   AntiPoison := 0;
   PoisonRecover := 0;
   HealthRecover := 0;
   SpellRecover := 0;
   AntiMagic := 1;   // 基础魔抗 10%
   Luck := 0;
   HitSpeed := 0;
   oldhmode := BoHumHideMode;
   BoHumHideMode := FALSE;

   //특수한 능력
   BoAbilSpaceMove := FALSE;
   BoAbilMakeStone := FALSE;
   BoAbilRevival := FALSE;
   BoAddMagicFireball := FALSE;
   BoAddMagicHealing := FALSE;
   BoAbilAngerEnergy := FALSE;
   BoMagicShield := FALSE;
   BoAbilSuperStrength := FALSE;
   BoFastTraining := FALSE;
   BoAbilSearch := FALSE;
   // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
   BoOldVersionUser_Italy := FALSE;

   ManaToHealthPoint := 0; //마력 -> 체력
   mh_ring := FALSE;
   mh_bracelet := FALSE;
   mh_necklace := FALSE;

   SuckupEnemyHealthRate := 0; //체력 흡수
   SuckupEnemyHealth := 0;
   sh_ring := FALSE;
   sh_bracelet := FALSE;
   sh_necklace := FALSE;
   // 2003/01/15 세트 아이템 추가...세륜셋, 녹취셋, 도부셋
   hp_ring        := FALSE;
   hp_bracelet    := FALSE;
   mp_ring        := FALSE;
   mp_bracelet    := FALSE;
   hpmp_ring      := FALSE;
   hpmp_bracelet  := FALSE;
   // 2003/02/11 세트 아이템 추가...오현셋, 초혼셋
   hpp_necklace   := FALSE;
   hpp_bracelet   := FALSE;
   hpp_ring       := FALSE;
   cho_weapon     := FALSE;
   cho_necklace   := FALSE;
   cho_ring       := FALSE;
   cho_helmet     := FALSE;
   cho_bracelet   := FALSE;
   // 2003/02/11 초혼 풀세트 착용여부 플레그로 용도 변경하여 사용
   BoOldVersionUser_Italy := FALSE;
   // 2003/03/04 세트 아이템 추가...파쇄셋, 환마석셋, 영령옥셋
   pset_necklace  := FALSE;
   pset_bracelet  := FALSE;
   pset_ring      := FALSE;
   hset_necklace  := FALSE;
   hset_bracelet  := FALSE;
   hset_ring      := FALSE;
   yset_necklace  := FALSE;
   yset_bracelet  := FALSE;
   yset_ring      := FALSE;

   // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
   lv_ring     := FALSE;
   lv_bracelet := FALSE;
   lv_necklace := FALSE;
   lv_helmet   := FALSE;

   BoCGHIEnable := FALSE;  //천지합일
   cghi[0] := FALSE;
   cghi[1] := FALSE;
   cghi[2] := FALSE;
   cghi[3] := FALSE;


   // 2003/03/04 사람의 경우만 아이템 착용여부 검사토록 변경
   if (RaceServer = RC_USERHUMAN) or (Master <> nil) then begin
      // 2003/03/15 아이템 인벤토리 확장
      for i:=0 to 12 do begin // 8 -> 12
         if (UseItems[i].Index > 0) and (UseItems[i].Dura > 0) then begin
            ApplyItemParameters (UseItems[i], AddAbil);
            // 2003/03/15 아이템 인벤토리 확장
            ApplyItemParametersEx (UseItems[i], WAbil);
            pstd := UserEngine.GetStdItem (UseItems[i].Index);
            if pstd <> nil then begin
               if (i = U_WEAPON) or (i = U_RIGHTHAND) then begin
                  WAbil.HandWeight := WAbil.HandWeight + pstd.Weight;  //손에 들고 있는 무게
               end else begin
                  WAbil.WearWeight := WAbil.WearWeight + pstd.Weight;  //입고 있거나 착용한 무게.
               end;
               WAbil.Weight := WAbil.Weight + pstd.Weight; //전체의 무게
               //무기인경우 강도
               if i = U_WEAPON then begin
                  if pstd.SpecialPwr in [1..10] then
                     AddAbil.WeaponStrong := pstd.SpecialPwr;  //무기의 강도, 강도가 높으면 잘 안뽀개짐
                  if (pstd.SpecialPwr <= -1) and (pstd.SpecialPwr >= -50) then
                     AddAbil.UndeadPower := AddAbil.UndeadPower + (-pstd.SpecialPwr);  //언데드 공격 효과 상승
                  if (pstd.SpecialPwr <= -51) and (pstd.SpecialPwr >= -100) then
                     AddAbil.UndeadPower := AddAbil.UndeadPower + (pstd.SpecialPwr + 50);  //언데드 공격 효과 감소
                  if pstd.Shape = CCHO_WEAPON then
                     cho_weapon     := TRUE;
               end;
               //목걸이
               if i = U_NECKLACE then begin
                  if pstd.Shape = NECTLACE_FASTTRAINING_ITEM then begin  //수련의목걸이
                     BoFastTraining := TRUE;
                  end;
                  if pstd.Shape = NECTLACE_SEARCH_ITEM then begin
                     BoAbilSearch := TRUE;
                  end;
                  if pstd.Shape = NECKLACE_GI_ITEM then begin  //천지합일 (지)
                     cghi[1] := TRUE;
                  end;
                  if pstd.Shape = NECKLACE_OF_MANATOHEALTH then begin //마력 -> 체력
                     mh_necklace := TRUE;
                     ManaToHealthPoint := ManaToHealthPoint + pstd.AniCount;
                  end;
                  if pstd.Shape = NECKLACE_OF_SUCKHEALTH then begin  //상대 체력 흡수
                     sh_necklace := TRUE;
                     SuckupEnemyHealthRate := SuckupEnemyHealthRate + pstd.AniCount;
                  end;
                  if pstd.Shape = NECKLACE_OF_HPPUP then begin  //HP PERCENT UP
                     hpp_necklace   := TRUE;
                  end;
                  if pstd.Shape = CCHO_NECKLACE then cho_necklace   := TRUE;
                  // 2003/03/04 세트 아이템 추가...파쇄셋, 환마석셋, 영령옥셋
                  if pstd.Shape = PSET_NECKLACE_SHAPE then pset_necklace := TRUE;
                  if pstd.Shape = HSET_NECKLACE_SHAPE then hset_necklace := TRUE;
                  if pstd.Shape = YSET_NECKLACE_SHAPE then yset_necklace := TRUE;
               // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
               if pstd.Shape = NECKLACE_OF_LOVE then lv_necklace := TRUE;
               end;
               //반지
               if (i = U_RINGR) or (i = U_RINGL) then begin
                  if pstd.Shape = RING_TRANSPARENT_ITEM then begin
                     StatusArr[STATE_TRANSPARENT] := 60000;  //타임아웃 없음..
                     BoHumHideMode := TRUE;   //투명모드
                  end;
                  if pstd.Shape = RING_SPACEMOVE_ITEM then begin
                     BoAbilSpaceMove := TRUE;
                  end;
                  if pstd.Shape = RING_MAKESTONE_ITEM then begin
                     BoAbilMakeStone := TRUE;
                  end;
                  if pstd.Shape = RING_REVIVAL_ITEM then begin
                     BoAbilRevival := TRUE;
                  end;
                  if pstd.Shape = RING_FIREBALL_ITEM then begin
                     BoAddMagicFireBall := TRUE;
                  end;
                  if pstd.Shape = RING_HEALING_ITEM then begin
                     BoAddMagicHealing := TRUE;
                  end;
                  if pstd.Shape = RING_ANGERENERGY_ITEM then begin
                     BoAbilAngerEnergy := TRUE;
                  end;
                  if pstd.Shape = RING_MAGICSHIELD_ITEM then begin
                     BoMagicShield := TRUE;
                  end;
                  if pstd.Shape = RING_SUPERSTRENGTH_ITEM then begin
                     BoAbilSuperStrength := TRUE;
                  end;
                  if pstd.Shape = RING_CHUN_ITEM then begin  //천지합일 (천)
                     cghi[0] := TRUE;
                  end;
                  if pstd.Shape = RING_OF_MANATOHEALTH then begin  //마력 -> 체력
                     mh_ring := TRUE;
                     ManaToHealthPoint := ManaToHealthPoint + pstd.AniCount;
                  end;
                  if pstd.Shape = RING_OF_SUCKHEALTH then begin  //상대 체력 흡수
                     sh_ring := TRUE;
                     SuckupEnemyHealthRate := SuckupEnemyHealthRate + pstd.AniCount;
                  end;
                  // 2003/01/15 세트 아이템 추가...세륜셋, 녹취셋, 도부셋
                  if pstd.Shape = RING_OF_HPUP then begin  //HP증가
                     hp_ring := TRUE;
                  end;
                  if pstd.Shape = RING_OF_MPUP then begin  //MP증가
                     mp_ring := TRUE;
                  end;
                  if pstd.Shape = RING_OF_HPMPUP then begin  //HP/MP 증가
                     hpmp_ring := TRUE;
                  end;
                  if pstd.Shape = RING_OH_HPPUP then begin  //HP PERCENT 증가
                     hpp_ring := TRUE;
                  end;
                  if pstd.Shape = CCHO_RING then cho_ring   := TRUE;
                  // 2003/03/04 세트 아이템 추가...파쇄셋, 환마석셋, 영령옥셋
                  if pstd.Shape = PSET_RING_SHAPE then pset_ring := TRUE;
                  if pstd.Shape = HSET_RING_SHAPE then hset_ring := TRUE;
                  if pstd.Shape = YSET_RING_SHAPE then yset_ring := TRUE;
               // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
               if pstd.Shape = RING_OF_LOVE then lv_ring := TRUE;
               end;
               //팔찌
               if (i = U_ARMRINGL) or (i = U_ARMRINGR) then begin
                  if pstd.Shape = ARMRING_HAP_ITEM then begin  //천지합일 (합)
                     cghi[2] := TRUE;
                  end;
                  if pstd.Shape = BRACELET_OF_MANATOHEALTH then begin  //마력 -> 체력
                     mh_bracelet := TRUE;
                     ManaToHealthPoint := ManaToHealthPoint + pstd.AniCount;
                  end;
                  if pstd.Shape = BRACELET_OF_SUCKHEALTH then begin  //상대 체력 흡수
                     sh_bracelet := TRUE;
                     SuckupEnemyHealthRate := SuckupEnemyHealthRate + pstd.AniCount;
                  end;
                  // 2003/01/15 세트 아이템 추가...세륜셋, 녹취셋, 도부셋
                  if pstd.Shape = BRACELET_OF_HPUP then begin  //HP증가
                     hp_bracelet := TRUE;
                  end;
                  if pstd.Shape = BRACELET_OF_MPUP then begin  //MP증가
                     mp_bracelet := TRUE;
                  end;
                  if pstd.Shape = BRACELET_OF_HPMPUP then begin  //HP/MP증가
                     hpmp_bracelet := TRUE;
                  end;
                  if pstd.Shape = BRACELET_OF_HPPUP then begin  //HP PERCENT 증가
                     hpp_bracelet := TRUE;
                  end;
                  if pstd.Shape = CCHO_BRACELET then cho_bracelet   := TRUE;
                  // 2003/03/04 세트 아이템 추가...파쇄셋, 환마석셋, 영령옥셋
                  if pstd.Shape = PSET_BRACELET_SHAPE then pset_bracelet := TRUE;
                  if pstd.Shape = HSET_BRACELET_SHAPE then hset_bracelet := TRUE;
                  if pstd.Shape = YSET_BRACELET_SHAPE then yset_bracelet := TRUE;
               // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
               if pstd.Shape = BRACELET_OF_LOVE then lv_bracelet := TRUE;
               end;
               //투구
               if (i = U_HELMET) then begin
                  if pstd.Shape = HELMET_IL_ITEM then begin
                     cghi[3] := TRUE;
                  end;
                  if pstd.Shape = CCHO_HELMET then cho_helmet   := TRUE;
               // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
               if pstd.Shape = HELMET_OF_LOVE then lv_helmet := TRUE;
               end;
            end;
         end;
      end;

      //-----세트 아이템
      //천지합일 검사
      if cghi[0] and cghi[1] and cghi[2] and cghi[3] then begin  //천지합일을 다 찼음
         BoCGHIEnable := TRUE;
      end;

      //마력 -> 체력으로 세트 (적난 세트)
      if mh_necklace and mh_bracelet and mh_ring then begin
         ManaToHealthPoint := ManaToHealthPoint + 50;   //보너스 50
      end;

      //상대 체력흡수 세트 (밀화 세트)
      if sh_necklace and sh_bracelet and sh_ring then begin
         AddAbil.HIT := AddAbil.HIT + 2;  //보너스로 정확이 2 증가
      end;

      // 2003/01/15 세트 아이템 추가...세륜셋, 녹취셋, 도부셋
      if hp_bracelet and hp_ring then begin
         AddAbil.HP := AddAbil.HP + 50;  //보너스로 HP 50 증가
      end;
      if mp_bracelet and mp_ring then begin
         AddAbil.MP := AddAbil.MP + 50;  //보너스로 MP 50 증가
      end;
      if hpmp_bracelet and hpmp_ring then begin
         AddAbil.HP := AddAbil.HP + 30;  //보너스로 HP 30 증가
         AddAbil.MP := AddAbil.MP + 30;  //보너스로 MP 30 증가
      end;
      // 2003/02/11 세트 아이템 추가...오현셋, 초혼셋
      if hpp_necklace and hpp_bracelet and hpp_ring then begin
         AddAbil.HP := AddAbil.HP + ((WAbil.MaxHP * 30) div 100);  // 보너스로 HP의 30% 증가
         AddAbil.AC := AddAbil.AC + MAKEWORD(2,2);
      end;
      if cho_weapon and cho_necklace and cho_ring and cho_helmet and cho_bracelet then begin
         AddAbil.HitSpeed := -2;
         AddAbil.DC := AddAbil.DC + MakeWord(2,5); // 보너스로 최소파괴 +2, 최대파괴 +5
         BoOldVersionUser_Italy := TRUE;           // 풀셋 착용 세트
      end;
      // 2003/03/04 세트 아이템 추가...파쇄셋, 환마석셋, 영령옥셋
      if pset_bracelet and pset_ring then begin          // 보너스로 공속 +1
         AddAbil.HitSpeed := AddAbil.HitSpeed + 1;
         if pset_necklace then begin                     // 보너스로 파괴1-3
            AddAbil.DC := AddAbil.DC + MakeWord(1,3);
         end;
      end;
      {
      // 무게 초기화
      case Job of
         0: //전사
            begin
               Abil.MaxWeight := 50 + Round((Abil.Level / 3) * Abil.Level);
               Abil.MaxWearWeight := 15 + Round((Abil.Level / 20) * Abil.Level);
            end;
         1: //술사인경우
            begin
               Abil.MaxWeight := 50 + Round((Abil.Level / 5{) * Abil.Level);
               Abil.MaxWearWeight := 15 + Round((Abil.Level / 100) * Abil.Level);
            end;
         2: //도사인 경우
            begin
               Abil.MaxWeight := 50 + Round((Abil.Level / 4) * Abil.Level);
               Abil.MaxWearWeight := 15 + Round((Abil.Level / 50) * Abil.Level);
            end;
      end;
      }
      if hset_bracelet and hset_ring then begin          // 보너스로 착용+5, 가방+20
         WAbil.MaxWeight     := WAbil.MaxWeight     + 20;
         WAbil.MaxWearWeight := WAbil.MaxWearWeight + 5;
         if hset_necklace then begin                     // 보너스로 마법1-2
            AddAbil.MC := AddAbil.MC + MakeWord(1,2);
         end;
      end;
      if yset_bracelet and yset_ring then begin          // 보너스로 신성+3
         AddAbil.UndeadPower := AddAbil.UndeadPower + 3;
         if yset_necklace then begin                     // 보너스로 도력1-2
            AddAbil.SC := AddAbil.SC + MakeWord(1,2);
         end;
      end;
   // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
   if lv_ring and lv_bracelet and lv_necklace and lv_helmet then begin
      BoOldVersionUser_Italy := TRUE;
   end;

      //-----세트 아이템 끝

      WAbil.Weight := {WAbil.Weight +} CalcBagWeight;
   // 2003/03/04 사람의 경우만 아이템 착용여부 검사토록 변경
   end;
   if (BoFixedHideMode) and (StatusArr[STATE_TRANSPARENT] > 0) then  //은신술
      BoHumHideMode := TRUE;

   if BoHumHideMode then begin
      if not oldhmode then begin
         CharStatus := GetCharStatus;
         CharStatusChanged;
      end;
   end else begin
      if oldhmode then begin
         StatusArr[STATE_TRANSPARENT] := 0;
         CharStatus := GetCharStatus;
         CharStatusChanged;
      end;
   end;

   //AccuracyPoint, SpeedPoint 저설정, 무술로 올라간다.
   RecalcHitSpeed;

   //초,횟불
   oldlight := Light;
   Light := GetMyLight;
   if oldlight <> light then
      SendRefMsg (RM_CHANGELIGHT, 0, 0, 0, 0, '');

   SpeedPoint := SpeedPoint + AddAbil.SPEED;
   AccuracyPoint := AccuracyPoint + AddAbil.HIT;
   AntiPoison := AntiPoison + AddAbil.AntiPoison;
   PoisonRecover := PoisonRecover + AddAbil.PoisonRecover;
   HealthRecover := HealthRecover + AddAbil.HealthRecover;
   SpellRecover := SpellRecover + AddAbil.SpellRecover;
   AntiMagic := AntiMagic + AddAbil.AntiMagic;
   Luck := Luck + AddAbil.Luck;
   Luck := Luck - AddAbil.UnLuck;
   HitSpeed := AddAbil.HitSpeed;

   WAbil.MaxHP := Abil.MaxHP + AddAbil.HP;
   WAbil.MaxMP := Abil.MaxMP + AddAbil.MP;
   if(RaceServer = RC_USERHUMAN) and (WAbil.HP > WAbil.MaxHP) and ((not mh_necklace) and (not mh_bracelet) and (not mh_ring)) then
      WAbil.HP := WAbil.MaxHP;
   if(RaceServer = RC_USERHUMAN) and (WAbil.MP > WAbil.MaxMP) then WAbil.MP := WAbil.MaxMP;

   WAbil.AC := MakeWord (Lobyte(AddAbil.AC) + Lobyte(Abil.AC), Hibyte(AddAbil.AC) + Hibyte(Abil.AC));
   WAbil.MAC := MakeWord (Lobyte(AddAbil.MAC) + Lobyte(Abil.MAC), Hibyte(AddAbil.MAC) + Hibyte(Abil.MAC));
   WAbil.DC := MakeWord (Lobyte(AddAbil.DC) + Lobyte(Abil.DC), Hibyte(AddAbil.DC) + Hibyte(Abil.DC));
   WAbil.MC := MakeWord (Lobyte(AddAbil.MC) + Lobyte(Abil.MC), Hibyte(AddAbil.MC) + Hibyte(Abil.MC));
   WAbil.SC := MakeWord (Lobyte(AddAbil.SC) + Lobyte(Abil.SC), Hibyte(AddAbil.SC) + Hibyte(Abil.SC));

   //마법으로 걸린 설정
   if StatusArr[STATE_DEFENCEUP] > 0 then begin //방어력 상승
      WAbil.AC := MakeWord (
                     Lobyte(WAbil.AC), // + 2 + (Abil.Level div 8),
                     Hibyte(WAbil.AC) + 2 + (Abil.Level div 7)
                  );
   end;
   if StatusArr[STATE_MAGDEFENCEUP] > 0 then begin //마항력 상승
      WAbil.MAC := MakeWord (
                     Lobyte(WAbil.MAC), // + 2 + (Abil.Level div 8),
                     Hibyte(WAbil.MAC) + 2 + (Abil.Level div 7)
                  );
   end;

   //물약으로 증사한 능력 설정
   if ExtraAbil[EABIL_DCUP] > 0 then begin
      WAbil.DC := MakeWord(
                     Lobyte(WAbil.DC),
                     Hibyte(WAbil.DC) + ExtraAbil[EABIL_DCUP]
                  );
   end;
   if ExtraAbil[EABIL_MCUP] > 0 then begin
      WAbil.MC := MakeWord(
                     Lobyte(WAbil.MC),
                     Hibyte(WAbil.MC) + ExtraAbil[EABIL_MCUP]
                  );
   end;
   if ExtraAbil[EABIL_SCUP] > 0 then begin
      WAbil.SC := MakeWord(
                     Lobyte(WAbil.SC),
                     Hibyte(WAbil.SC) + ExtraAbil[EABIL_SCUP]
                  );
   end;
   if ExtraAbil[EABIL_HITSPEEDUP] > 0 then begin
      HitSpeed := HitSpeed + ExtraAbil[EABIL_HITSPEEDUP];
   end;
   if ExtraAbil[EABIL_HPUP] > 0 then begin
      WAbil.MaxHP := WAbil.MaxHP + ExtraAbil[EABIL_HPUP];
   end;
   if ExtraAbil[EABIL_MPUP] > 0 then begin
      WAbil.MaxMP := WAbil.MaxMP + ExtraAbil[EABIL_MPUP];
   end;



   //반지의 특수한 능력,... 화염의반지(화염장), 회복의반지(회복술)
   if BoAddMagicFireBall then begin
      AddMagicWithItem (AM_FIREBALL);
   end else begin
      DelMagicWithItem (AM_FIREBALL);
   end;

   if BoAddMagicHealing then begin
      AddMagicWithItem (AM_HEALING);
   end else begin
      DelMagicWithItem (AM_HEALING);
   end;

   if BoAbilSuperStrength then begin
      WAbil.MaxWeight := WAbil.MaxWeight * 2;
      WAbil.MaxWearWeight := WAbil.MaxWearWeight * 2;
      if(WAbil.MaxHandWeight * 2 > 255) then WAbil.MaxHandWeight := 255
      else                                   WAbil.MaxHandWeight := WAbil.MaxHandWeight * 2;
   end;

   //적난 세트 마력->체력
   if ManaToHealthPoint > 0 then begin
      if ManaToHealthPoint >= WAbil.MaxMp then ManaToHealthPoint := WAbil.MaxMp-1; //MaxMP는 0이 아님
      WAbil.MaxMP := WAbil.MaxMP - ManaToHealthPoint;
      WAbil.MaxHP := WAbil.MaxHP + ManaToHealthPoint;
      if(RaceServer = RC_USERHUMAN) and (WAbil.HP > WAbil.MaxHP) then WAbil.HP := WAbil.MaxHP;
   end;

   if RaceServer = RC_USERHUMAN then
      UpdateMsg (self, RM_CHARSTATUSCHANGED, HitSpeed{wparam}, CharStatus, 0, 0, '');
   if RaceServer >= RC_ANIMAL then begin
      //if Master <> nil then
      ApplySlaveLevelAbilitys;
   end;

end;

{ ApplyItemParameters - 应用物品属性
  功能: 将物品属性加到附加能力中
  参数:
    uitem - 用户物品
    aabil - 附加能力(输出)
  实现原理: 根据物品类型应用不同属性 }
procedure TCreature.ApplyItemParameters (uitem: TUserItem; var aabil: TAddAbility);
var
   ps: PTStdItem;  // 标准物品指针
   std: TStdItem;  // 标准物品
begin
   ps := UserEngine.GetStdItem (uitem.Index);
   if ps <> nil then begin
      std := ps^;
      ItemMan.GetUpgradeStdItem (uitem, std);  // 获取升级后的属性
      case ps.StdMode of
         5,6: // 武器类 (AC:精确 MAC:敏捷)
         begin
            aabil.HIT := aabil.HIT + Hibyte(std.AC);
            if Hibyte(std.MAC) > 10 then
               aabil.HitSpeed := aabil.HitSpeed + (Hibyte(std.MAC) - 10)  // 攻击速度(+)
            else
               aabil.HitSpeed := aabil.HitSpeed - Hibyte(std.MAC);  // 攻击速度(-)
            aabil.Luck := aabil.Luck + Lobyte(std.AC);      // 幸运
            aabil.UnLuck := aabil.UnLuck + Lobyte(std.MAC); // 诅咒
         end;
         19: // 项链
         begin
            aabil.AntiMagic := aabil.AntiMagic + Hibyte(std.AC);
            aabil.UnLuck := aabil.UnLuck + Lobyte(std.MAC);
            aabil.Luck := aabil.Luck + Hibyte(std.MAC);
         end;
         20, // 项链
         24: // 手镐
         begin
            aabil.HIT := aabil.HIT + Hibyte(std.AC);
            aabil.SPEED := aabil.SPEED + Hibyte(std.MAC);
         end;
         21: // 项链
         begin
            aabil.HealthRecover := aabil.HealthRecover + Hibyte(std.AC);
            aabil.SpellRecover := aabil.SpellRecover + Hibyte(std.MAC);
            aabil.HitSpeed := aabil.HitSpeed + Lobyte(std.AC);
            aabil.HitSpeed := aabil.HitSpeed - Lobyte(std.MAC);
         end;
         23: // 戒指
         begin
            aabil.AntiPoison := aabil.AntiPoison + Hibyte(std.AC);
            aabil.PoisonRecover := aabil.PoisonRecover + Hibyte(std.MAC);
            aabil.HitSpeed := aabil.HitSpeed + Lobyte(std.AC);
            aabil.HitSpeed := aabil.HitSpeed - Lobyte(std.MAC);
         end;
         // 2003/03/15 物品栏扩展
         53: // 守护石
         begin
            aabil.HP := aabil.HP + std.HpAdd;
            aabil.MP := aabil.MP + std.MpAdd;
         end;
         else begin
            aabil.AC := MakeWord (Lobyte(aabil.AC) + Lobyte(std.AC), Hibyte(aabil.AC) + Hibyte(std.AC));
            aabil.MAC := MakeWord (Lobyte(aabil.MAC) + Lobyte(std.MAC), Hibyte(aabil.MAC) + Hibyte(std.MAC));
         end;
      end;
      // 通用属性
      aabil.DC := MakeWord (Lobyte(aabil.DC) + Lobyte(std.DC), Hibyte(aabil.DC) + Hibyte(std.DC));
      aabil.MC := MakeWord (Lobyte(aabil.MC) + Lobyte(std.MC), Hibyte(aabil.MC) + Hibyte(std.MC));
      aabil.SC := MakeWord (Lobyte(aabil.SC) + Lobyte(std.SC), Hibyte(aabil.SC) + Hibyte(std.SC));
   end;
end;

{ ApplyItemParametersEx - 应用物品属性(扩展)
  功能: 将物品属性加到能力值中
  参数:
    uitem - 用户物品
    AWabil - 能力值(输出)
  实现原理: 处理鞋子等特殊物品的属性 }
procedure  TCreature.ApplyItemParametersEx (uitem: TUserItem; var AWabil: TAbility);
var
   ps: PTStdItem;  // 标准物品指针
   std: TStdItem;  // 标准物品
begin
   ps := UserEngine.GetStdItem (uitem.Index);
   if ps <> nil then begin
      std := ps^;
      ItemMan.GetUpgradeStdItem (uitem, std);  // 获取升级后的属性
      case ps.StdMode of
         52: // 鞋子类
         begin
            // 效果类型1
            if std.EffType1 > 0 then begin
               case std.EffType1 of
                  1: if(AWabil.MaxHandWeight + std.EffValue1 > 255) then AWabil.MaxHandWeight := 255
                     else AWabil.MaxHandWeight := AWabil.MaxHandWeight + std.EffValue1;
                  2: AWabil.MaxWearWeight := AWabil.MaxWearWeight + std.EffValue1;
               end;
            end;
            // 效果类型2
            if std.EffType2 > 0 then begin
               case std.EffType2 of
                  1: if(AWabil.MaxHandWeight + std.EffValue1 > 255) then AWabil.MaxHandWeight := 255
                     else AWabil.MaxHandWeight := AWabil.MaxHandWeight + std.EffValue2;
                  2: AWabil.MaxWearWeight := AWabil.MaxWearWeight + std.EffValue2;
               end;
            end;
         end;
      end;
   end;
end;

{ MakeWeaponUnlock - 武器解锁
  功能: 解锁武器
  实现原理:
    1. 减少幸运
    2. 幸运为0时增加诅咒(最多10) }
procedure  TCreature.MakeWeaponUnlock;
begin
   if UseItems[U_WEAPON].Index <> 0 then begin
      if UseItems[U_WEAPON].Desc[3] > 0 then begin
         UseItems[U_WEAPON].Desc[3] := UseItems[U_WEAPON].Desc[3] - 1; //减少幸运
         SysMsg ('你的武器发出了黑光', 0);
      end else begin
         if UseItems[U_WEAPON].Desc[4] < 10 then begin
            UseItems[U_WEAPON].Desc[4] := UseItems[U_WEAPON].Desc[4] + 1; //增加诅咒
            SysMsg ('你的武器发出了黑光', 0);
         end;
      end;
      if RaceServer = 0 then begin
         RecalcAbilitys;
         SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
         SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
      end;
   end;
end;

{ TrainSkill - 训练技能
  功能: 增加技能经验
  参数:
    pum - 用户魔法指针
    train - 训练值 }
procedure  TCreature.TrainSkill (pum: PTUserMagic; train: integer);
begin
   if BoFastTraining then train := train * 3;
   pum.CurTrain := pum.CurTrain + train;
end;

{ GetMyAbility - 获取我的能力值
  功能: 获取基础能力和附加能力的总和
  返回值: 总能力值 }
function  TCreature.GetMyAbility: TAbility;
begin
   Result := Abil;
   Result.HP := AddAbil.HP + Abil.HP;
   Result.MP := AddAbil.MP + Abil.MP;
   Result.AC := MakeWord (Lobyte(AddAbil.AC) + Lobyte(Abil.AC), Hibyte(AddAbil.AC) + Hibyte(Abil.AC));
   Result.MAC := MakeWord (Lobyte(AddAbil.MAC) + Lobyte(Abil.MAC), Hibyte(AddAbil.MAC) + Hibyte(Abil.MAC));
   Result.DC := MakeWord (Lobyte(AddAbil.DC) + Lobyte(Abil.DC), Hibyte(AddAbil.DC) + Hibyte(Abil.DC));
   Result.MC := MakeWord (Lobyte(AddAbil.MC) + Lobyte(Abil.MC), Hibyte(AddAbil.MC) + Hibyte(Abil.MC));
   Result.SC := MakeWord (Lobyte(AddAbil.SC) + Lobyte(Abil.SC), Hibyte(AddAbil.SC) + Hibyte(Abil.SC));
end;

{ GetMyLight - 获取我的亮度
  功能: 获取角色的照明亮度
  返回值: 亮度等级 }
function  TCreature.GetMyLight: integer;
begin
   if BoTaiwanEventUser then begin
      Result := 4;
   end else begin
      if (UseItems[U_RIGHTHAND].Index > 0) and (UseItems[U_RIGHTHAND].Dura > 0) then
         Result := 3 //蜡烛亮度
      else
         Result := 0;
   end;
end;

{ GetUserName - 获取用户名
  功能: 获取显示用的用户名
  返回值: 用户名字符串
  实现原理:
    1. 怪物: 显示名称+主人名
    2. 玩家: 显示名称+行会名 }
function  TCreature.GetUserName: string;
begin
   // 怪物名称
   if RaceServer <> RC_USERHUMAN then begin
      GetValidStrNoVal (UserName, Result);  // 过滤数字名称
      // 显示主人名
      if Master <> nil then begin
         if not Master.BoSuperviserMode then
            Result := Result + '(' + Master.UserName + ')';
      end;
   end else begin
      // 玩家名称
      Result := UserName;
      // 台湾活动用户
      if BoTaiwanEventUser then
         Result := Result + '(' + TaiwanEventItemName + ')';
      // 行会名称
      if MyGuild <> nil then begin
         // 占领城堡时显示城堡名
         if UserCastle.IsOurCastle(TGuild(MyGuild)) then begin
            Result := Result + '\' +
                      TGuild(MyGuild).GuildName + '(' + UserCastle.CastleName + ')';
         end else begin
            // 攻城战时显示行会名
            if UserCastle.BoCastleUnderAttack then
               if (BoInFreePKArea) or (UserCastle.IsCastleWarArea (PEnvir, CX, CY)) then
                  Result := Result + '\' +
                            TGuild(MyGuild).GuildName;
         end;
      end;
   end;
end;

{ GetHungryState - 获取饥饿状态
  功能: 获取当前饥饿等级
  返回值: 饥饿等级(0-4) }
function  TCreature.GetHungryState: integer;
begin
   Result := HungryState div 1000;
   if Result > 4 then Result := 4;
end;

{ GetQuestMark - 获取任务标记
  功能: 获取指定任务的完成状态
  参数:
    idx - 任务索引(1开始)
  返回值: 0=未完成, 1=已完成 }
function   TCreature.GetQuestMark (idx: integer): integer;
var
   dcount, mcount: integer;  // 字节位置、位位置
begin
   Result := 0;
   idx := idx - 1;
   if idx >= 0 then begin
      dcount := idx div 8;
      mcount := idx mod 8;
      if dcount in [0..MAXQUESTBYTE-1] then begin
         if QuestStates[dcount] and ($80 shr mcount) <> 0 then
            Result := 1
         else Result := 0;
      end;
   end;
end;

{ SetQuestMark - 设置任务标记
  功能: 设置指定任务的完成状态
  参数:
    idx - 任务索引(1开始)
    value - 0=未完成, 1=已完成 }
procedure  TCreature.SetQuestMark (idx, value: integer);
var
   dcount, mcount: integer;  // 字节位置、位位置
   val: byte;                // 当前值
begin
   idx := idx - 1;
   if idx >= 0 then begin
      dcount := idx div 8;
      mcount := idx mod 8;
      if dcount in [0..MAXQUESTBYTE-1] then begin
         val := QuestStates[dcount];
         if value = 0 then
            QuestStates[dcount] := val and (not ($80 shr mcount))
         else
            QuestStates[dcount] := val or ($80 shr mcount);
      end;
   end;
end;

{ GetQuestOpenIndexMark - 获取任务开启标记
  功能: 获取指定任务的开启状态
  参数:
    idx - 任务索引(1开始)
  返回值: 0=未开启, 1=已开启 }
function   TCreature.GetQuestOpenIndexMark (idx: integer): integer;
var
   dcount, mcount: integer;  // 字节位置、位位置
begin
   Result := 0;
   idx := idx - 1;
   if idx >= 0 then begin
      dcount := idx div 8;
      mcount := idx mod 8;
      if dcount in [0..MAXQUESTINDEXBYTE-1] then begin
         if QuestIndexOpenStates[dcount] and ($80 shr mcount) <> 0 then
            Result := 1
         else Result := 0;
      end;
   end;
end;

procedure  TCreature.SetQuestOpenIndexMark (idx, value: integer);
var
   dcount, mcount: integer;
   val: byte;
begin
   idx := idx - 1;
   if idx >= 0 then begin
      dcount := idx div 8;
      mcount := idx mod 8;
      if dcount in [0..MAXQUESTINDEXBYTE-1] then begin
         val := QuestIndexOpenStates[dcount];
         if value = 0 then
            QuestIndexOpenStates[dcount] := val and (not ($80 shr mcount))
         else
            QuestIndexOpenStates[dcount] := val or ($80 shr mcount);
      end;
   end;
end;

function   TCreature.GetQuestFinIndexMark (idx: integer): integer;
var
   dcount, mcount: integer;
begin
   Result := 0;
   idx := idx - 1;
   if idx >= 0 then begin
      dcount := idx div 8;
      mcount := idx mod 8;
      if dcount in [0..MAXQUESTINDEXBYTE-1] then begin
         if QuestIndexFinStates[dcount] and ($80 shr mcount) <> 0 then
            Result := 1
         else Result := 0;
      end;
   end;
end;

procedure  TCreature.SetQuestFinIndexMark (idx, value: integer);
var
   dcount, mcount: integer;
   val: byte;
begin
   idx := idx - 1;
   if idx >= 0 then begin
      dcount := idx div 8;
      mcount := idx mod 8;
      if dcount in [0..MAXQUESTINDEXBYTE-1] then begin
         val := QuestIndexFinStates[dcount];
         if value = 0 then
            QuestIndexFinStates[dcount] := val and (not ($80 shr mcount))
         else
            QuestIndexFinStates[dcount] := val or ($80 shr mcount);
      end;
   end;
end;


{ DoDamageWeapon - 武器耐久损耗
  功能: 武器攻击时耐久损耗
  参数:
    wdam - 损耗值
  实现原理: 耐久为0时武器失效 }
procedure TCreature.DoDamageWeapon (wdam: integer);
var
   olddura, idura: integer;  // 旧耐久、当前耐久
   hum: TUserHuman;          // 玩家对象
begin
   if (UseItems[U_WEAPON].Index > 0) and (UseItems[U_WEAPON].Dura > 0) then begin
      idura := UseItems[U_WEAPON].Dura;
      olddura := Round (idura / 1000);
      idura := idura - wdam;
      // 耐久为0时武器失效
      if idura <= 0 then begin
         idura := 0;
         (*
         UseItems[U_WEAPON].Dura := 0;
         //다 닮면 없어진다.
         if RaceServer = RC_USERHUMAN then begin
            hum := TUserHuman(self);
            hum.SendDelItem (UseItems[U_WEAPON]); //클라이언트에 없어진거 보냄
            //닳아 없어진거 로그 남김
            AddUserLog ('3'#9 +  //닳음_ +
                        MapName + ''#9 +
                        IntToStr(CX) + ''#9 +
                        IntToStr(CY) + ''#9 +
                        UserName + ''#9 +
                        UserEngine.GetStdItemName (UseItems[U_WEAPON].Index) + ''#9 +
                        IntToStr(UseItems[U_WEAPON].MakeIndex) + ''#9 +
                        IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                        '0');
         end;
         UseItems[U_WEAPON].Index := 0; *)
         SysMsg (UserEngine.GetStdItemName (UseItems[U_WEAPON].Index) + '넣씹綠苟슉逞0', 0);
         UseItems[U_WEAPON].Dura := 0;
         RecalcAbilitys;
         SendMsg (self, RM_DURACHANGE, U_WEAPON, UseItems[U_WEAPON].Dura, UseItems[U_WEAPON].DuraMax, 0, '');
         SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
         SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
      end else
         UseItems[U_WEAPON].Dura := idura;
      if olddura <> Round(idura / 1000) then begin //내구성 수치 변경
         SendMsg (self, RM_DURACHANGE, U_WEAPON, UseItems[U_WEAPON].Dura, UseItems[U_WEAPON].DuraMax, 0, '');
      end;
   end;
end;

{ GetAttackPower - 获取攻击力
  功能: 计算实际攻击力
  参数:
    damage - 基础伤害
    ranval - 随机范围
  返回值: 实际攻击力
  实现原理:
    - 幸运时有几率打出最大伤害
    - 诅咒时有几率打出最小伤害 }
function TCreature.GetAttackPower (damage, ranval: integer): integer;
var
   v1, v2: integer;  // 临时变量
begin
   if ranval < 0 then ranval := 0;
   // 幸运时有几率打出最大伤害
   if Luck > 0 then begin
      if Random(10 - _MIN(9,Luck)) = 0 then Result := damage + ranval
      else Result := damage + Random(ranval + 1);
   end else begin
      Result := damage + Random(ranval + 1);
      // 诅咒时有几率打出最小伤害
      if Luck < 0 then begin
         if Random(10 - _MAX(0,-Luck)) = 0 then Result := damage;
      end;
   end;
end;

{ _Attack - 攻击处理
  功能: 处理攻击逻辑
  参数:
    hitmode - 攻击模式
    targ - 目标生物
  返回值: 是否攻击成功 }
function  TCreature._Attack (hitmode: word; targ: TCreature): Boolean;
   // 直接攻击
   function DirectAttack (target: TCreature; damage: integer): Boolean;
   begin
      Result := FALSE;
      // 安全区不能攻击
      if (RaceServer = RC_USERHUMAN) and (target.RaceServer = RC_USERHUMAN) and ((target.InSafeZone) or (InSafeZone)) then
         exit;
      if IsProperTarget (target) then begin
         // 命中判定
         if Random(target.SpeedPoint) < AccuracyPoint then begin
            target.StruckDamage (damage, self);
            target.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, damage,
                     target.WAbil.HP, target.WAbil.MaxHP, Longint(self), '', 500);
            // 怪物需要直接发送消息
            if target.RaceServer <> RC_USERHUMAN then
               target.SendMsg (target, RM_STRUCK, damage, target.WAbil.HP, target.WAbil.MaxHP, Longint(self), '');
            Result := TRUE;
         end;
      end;
   end;
   // 刺杀攻击(攻击前方2格)
   function SwordLongAttack (damage: integer): Boolean;
   var
      xx, yy: integer;
      target: TCreature;
   begin
      Result := FALSE;
      if GetNextPosition (PEnvir, CX, CY, Dir, 2, xx, yy) then begin
         target := TCreature (PEnvir.GetCreature (xx, yy, TRUE));
         if (damage > 0) and (target <> nil) then
            if IsProperTarget (target) then begin
               Result := DirectAttack (target, damage);
               SelectTarget (target);
            end;
      end;
   end;
   // 半月攻击(攻击前方3个方向)
   function SwordWideAttack (damage: integer): Boolean;
   const
      valarr: array[0..2] of integer = (7, 1, 2);
   var
      i, ndir, xx, yy: integer;
      target: TCreature;
   begin
      Result := FALSE;
      for i:=0 to 2 do begin
         ndir := (Dir + valarr[i]) mod 8;
         if GetNextPosition (PEnvir, CX, CY, ndir, 1, xx, yy) then begin
            target := TCreature (PEnvir.GetCreature (xx, yy, TRUE));
            if (damage > 0) and (target <> nil) then
               if IsProperTarget (target) then begin
                  Result := DirectAttack (target, damage);
                  SelectTarget (target);
               end;
         end;
      end;
   end;
   // 2003/03/15 狂风斩(攻击周围7个方向)
   function SwordCrossAttack (damage: integer): Boolean;
   const
      valarr: array[0..6] of integer = (7, 1, 2, 3, 4, 5, 6);
   var
      i, ndir, xx, yy: integer;
      target: TCreature;
   begin
      Result := FALSE;
      for i:=0 to 6 do begin
         ndir := (Dir + valarr[i]) mod 8;
         if GetNextPosition (PEnvir, CX, CY, ndir, 1, xx, yy) then begin
            target := TCreature (PEnvir.GetCreature (xx, yy, TRUE));
            if (damage > 0) and (target <> nil) then
               if IsProperTarget (target) then begin
                  // 对玩家伤害降低20%
                  if target.RaceServer <> RC_USERHUMAN then
                     Result := DirectAttack (target, damage)
                  else
                     Result := DirectAttack (target, Round(damage * 0.8) );
                  SelectTarget (target);
               end;
         end;
      end;
   end;
{
   var
      ndir: byte;
      rx, ry, xx, yy: integer;
      target: TCreature;
      procedure __DAttack;
      begin
         if GetNextPosition (PEnvir, rx, ry, ndir, 1, xx, yy) then begin
            target := TCreature (PEnvir.GetCreature (xx, yy, TRUE));
            if (damage > 0) and (target <> nil) then
               Result := DirectAttack (target, damage);
         end;
      end;
   begin
      Result := FALSE;
      ndir := Dir; GetNextPosition (PEnvir, CX, CY, ndir, 1, rx, ry);
      //정면 앞
      __DAttack;
      //오른쪽
      ndir := GetTurnDir (Dir, 2);
      __DAttack;
      //왼쪽
      ndir := GetTurnDir (Dir, 6);
      __DAttack;
   end;
}
var
   dam, seconddam, armor, olddura, idura, weapondamage, n: integer;
   hum: TUserHuman;
   addplus: Boolean;
begin
   Result := FALSE;
   try
      addplus := FALSE;  //풀러스 파워가 먹혔느지 여부
      weapondamage := 0;
      dam := 0;
      seconddam := 0;
      if targ <> nil then begin
         with WAbil do
            dam := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC))); //
         //검법으로 향상된 파워
         if (hitmode = HM_POWERHIT) and (BoAllowPowerHit) then begin
            BoAllowPowerHit := FALSE;
            dam := dam + HitPowerPlus;
            addplus := TRUE;
         end;
         if (hitmode = HM_FIREHIT) and (BoAllowFireHit) then begin
            BoAllowFireHit := FALSE;
            with WAbil do
               dam := dam + Round(dam / 100 * (HitDouble * 10));
                  //GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
            addplus := TRUE;
         end;
         // 2003/03/15 광풍참...재검토
{
         if (hitmode = HM_CROSSHIT) then begin
            BoAllowPowerHit := FALSE;
            dam := dam + HitPowerPlus;
            addplus := TRUE;
         end;
}
      end else begin
         with WAbil do
            dam := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC))); //
         //검법으로 향상된 파워
         if (hitmode = HM_POWERHIT) and (BoAllowPowerHit) then begin
            BoAllowPowerHit := FALSE;
            dam := dam + HitPowerPlus;
            addplus := TRUE;
         end;
      end;

      //긴 거리 공격 (어검)
      if hitmode = HM_LONGHIT then begin
         seconddam := 0;
         if RaceServer = RC_USERHUMAN then begin
            if PLongHitSkill <> nil then
               seconddam := Round(dam / (PLongHitSkill.pDef.MaxTrainLevel+2) * (PLongHitSkill.Level+2));
         end else seconddam := dam;
         if seconddam > 0 then SwordLongAttack (seconddam);
      end;
      //주변 공격  (반월)
      if hitmode = HM_WIDEHIT then begin
         seconddam := 0;
         if RaceServer = RC_USERHUMAN then begin
            if PWideHitSkill <> nil then
               seconddam := Round(dam / (PWideHitSkill.pDef.MaxTrainLevel+10) * (PWideHitSkill.Level+2));
         end else seconddam := dam;
         if seconddam > 0 then SwordWideAttack (seconddam);
      end;
      //크로스 공격 -> 광풍참
      if hitmode = HM_CROSSHIT then begin
         seconddam := 0;
         if RaceServer = RC_USERHUMAN then begin
            if PCrossHitSkill <> nil then
               seconddam := Round(dam / (PCrossHitSkill.pDef.MaxTrainLevel+11) * (PCrossHitSkill.Level+3));
         end else seconddam := dam;
         if seconddam > 0 then SwordCrossAttack (seconddam);
      end;

      if targ = nil then      //어검, 반월 자동수련을 막기 위해서
         exit;

      //어검,반월 등은 targ와 상관없이 _Attack안으로 들어온다.
      if IsProperTarget (targ) then begin
         if AccuracyPoint > Random(targ.SpeedPoint) then begin
            ;
         end else
            dam := 0;
      end else
         dam := 0;


      if dam > 0 then begin
         dam := targ.GetHitStruckDamage (self, dam);
         weapondamage := Random(5) + 2 - AddAbil.WeaponStrong;  //단단한 무기는 내구가 잘 안단다.
      end;

      if dam > 0 then begin
         targ.StruckDamage (dam, self);
         targ.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                  targ.WAbil.HP{lparam1}, targ.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '', 200);

         //때리는자가 마비의반지를 끼고 있음
         if BoAbilMakeStone then begin
            if Random(5 + targ.AntiPoison) = 0 then
               targ.MakePoison (POISON_STONE, 5{시간}, 0);   //마비
         end;

         //때리는자가 밀화 세트를 끼고 있음 (체력 흡수)
         if SuckupEnemyHealthRate > 0 then begin
            SuckupEnemyHealth := SuckupEnemyHealth + (dam / 100 * SuckupEnemyHealthRate);
            if SuckupEnemyHealth >= 2 then begin
               n := Trunc (SuckupEnemyHealth);
               SuckupEnemyHealth := SuckupEnemyHealth - n;
               DamageHealth (-n);
            end;
         end;

         //검술 향상
         if (PSwordSkill <> nil) and (targ.RaceServer >= RC_ANIMAL) then begin
            if PSwordSkill.Level < 3 then begin
               if Abil.Level >= PSwordSkill.pDef.NeedLevel[PSwordSkill.Level] then begin
                  TrainSkill (PSwordSkill, 1 + Random(3));
                  if not CheckMagicLevelup (PSwordSkill) then
                     SendDelayMsg (self, RM_MAGIC_LVEXP, 0, PSwordSkill.pDef.MagicId, PSwordSkill.Level, PSwordSkill.CurTrain, '', 3000);
               end;
            end;
         end;
         //검술 향상 2 (예도검법)
         if addplus then begin
            if (PPowerHitSkill <> nil) and (targ.RaceServer >= RC_ANIMAL) then begin
               if PPowerHitSkill.Level < 3 then begin
                  if Abil.Level >= PPowerHitSkill.pDef.NeedLevel[PPowerHitSkill.Level] then begin
                     TrainSkill (PPowerHitSkill, 1 + Random(3));
                     if not CheckMagicLevelup (PPowerHitSkill) then
                        SendDelayMsg (self, RM_MAGIC_LVEXP, 0, PPowerHitSkill.pDef.MagicId, PPowerHitSkill.Level, PPowerHitSkill.CurTrain, '', 3000);
                  end;
               end;
            end;
         end;
         //검술 향상 3 (어검술)
         if (hitmode = HM_LONGHIT) and (PLongHitSkill <> nil) and (targ.RaceServer >= RC_ANIMAL) then begin
            if PLongHitSkill.Level < 3 then begin
               if Abil.Level >= PLongHitSkill.pDef.NeedLevel[PLongHitSkill.Level] then begin
                  TrainSkill (PLongHitSkill, 1);
                  if not CheckMagicLevelup (PLongHitSkill) then
                     UpdateDelayMsgCheckParam1 (self, RM_MAGIC_LVEXP, 0, PLongHitSkill.pDef.MagicId, PLongHitSkill.Level, PLongHitSkill.CurTrain, '', 3000);
               end;
            end;
         end;
         //검술 향상 4 (반월검법)
         if (hitmode = HM_WIDEHIT) and (PWideHitSkill <> nil) and (targ.RaceServer >= RC_ANIMAL) then begin
            if PWideHitSkill.Level < 3 then begin
               if Abil.Level >= PWideHitSkill.pDef.NeedLevel[PWideHitSkill.Level] then begin
                  TrainSkill (PWideHitSkill, 1);
                  if not CheckMagicLevelup (PWideHitSkill) then
                     UpdateDelayMsgCheckParam1 (self, RM_MAGIC_LVEXP, 0, PWideHitSkill.pDef.MagicId, PWideHitSkill.Level, PWideHitSkill.CurTrain, '', 3000);
               end;
            end;
         end;
         //검술 향상 5 (염화결)
         if (hitmode = HM_FIREHIT) and (PFireHitSkill <> nil) and (targ.RaceServer >= RC_ANIMAL) then begin
            if PFireHitSkill.Level < 3 then begin
               if Abil.Level >= PFireHitSkill.pDef.NeedLevel[PFireHitSkill.Level] then begin
                  TrainSkill (PFireHitSkill, 1);
                  if not CheckMagicLevelup (PFireHitSkill) then
                     UpdateDelayMsgCheckParam1 (self, RM_MAGIC_LVEXP, 0, PFireHitSkill.pDef.MagicId, PFireHitSkill.Level, PFireHitSkill.CurTrain, '', 3000);
               end;
            end;
         end;
         //2003/03/15 신규무공
         //검술 향상 6 (광풍참)
         if (hitmode = HM_CROSSHIT) and (PCrossHitSkill <> nil) and (targ.RaceServer >= RC_ANIMAL) then begin
            if PCrossHitSkill.Level < 3 then begin
               if Abil.Level >= PCrossHitSkill.pDef.NeedLevel[PCrossHitSkill.Level] then begin
                  TrainSkill (PCrossHitSkill, 1);
                  if not CheckMagicLevelup (PCrossHitSkill) then
                     UpdateDelayMsgCheckParam1 (self, RM_MAGIC_LVEXP, 0, PCrossHitSkill.pDef.MagicId, PCrossHitSkill.Level, PCrossHitSkill.CurTrain, '', 3000);
               end;
            end;
         end;
         //맞아야 성공
         Result := TRUE;
      end;

      if weapondamage > 0 then begin
         if UseItems[U_WEAPON].Index > 0 then begin //무기를 차고 있으면
            DoDamageWeapon (weapondamage);
         end;
      end;

      //몬스터한테는 직접전달해야 함..
      if targ.RaceServer <> RC_USERHUMAN then
         targ.SendMsg (targ, RM_STRUCK, dam, targ.WAbil.HP, targ.WAbil.MaxHP, Longint(self), '');
   except
      MainOutMessage ('[Exception] TCreature._Attack');
   end;
end;


procedure TCreature.HitHit (target: TCreature; hitmode, dir: word);
   procedure IdentifyWeapon (var ui: TUserItem);
   begin
      if ui.Desc[0] + ui.Desc[1] + ui.Desc[2] < 20 then begin
         case ui.Desc[10] of
            10..13: ui.Desc[0] := ui.Desc[0] + (ui.Desc[10] - 9);
            20..23: ui.Desc[1] := ui.Desc[1] + (ui.Desc[10] - 19);
            30..33: ui.Desc[2] := ui.Desc[2] + (ui.Desc[10] - 29);
            1: ui.Index := 0;  //뽀개짐
         end;
      end else
         ui.Index := 0;
      ui.Desc[10] := 0;
   end;
   procedure CheckWeaponUpgradeResult;
   var
      oldweapon: TUserItem;
      hum: TUserHuman;
   begin
      if UseItems[U_WEAPON].Desc[10] <> 0 then begin
         //아이덴티파이가 안된 무기
         oldweapon := UseItems[U_WEAPON];
         IdentifyWeapon (UseItems[U_WEAPON]);
         if UseItems[U_WEAPON].Index = 0 then begin  //뽀사짐
            SysMsg ('콄ず챎씁쨢퇱퇗딤좧', 0);
            hum := TUserHuman(self);
            hum.SendDelItem (oldweapon); //클라이언트에 없어진거 보냄
            SendRefMsg (RM_BREAKWEAPON, 0, 0, 0, 0, '');
            //업그레이드 실패로 없어진거 로그 남김
            AddUserLog ('21'#9 + //업실_ +
                        MapName + ''#9 +
                        IntToStr(CX) + ''#9 +
                        IntToStr(CY) + ''#9 +
                        UserName + ''#9 +
                        UserEngine.GetStdItemName (oldweapon.Index) + ''#9 +
                        IntToStr(oldweapon.MakeIndex) + ''#9 +
                        '1'#9 +
                        '0');
            FeatureChanged;
         end else begin  //업그레이드 성공
            SysMsg ('콄ず챎씁쨢퇱ㅙ�짦좧', 1);
            hum := TUserHuman(self);
            hum.SendUpdateItem (UseItems[U_WEAPON]);
            //업그레이드 성공 로그 남김
            AddUserLog ('20'#9 + //업성_ +
                        MapName + ''#9 +
                        IntToStr(CX) + ''#9 +
                        IntToStr(CY) + ''#9 +
                        UserName + ''#9 +
                        UserEngine.GetStdItemName (UseItems[U_WEAPON].Index) + ''#9 +
                        IntToStr(UseItems[U_WEAPON].MakeIndex) + ''#9 +
                        '1'#9 +
                        '0');
            RecalcAbilitys;
            SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
            SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
         end;
      end;
   end;
   function GetSWSpell (pum: PTUserMagic): integer;
   begin
      Result := Round(pum.pDef.Spell / (pum.pDef.MaxTrainLevel+1) * (pum.Level+1));
   end;
var
   newdir, soundeff, msg: integer;
   targ: TCreature;
   bopower, bofire: Boolean;
begin
   if hitmode = HM_WIDEHIT then begin
      if PWideHitSkill <> nil then begin
         if WAbil.MP > 0 then begin
            DamageSpell (GetSWSpell(PWideHitSkill) + PWideHitSkill.pDef.DefSpell);
            HealthSpellChanged;
         end else
            hitmode := RM_HIT;  //마력없음...
      end;
   end;
   // 2003/03/15 신규무공
   if hitmode = HM_CROSSHIT then begin
      if PCrossHitSkill <> nil then begin
         if WAbil.MP > 0 then begin
            DamageSpell (GetSWSpell(PCrossHitSkill) + PCrossHitSkill.pDef.DefSpell);
            HealthSpellChanged;
         end else
            hitmode := RM_HIT;  //마력없음...
      end;
   end;

	//방향으로 친다.
   self.Dir := dir;
   if target = nil then targ := GetFrontCret
   else targ := target;

   if targ <> nil then begin
      if UseItems[U_WEAPON].Index <> 0 then begin
         //제련이 끝난 무기의 테스트(성공여부)
         CheckWeaponUpgradeResult;
      end;
   end;

   bopower := BoAllowPowerHit;  //
   bofire := BoAllowFireHit;    //_attack 에서 해제 됨

   if _Attack (hitmode, targ) then
      SelectTarget (targ);

   msg := RM_HIT;
   if RaceServer = RC_USERHUMAN then begin
      msg := RM_HIT;
      case hitmode of
         HM_HIT:  msg := RM_HIT;
         HM_HEAVYHIT:   msg := RM_HEAVYHIT;
         HM_BIGHIT:     msg := RM_BIGHIT;
         HM_POWERHIT:
            if bopower then begin
               msg := RM_POWERHIT;
            end;
         HM_LONGHIT:
            if PLongHitSkill <> nil then begin
               msg := RM_LONGHIT;
            end;
         HM_WIDEHIT:
            if PWideHitSkill <> nil then begin
               msg := RM_WIDEHIT;
            end;
         HM_FIREHIT:
            if bofire then begin
               msg := RM_FIREHIT;
            end;
         // 2003/03/15 신규무공
         HM_CROSSHIT:
            if PCrossHitSkill <> nil then begin
               msg := RM_CROSSHIT;
            end;
      end;
   end;
   //SendRefMsg (msg, self.Dir, CX, CY, 0, '');
   HitMotion (msg, self.Dir, CX, CY);
end;

{ HitMotion - 攻击动作
  功能: 发送攻击动作消息
  参数:
    hitmsg - 攻击消息类型
    hitdir - 攻击方向
    x, y - 坐标 }
procedure TCreature.HitMotion (hitmsg: integer; hitdir: byte; x, y: integer);
begin
   SendRefMsg (hitmsg, hitdir, x, y, 0, '');
end;

{ HitHit2 - 攻击目标(物理+魔法)
  功能: 对目标造成物理和魔法伤害
  参数:
    target - 目标生物
    hitpwr - 物理伤害
    magpwr - 魔法伤害
    all - 是否攻击所有目标 }
procedure  TCreature.HitHit2 (target: TCreature; hitpwr, magpwr: integer; all: Boolean);
begin
   HitHitEx2 (target, RM_HIT, hitpwr, magpwr, all);
end;

{ HitHitEx2 - 攻击目标(扩展)
  功能: 对目标造成物理和魔法伤害
  参数:
    target - 目标生物
    rmmsg - 消息类型
    hitpwr - 物理伤害
    magpwr - 魔法伤害
    all - 是否攻击所有目标 }
procedure  TCreature.HitHitEx2 (target: TCreature; rmmsg, hitpwr, magpwr: integer; all: Boolean);
var
   i, dam: integer;   // 循环、伤害
   list: TList;       // 生物列表
   cret: TCreature;   // 生物对象
begin
   self.Dir := GetNextDirection (CX, CY, target.CX, target.CY);
   list := TList.Create;
   PEnvir.GetAllCreature (target.CX, target.CY, TRUE, list);
   for i:=0 to list.Count-1 do begin
      cret := TCreature(list[i]);
      if IsProperTarget (cret) then begin
         dam := 0;
         dam := dam + cret.GetHitStruckDamage (self, hitpwr);
         dam := dam + cret.GetMagStruckDamage (self, magpwr);
         if dam > 0 then begin
            cret.StruckDamage (dam, self);
            cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam,
                     cret.WAbil.HP, cret.WAbil.MaxHP, Longint(self), '', 200);
         end;
      end;
   end;
   list.Free;
   SendRefMsg (rmmsg, self.Dir, CX, CY, 0, '');
end;

{ CharPushed - 被推开
  功能: 被某种力量推开
  参数:
    ndir - 推开方向
    pushcount - 推开格数
  返回值: 实际推开的格数 }
function  TCreature.CharPushed (ndir, pushcount: integer): integer;
var
   i, nx, ny, olddir, oldx, oldy: integer;  // 循环、新坐标、旧方向、旧坐标
   flag: Boolean;  // 是否移动标志
begin
   Result := 0;
   olddir := Dir;
   oldx := CX;
   oldy := CY;
   Dir := ndir;
   flag := FALSE;
   // 尝试推开指定格数
   for i:=0 to pushcount-1 do begin
      GetFrontPosition (self, nx, ny);
      if PEnvir.CanWalk (nx, ny, FALSE) then begin
         if PEnvir.MoveToMovingObject (CX, CY, self, nx, ny, FALSE) > 0 then begin
            CX := nx;
            CY := ny;
            SendRefMsg (RM_PUSH, GetBack(ndir), CX, CY, 0, '');
            Inc (Result);
            // 怪物被推开后攻击延迟
            if RaceServer >= RC_ANIMAL then
               WalkTime := WalkTime + 800;
            flag := TRUE;
         end else
            break;
      end else
         break;
   end;

   if flag then
      Dir := GetBack(ndir);
end;

{ CharRushRush - 野蛮冲撞
  功能: 执行野蛮冲撞技能
  参数:
    ndir - 冲撞方向
    rushlevel - 技能等级
  返回值: 是否成功 }
function  TCreature.CharRushRush (ndir, rushlevel: integer): Boolean;
   // 检查是否可以推开目标
   function CanPush (cret: TCreature): Boolean;
   var
      levelgap: integer;  // 等级差
   begin
      Result := FALSE;
      if (Abil.Level > cret.Abil.Level) and (not cret.StickMode) then begin
         levelgap := Abil.Level - cret.Abil.Level;
         // 根据技能等级和等级差计算成功率
         if (Random(20) < 6+rushlevel*3+levelgap) then begin
            if IsProperTarget(cret) then begin
               Result := TRUE;
            end;
         end;
      end;
   end;
var
   i, nx, ny, damage, damagelevel, mydamagelevel: integer;
   cret, cret2, attackcret: TCreature;
   crash: Boolean;
begin
   Result := FALSE;
   crash := TRUE;
   Dir := ndir;
   attackcret := nil;
   damagelevel := rushlevel + 1;
   mydamagelevel := damagelevel;
   cret := GetFrontCret;

   if cret <> nil then begin
      for i:=0 to _MAX(2,rushlevel+1) do begin
         cret := GetFrontCret;
         if cret <> nil then begin
            mydamagelevel := 0;
            if CanPush (cret) then begin
               if rushlevel >= 3 then
                  if GetNextPosition (PEnvir, CX, CY, Dir, 2, nx, ny) then begin
                     cret2 := TCreature (PEnvir.GetCreature (nx, ny, TRUE));
                     if cret2 <> nil then begin
                        if CanPush (cret2) then begin
                           cret2.CharPushed (Dir, 1);
                        end;
                     end;
                  end;
               attackcret := cret;
               if cret.CharPushed (Dir, 1) = 1 then begin
                  GetFrontPosition (self, nx, ny);
                  if PEnvir.MoveToMovingObject (CX, CY, self, nx, ny, FALSE) > 0 then begin
                     CX := nx;
                     CY := ny;
                     SendRefMsg (RM_RUSH, ndir, CX, CY, 0, '');
                     crash := FALSE;
                     Result := TRUE;
                  end;
                  Dec (damagelevel);
               end else begin
                  break;
               end;
            end else
               break;
         end;
      end;
   end else begin
      crash := FALSE;
      for i:=0 to _MAX(2,rushlevel+1) do begin
         GetFrontPosition (self, nx, ny);
         if PEnvir.MoveToMovingObject (CX, CY, self, nx, ny, FALSE) > 0 then begin
            CX := nx;
            CY := ny;
            SendRefMsg (RM_RUSH, ndir, CX, CY, 0, '');
            Dec (mydamagelevel);
         end else begin  //벽에 부딯힌 경우
            if PEnvir.CanWalk (nx, ny, TRUE) then
               mydamagelevel := 0  //사람때문에 못감
            else crash := TRUE; //벽에 부딯힘
            break;
         end;
      end;
   end;
   if attackcret <> nil then begin
      if damagelevel < 0 then damagelevel := 0;
      damage := (1+damagelevel)*4 + Random((1+damagelevel) * 5);
      with attackcret do begin
         damage := GetHitStruckDamage (self, damage);
         StruckDamage (damage);
         SendRefMsg (RM_STRUCK, damage{wparam}, WAbil.HP{lparam1}, WAbil.MaxHP{lparam2}, Longint(self){hiter}, '');
         //몬스터한테는 직접전달해야 함..
         if RaceServer <> RC_USERHUMAN then
            SendMsg (attackcret, RM_STRUCK, damage, WAbil.HP, WAbil.MaxHP, Longint(self), '');
      end;
   end;
   if crash then begin
      //움직이는 시늉한다.
      GetFrontPosition (self, nx, ny);
      SendRefMsg (RM_RUSHKUNG, Dir, nx, ny, 0, '');
      //SendRefMsg (RM_TURN, Dir, CX, CY, 0, '');
      SysMsg ('�쩎식섣짵턲좧', 0);
   end;
   if mydamagelevel > 0 then begin
      if damagelevel < 0 then damagelevel := 0;
      damage := (1+damagelevel)*5 + Random((1+damagelevel) * 5);
      damage := GetHitStruckDamage (self, damage);
      StruckDamage (damage);
      if(crash) and (LastHiter <> nil) then LastHiter := nil;
      SendRefMsg (RM_STRUCK, damage{wparam}, WAbil.HP{lparam1}, WAbil.MaxHP{lparam2}, 0{hiter}, '');
   end;
end;

{ SiegeCount - 包围数量
  功能: 获取周围包围的生物数量
  返回值: 包围数量 }
function  TCreature.SiegeCount: integer;
var
   i: integer;      // 循环变量
   cret: TCreature; // 生物对象
begin
   Result := 0;
   for i:=0 to VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
      if not cret.Death then begin
         // 检查是否在相邻格子
         if (abs(CX-cret.CX) <= 1) and (abs(CY-cret.CY) <= 1) then
            Inc (Result);
      end;
   end;
end;

{ SiegeLockCount - 包围锁定数量
  功能: 获取周围不可行走的格子数量
  返回值: 锁定数量 }
function   TCreature.SiegeLockCount: integer;
var
   i, j, n: integer;  // 循环、计数
begin
   n := 0;
   for i:=-1 to 1 do
      for j:=-1 to 1 do begin
         // 检查周围8个方向是否可行走
         if (not PEnvir.CanWalk (CX+i, CY+j, FALSE)) and (not ((i=0) and (j=0))) then
            Inc (n);
      end;
   Result := n;      
end;

{ MakePoison - 中毒
  功能: 使目标中毒
  参数:
    poison - 中毒类型
    sec - 持续时间(秒)
    poisonlv - 中毒等级
  返回值: 是否成功 }
function  TCreature.MakePoison (poison, sec, poisonlv: integer): Boolean;
var
   old: integer;  // 旧状态
begin
   Result := FALSE;
   if poison in [0..11] then begin
      old := CharStatus;
      // 更新中毒时间
      if StatusArr[poison] > 0 then begin
         if sec > StatusArr[poison] then
            StatusArr[poison] := sec;
      end else
         StatusArr[poison] := sec;
      StatusTimes[poison] := GetTickCount;
      CharStatus := GetCharStatus;
      PoisonLevel := poisonlv;
      // 状态变化时通知
      if old <> CharStatus then
         CharStatusChanged;
      if RaceServer = RC_USERHUMAN then
         SysMsg ('你中毒了', 0);
      Result := TRUE;
   end;
end;

{ ClearPoison - 清除中毒
  功能: 清除指定类型的中毒
  参数:
    poison - 中毒类型 }
procedure  TCreature.ClearPoison (poison: integer);
var
   old: integer;  // 旧状态
begin
   if poison in [0..11] then begin
      old := CharStatus;
      if StatusArr[poison] > 0 then
         StatusArr[poison] := 0;
      CharStatus := GetCharStatus;
      if old <> CharStatus then
         CharStatusChanged;
   end;
end;


{ GetFrontCret - 获取前方生物
  功能: 获取前方一格的生物
  返回值: 生物对象或nil }
function  TCreature.GetFrontCret: TCreature;
var
	fx, fy: integer;  // 前方坐标
begin
	Result := nil;
	if GetFrontPosition (self, fx, fy) then begin
   	Result := TCreature (PEnvir.GetCreature (fx, fy, TRUE));
   end;
end;

{ GetBackCret - 获取后方生物
  功能: 获取后方一格的生物
  返回值: 生物对象或nil }
function  TCreature.GetBackCret: TCreature;
var
	fx, fy: integer;  // 后方坐标
begin
	Result := nil;
	if GetBackPosition (self, fx, fy) then begin
   	Result := TCreature (PEnvir.GetCreature (fx, fy, TRUE));
   end;
end;

{ CretInNearXY - 生物在附近
  功能: 检查指定生物是否在指定坐标附近
  参数:
    tagcret - 目标生物
    xx, yy - 坐标
  返回值: 是否在附近 }
function TCreature.CretInNearXY (tagcret: TCreature; xx, yy: integer): Boolean;
var
	i, j, k: Longint;   // 循环变量
   pm: PTMapInfo;      // 地图信息指针
   inrange: Boolean;   // 是否在范围内
   cret: TCreature;    // 生物对象
begin
   Result := FALSE;
   // 检查周围9格
   for i:=xx-1 to xx+1 do
      for j:=yy-1 to yy+1 do begin
         inrange := PEnvir.GetMapXY (i, j, pm);
         if inrange then begin
            if pm.ObjList <> nil then
               for k:=0 to pm.ObjList.Count-1 do
                  // 检查移动对象
                  if PTAThing (pm.ObjList[k]).Shape = OS_MOVINGOBJECT then begin
                     cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                     if cret <> nil then
                        if (not cret.BoGhost) and (cret = tagcret) then begin
                           Result := TRUE;
                           exit;
                        end;
                  end;
         end;
      end;
end;

{ MakeSlave - 创建召唤兽
  功能: 创建召唤兽
  参数:
    sname - 召唤兽名称
    slevel - 召唤等级
    max_slave - 最大召唤数量
    royaltysec - 忠诚时间(秒)
  返回值: 召唤兽对象或nil }
function   TCreature.MakeSlave (sname: string; slevel, max_slave, royaltysec: integer): TCreature;
var
   nx, ny: integer;   // 坐标
   mon: TCreature;    // 召唤兽对象
begin
   Result := nil;
   if (SlaveList.Count < max_slave) then begin
      GetFrontPosition (self, nx, ny);
      mon := UserEngine.AddCreatureSysop (PEnvir.MapName, nx, ny, sname);
      if mon <> nil then begin
         mon.Master := self;
         mon.MasterRoyaltyTime := GetTickCount + longword(royaltysec) * 1000;
         mon.SlaveMakeLevel := slevel;
         mon.SlaveExpLevel := slevel;
         mon.RecalcAbilitys;
         // 恢复一半血量
         if mon.WAbil.HP < mon.WAbil.MaxHP then begin
            mon.WAbil.HP := mon.WAbil.HP + (mon.WAbil.MaxHP - mon.WAbil.HP) div 2;
         end;
         mon.ChangeNameColor;
         SlaveList.Add (mon);
         Result := mon;
      end;
   end;
end;

{ ClearAllSlaves - 清除所有召唤兽
  功能: 清除所有召唤兽(主要用于服务器迁移) }
procedure TCreature.ClearAllSlaves;
var
   i: integer;  // 循环变量
begin
   for i:=0 to SlaveList.Count-1 do begin
      if not TCreature(SlaveList[i]).Death then
         TCreature(SlaveList[i]).MakeGhost;
   end;
end;

{ ExistAttackSlaves - 是否存在攻击中的召唤兽
  功能: 检查是否有召唤兽正在攻击玩家
  返回值: 是否存在 }
function TCreature.ExistAttackSlaves: Boolean;
var
   i: integer;      // 循环变量
   cret: TCreature; // 生物对象
begin
   Result := FALSE;
   for i:=0 to SlaveList.Count-1 do begin
      cret := TCreature(SlaveList[i]);
      if not cret.Death then
      begin
         // 检查是否正在攻击玩家
         if cret.TargetCret <> nil then begin
            if cret.TargetCret.RaceServer = RC_USERHUMAN then begin
               Result := TRUE;
               break;
            end;
         end;
      end;
   end;
end;

{------------------------- 组队相关 -----------------------------}

{ IsGroupMember - 是否组队成员
  功能: 检查指定生物是否为组队成员
  参数:
    cret - 目标生物
  返回值: 是否为成员 }
function  TCreature.IsGroupMember (cret: TCreature): Boolean;
var
   i: integer;  // 循环变量
begin
   Result := FALSE;
   if GroupOwner <> nil then begin
      for i:=0 to GroupOwner.GroupMembers.Count-1 do begin
         if GroupOwner.GroupMembers.Objects[i] = cret then begin
            Result := TRUE;
            break;
         end;
      end;
   end;
end;

{ CheckGroupValid - 检查组队有效性
  功能: 检查组队是否有效
  返回值: 是否有效 }
function  TCreature.CheckGroupValid: Boolean;
begin
   Result := TRUE;
   // 只剩队长时解散组队
   if GroupMembers.Count <= 1 then begin
      GroupMsg ('你的组队已解散。');
      GroupMembers.Clear;
      GroupOwner := nil;
      Result := FALSE;
   end;
end;

{ DelGroupMember - 删除组队成员
  功能: 从组队中删除指定成员
  参数:
    who - 要删除的成员 }
procedure  TCreature.DelGroupMember (who: TCreature);
var
   i: integer;      // 循环变量
   cret: TCreature; // 生物对象
   hum: TUserHuman; // 玩家对象
begin
   if GroupOwner <> who then begin
      for i:=0 to GroupMembers.Count-1 do begin
         cret := TCreature(GroupMembers.Objects[i]);
         if cret = who then begin
            who.LeaveGroup;
            GroupMembers.Delete (i);
            break;
         end;
      end;
   end else begin
      // 队长退出，组队解散
      for i:=GroupMembers.Count-1 downto 0 do begin
         TCreature (GroupMembers.Objects[i]).LeaveGroup;
         GroupMembers.Delete (i);
      end;
   end;
   hum := TUserHuman(self);
   if not CheckGroupValid then
      hum.SendDefMessage (SM_GROUPCANCEL, 0, 0, 0, 0, '')
   else hum.RefreshGroupMembers;
end;

{ EnterGroup - 加入组队
  功能: 加入指定组队
  参数:
    gowner - 组队队长 }
procedure  TCreature.EnterGroup (gowner: TCreature);
begin
   GroupOwner := gowner;
   GroupMsg (UserName + '加入了组队。');
end;

{ LeaveGroup - 离开组队
  功能: 离开当前组队 }
procedure  TCreature.LeaveGroup;
begin
   GroupMsg (UserName + '离开了组队。');
   GroupOwner := nil;
   SendMsg (self, RM_GROUPCANCEL, 0, 0, 0, 0, '');
end;

{ DenyGroup - 拒绝组队
  功能: 拒绝组队邀请或退出组队 }
procedure  TCreature.DenyGroup;
begin
   if GroupOwner <> nil then begin
      if GroupOwner <> self then begin
         // 退出组队
         GroupOwner.DelGroupMember (self);
         AllowGroup := FALSE;
      end else begin
         // 队长不能退出
         SysMsg ('你是队长，不能退出组队。', 0);
      end;
   end else begin
      AllowGroup := FALSE;
   end;
end;


{----------------------------------------------------------------}



{ TargetInAttackRange - 目标在攻击范围内
  功能: 检查目标是否在攻击范围内
  参数:
    target - 目标生物
    targdir - 目标方向(输出)
  返回值: 是否在范围内 }
function  TCreature.TargetInAttackRange (target: TCreature; var targdir: byte): Boolean;
begin
   Result := FALSE;
   // 检查是否在相邻8格内
   if (target.CX >= (self.CX - 1)) and (target.CX <= (self.CX + 1)) and
      (target.CY >= (self.CY - 1)) and (target.CY <= (self.CY + 1)) and
      not ((target.CX = self.CX) and (target.CY = self.CY)) then begin
      Result := TRUE;
      // 确定目标方向
      while TRUE do begin
         if (target.CX = (self.CX - 1)) and (target.CY = self.CY) then begin
            targdir := DR_LEFT;
            break;
         end;
         if (target.CX = (self.CX + 1)) and (target.CY = self.CY) then begin
            targdir := DR_RIGHT;
            break;
         end;
         if (target.CX = self.CX) and (target.CY = (self.CY - 1)) then begin
            targdir := DR_UP;
            break;
         end;
         if (target.CX = self.CX) and (target.CY = (self.CY + 1)) then begin
            targdir := DR_DOWN;
            break;
         end;
         if (target.CX = self.CX - 1) and (target.CY = self.CY - 1) then begin
            targdir := DR_UPLEFT;
            break;
         end;
         if (target.CX = self.CX + 1) and (target.CY = self.CY - 1) then begin
            targdir := DR_UPRIGHT;
            break;
         end;
         if (target.CX = self.CX - 1) and (target.CY = self.CY + 1) then begin
            targdir := DR_DOWNLEFT;
            break;
         end;
         if (target.CX = self.CX + 1) and (target.CY = self.CY + 1) then begin
            targdir := DR_DOWNRIGHT;
            break;
         end;
         targdir := 0;  // 异常
         break;
      end;
   end;
end;

{ TargetInSpitRange - 目标在喷射范围内
  功能: 检查目标是否在喷射攻击范围内(2格)
  参数:
    target - 目标生物
    targdir - 目标方向(输出)
  返回值: 是否在范围内 }
function  TCreature.TargetInSpitRange (target: TCreature; var targdir: byte): Boolean;
var
   nx, ny: integer;  // 相对坐标
begin
   Result := FALSE;
   if (abs(target.CX-CX) <= 2) and (abs(target.CY-CY) <= 2) then begin
      nx := target.CX - CX;
      ny := target.CY - CY;
      // 相邻格内
      if (abs(nx) <= 1) and (abs(ny) <= 1) then begin
         TargetInAttackRange (target, targdir);
         Result := TRUE;
      end else begin
         // 2格范围内
         nx := nx + 2;
         ny := ny + 2;
         if (nx in [0..4]) and (ny in [0..4]) then begin
            targdir := GetNextDirection (CX, CY, target.CX, target.CY);
            if SpitMap[targdir, ny, nx] = 1 then begin
               Result := TRUE;
            end;
         end;
      end;
   end;
end;

{ TargetInCrossRange - 目标在十字范围内
  功能: 检查目标是否在十字攻击范围内(2格)
  参数:
    target - 目标生物
    targdir - 目标方向(输出)
  返回值: 是否在范围内 }
function  TCreature.TargetInCrossRange (target: TCreature; var targdir: byte): Boolean;
var
   nx, ny: integer;  // 相对坐标
begin
   Result := FALSE;
   if (abs(target.CX-CX) <= 2) and (abs(target.CY-CY) <= 2) then begin
      nx := target.CX - CX;
      ny := target.CY - CY;
      // 相邻格内
      if (abs(nx) <= 1) and (abs(ny) <= 1) then begin
         TargetInAttackRange (target, targdir);
         Result := TRUE;
      end else begin
         // 2格范围内
         nx := nx + 2;
         ny := ny + 2;
         if (nx in [0..4]) and (ny in [0..4]) then begin
            targdir := GetNextDirection (CX, CY, target.CX, target.CY);
            if CrossMap[targdir, ny, nx] = 1 then begin
               Result := TRUE;
            end;
         end;
      end;
   end;
end;


{ WalkTo - 行走到
  功能: 向指定方向行走一格
  参数:
    dir - 方向
    allowdup - 是否允许重叠
  返回值: 是否成功 }
function  TCreature.WalkTo (dir: integer; allowdup: Boolean): Boolean;
var
   prx, pry, nwx, nwy, masx, masy: integer;  // 旧坐标、新坐标、主人前方坐标
   hum: TUserHuman;        // 玩家对象
   oldpenvir: TEnvirnoment;  // 旧地图
   flag: Boolean;          // 标志
begin
   Result := FALSE;
   // 神圣战甲状态不能移动
   if BoHolySeize then begin
      exit;
   end;

   try
      prx := CX;
      pry := CY;
      oldpenvir := PEnvir;
      self.Dir := dir;
      nwx := 0; nwy := 0;
      // 根据方向计算新坐标
      case dir of
         DR_UP:      begin  nwx := CX;  nwy := CY-1; end;
         DR_DOWN:    begin  nwx := CX;  nwy := CY+1; end;
         DR_LEFT:    begin  nwx := CX-1;  nwy := CY; end;
         DR_RIGHT:   begin  nwx := CX+1;  nwy := CY; end;
         DR_UPLEFT:  begin  nwx := CX-1;  nwy := CY-1; end;
         DR_UPRIGHT: begin  nwx := CX+1;  nwy := CY-1; end;
         DR_DOWNLEFT:  begin  nwx := CX-1;  nwy := CY+1; end;
         DR_DOWNRIGHT: begin  nwx := CX+1;  nwy := CY+1; end;
      end;
      if (nwx >= 0) and (nwx <= PEnvir.MapWidth-1) and (nwy >= 0) and (nwy <= PEnvir.MapHeight-1) then begin
         flag := TRUE;
         // 怕火的怪物不能走向火焰
         if BoFearFire then
            if not PEnvir.CanSafeWalk (nwx, nwy) then
               flag := FALSE;
         // 召唤兽不能挡住主人前方
         if Master <> nil then begin
            GetNextPosition (Master.PEnvir, Master.CX, Master.CY, Master.Dir, 1, masx, masy);
            if (nwx = masx) and (nwy = masy) then
               flag := FALSE;
         end;
         if flag then
            if PEnvir.MoveToMovingObject (CX, CY, self, nwx, nwy, allowdup) > 0 then begin
               CX := nwx;
               CY := nwy;
            end;
      end;

      if (prx <> CX) or (pry <> CY) then begin
         if Walk(RM_WALK) then begin
            // 移动时解除隐身术
            if BoFixedHideMode then begin
               if BoHumHideMode then begin
                  StatusArr[STATE_TRANSPARENT] := 1;
               end;
            end;

            Result := TRUE;
         end else begin
            PEnvir.DeleteFromMap (CX, CY, OS_MOVINGOBJECT, self);
            PEnvir := oldpenvir;
            CX := prx;
            CY := pry;
            PEnvir.AddToMap (CX, CY, OS_MOVINGOBJECT, self);
         end
      end;
   except
      MainOutMessage ('[Exception] TCreatre.WalkTo');
   end;
end;

{ RunTo - 跑到
  功能: 向指定方向跑两格
  参数:
    dir - 方向
    allowdup - 是否允许重叠
  返回值: 是否成功 }
function  TCreature.RunTo (dir: integer; allowdup: Boolean): Boolean;
var
   prx, pry: integer;  // 旧坐标
begin
   Result := FALSE;
   try
      prx := CX;
      pry := CY;
      self.Dir := dir;
      // 根据方向跑两格
      case dir of
         DR_UP:
            begin
               if CY > 1 then
                  if PEnvir.CanWalk(CX, CY-1, allowdup) and (PEnvir.CanWalk(CX, CY-2, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX, CY-2, TRUE) > 0 then begin
                        CY := CY - 2;
                     end;
            end;
         DR_DOWN:
            begin
               if CY < PEnvir.MapHeight-2 then
                  if PEnvir.CanWalk(CX, CY+1, allowdup) and (PEnvir.CanWalk(CX, CY+2, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX, CY+2, TRUE) > 0 then begin
                        CY := CY + 2;
                     end;
            end;
         DR_LEFT:
            begin
               if CX > 1 then
                  if PEnvir.CanWalk(CX-1, CY, allowdup) and (PEnvir.CanWalk(CX-2, CY, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX-2, CY, TRUE) > 0 then begin
                        CX := CX - 2;
                     end;
            end;
         DR_RIGHT:
            begin
               if CX < PEnvir.MapWidth-2 then
                  if PEnvir.CanWalk(CX+1, CY, allowdup) and (PEnvir.CanWalk(CX+2, CY, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX+2, CY, TRUE) > 0 then begin
                        CX := CX + 2;
                     end;
            end;
         DR_UPLEFT:
            begin
               if (CX > 1) and (CY > 1) then
                  if PEnvir.CanWalk(CX-1, CY-1, allowdup) and (PEnvir.CanWalk(CX-2, CY-2, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX-2, CY-2, TRUE) > 0 then begin
                        CX := CX - 2;
                        CY := CY - 2;
                     end;
            end;
         DR_UPRIGHT:
            begin
               if (CX < PEnvir.MapWidth-2) and (CY > 1) then
                  if PEnvir.CanWalk(CX+1, CY-1, allowdup) and (PEnvir.CanWalk(CX+2, CY-2, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX+2, CY-2, TRUE) > 0 then begin
                        CX := CX + 2;
                        CY := CY - 2;
                     end;
            end;
         DR_DOWNLEFT:
            begin
               if (CX > 1) and (CY < PEnvir.MapHeight-2) then
                  if PEnvir.CanWalk(CX-1, CY+1, allowdup) and (PEnvir.CanWalk(CX-2, CY+2, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX-2, CY+2, TRUE) > 0 then begin
                        CX := CX - 2;
                        CY := CY + 2;
                     end;
            end;
         DR_DOWNRIGHT:
            begin
               if (CX < PEnvir.MapWidth-2) and (CY < PEnvir.MapHeight-2) then
                  if PEnvir.CanWalk(CX+1, CY+1, allowdup) and (PEnvir.CanWalk(CX+2, CY+2, allowdup)) then
                     if PEnvir.MoveToMovingObject (CX, CY, self, CX+2, CY+2, TRUE) > 0 then begin
                        CX := CX + 2;
                        CY := CY + 2;
                     end;
            end;
      end;

      if (prx <> CX) or (pry <> CY) then begin
         if Walk(RM_RUN) then begin
            Result := TRUE;
         end else begin
            // 失败时恢复坐标
            CX := prx;
            CY := pry;
            PEnvir.MoveToMovingObject (prx, pry, self, CX, CY, TRUE);
         end
      end;

   except
      MainOutMessage ('[Exception] TCreature.RunTo');
   end;
end;

{ IsEnoughBag - 背包是否足够
  功能: 检查背包是否有空位
  返回值: 是否有空位 }
function  TCreature.IsEnoughBag: Boolean;
begin
   if Itemlist.Count < MAXBAGITEM then Result := TRUE
   else Result := FALSE;
end;

{ WeightChanged - 重量变化
  功能: 重新计算重量并通知客户端 }
procedure TCreature.WeightChanged;
begin
   WAbil.Weight := CalcBagWeight;
   UpdateMsg (self, RM_WEIGHTCHANGED, 0, 0, 0, 0, '');
end;

{ GoldChanged - 金币变化
  功能: 通知客户端金币变化 }
procedure TCreature.GoldChanged;
begin
   if RaceServer = RC_USERHUMAN then
      UpdateMsg (self, RM_GOLDCHANGED, 0, 0, 0, 0, '');
end;

{ HealthSpellChanged - 生命魔法变化
  功能: 通知客户端生命魔法变化 }
procedure TCreature.HealthSpellChanged;
begin
   if RaceServer = RC_USERHUMAN then
      UpdateMsg (self, RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
   // 开启血量显示时广播
   if BoOpenHealth then
      SendRefMsg (RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
end;

{ IsAddWeightAvailable - 是否可以增加重量
  功能: 检查是否可以增加指定重量
  参数:
    addweight - 要增加的重量
  返回值: 是否可以 }
function  TCreature.IsAddWeightAvailable (addweight: integer): Boolean;
begin
   if WAbil.Weight + addweight <= WAbil.MaxWeight then Result := TRUE
   else Result := FALSE;
end;

{ FindItemName - 查找物品名称
  功能: 在背包中查找指定名称的物品
  参数:
    iname - 物品名称
  返回值: 物品指针或nil }
function  TCreature.FindItemName (iname: string): PTUserItem;
var
   i: integer;  // 循环变量
begin
   Result := nil;
   for i:=0 to ItemList.Count-1 do begin
      if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), iname) = 0 then begin
         Result := PTUserItem(ItemList[i]);
         break;
      end;
   end;
end;

{ FindItemNameEx - 查找物品名称(扩展)
  功能: 在背包中查找指定名称的物品并返回统计信息
  参数:
    iname - 物品名称
    count - 数量(输出)
    durasum - 耐久总和(输出)
    duratop - 最高耐久(输出)
  返回值: 最高耐久的物品指针 }
function  TCreature.FindItemNameEx (iname: string; var count, durasum, duratop: integer): PTUserItem;
var
   i: integer;  // 循环变量
begin
   Result := nil;
   durasum := 0;
   duratop := 0;
   count := 0;
   for i:=0 to ItemList.Count-1 do begin
      if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), iname) = 0 then begin
         // 记录最高耐久的物品
         if PTUserItem(ItemList[i]).Dura > duratop then begin
            duratop := PTUserItem(ItemList[i]).Dura;
            Result := PTUserItem(ItemList[i]);
         end;
         durasum := durasum + PTUserItem(ItemList[i]).Dura;
         if Result = nil then
            Result := PTUserItem(ItemList[i]);
         Inc (count);
      end;
   end;
end;

{ FindItemWear - 查找装备物品
  功能: 在装备栏中查找指定名称的物品
  参数:
    iname - 物品名称
    count - 数量(输出)
  返回值: 物品指针或nil }
function  TCreature.FindItemWear (iname: string; var count: integer): PTUserItem;
var
   i: integer;  // 循环变量
begin
   Result := nil;
   count := 0;
   for i:=0 to 8 do begin
      if CompareText (UserEngine.GetStdItemName (UseItems[i].Index), iname) = 0 then begin
         Result := @(UseItems[i]);
         Inc (count);
      end;
   end;
end;

{ CanAddItem - 是否可以添加物品
  功能: 检查背包是否有空位
  返回值: 是否可以 }
function  TCreature.CanAddItem: Boolean;
begin
   Result := FALSE;
   if Itemlist.Count < MAXBAGITEM then
      Result := TRUE;
end;

{ AddItem - 添加物品
  功能: 向背包添加物品
  参数:
    pu - 物品指针(已分配内存)
  返回值: 是否成功 }
function  TCreature.AddItem (pu: PTUserItem): Boolean;
begin
   Result := FALSE;
   if Itemlist.Count < MAXBAGITEM then begin
      Itemlist.Add (pu);
      WeightChanged;
      Result := TRUE;
   end;
end;

{ DelItem - 删除物品
  功能: 从背包中删除指定物品
  参数:
    svindex - 物品制造索引
    iname - 物品名称
  返回值: 是否成功 }
function  TCreature.DelItem (svindex: integer; iname: string): Boolean;
var
   i: integer;  // 循环变量
begin
   Result := FALSE;
   for i:=0 to ItemList.Count-1 do begin
      if PTUserItem(ItemList[i]).MakeIndex = svindex then begin
         if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), iname) = 0 then begin
            Dispose (PTUserItem(ItemList[i]));
            ItemList.Delete (i);
            Result := TRUE;
            break;
         end;
      end;
   end;
   if Result then
      WeightChanged;
end;

{ DelItemIndex - 删除物品索引
  功能: 根据索引删除背包物品
  参数:
    bagindex - 背包索引
  返回值: 是否成功 }
function  TCreature.DelItemIndex (bagindex: integer): Boolean;
var
   i: integer;  // 循环变量
begin
   Result := FALSE;
   if (bagindex >= 0) and (bagindex < ItemList.Count) then begin
      Dispose (PTUserItem(ItemList[bagindex]));
      ItemList.Delete (bagindex);
   end;
end;

{ DeletePItemAndSend - 删除物品并发送
  功能: 删除物品并通知客户端
  参数:
    pcheckitem - 物品指针
  返回值: 是否成功 }
function  TCreature.DeletePItemAndSend (pcheckitem: PTUserItem): Boolean;
var
   i: integer;      // 循环变量
   hum: TUserHuman; // 玩家对象
begin
   Result := FALSE;
   // 检查背包
   for i:=0 to ItemList.Count-1 do begin
      if ItemList[i] = pcheckitem then begin
         if RaceServer = RC_USERHUMAN then begin
            hum := TUserHuman(self);
            hum.SendDelItem (PTUserItem(ItemList[i])^);
         end;
         Dispose (PTUserItem(ItemList[i]));
         ItemList.Delete (i);
         Result := TRUE;
         exit;
      end;
   end;
   // 检查装备栏
   for i:=0 to 8 do begin
      if @(UseItems[i]) = pcheckitem then begin
         if RaceServer = RC_USERHUMAN then begin
            hum := TUserHuman(self);
            hum.SendDelItem (UseItems[i]);
         end;
         UseItems[i].Index := 0;
         Result := TRUE;
      end;
   end;
end;

{ CanTakeOn - 是否可以穿戴
  功能: 检查是否可以穿戴指定物品
  参数:
    index - 装备位置
    ps - 标准物品指针
  返回值: 是否可以
  实现原理: 检查性别、等级、职业、重量等条件 }
function  TCreature.CanTakeOn (index: integer; ps: PTStdItem): Boolean;
begin
   Result := FALSE;
   // 检查性别 - 男装
   if ps.StdMode = 10 then
      if Sex <> 0 then begin
         SysMsg ('只有男性可以穿戴。', 0);
         exit;
      end;
   // 检查性别 - 女装
   if ps.StdMode = 11 then
      if Sex <> 1 then begin
         SysMsg ('只有女性可以穿戴。', 0);
         exit;
      end;

   // 检查重量
   if (index = U_WEAPON) or (index = U_RIGHTHAND) then begin
      // 检查手持重量
      if ps.Weight > WAbil.MaxHandWeight then begin
         SysMsg ('重量超过了', 0);
         exit;
      end;
   end else begin
      // 检查穿戴重量
      if ps.Weight + CalcWearWeightEx (index) > WAbil.MaxWearWeight then begin
         SysMsg ('重量超过了', 0);
         exit;
      end;
   end;

   // 检查需求条件
   case ps.Need of
      0: // 等级检查
         begin
            if Abil.Level >= ps.NeedLevel then
               Result := TRUE;
         end;
      1: //DC
         begin
            if Hibyte(WAbil.DC) >= ps.NeedLevel then
               Result := TRUE;
         end;
      2: //MC
         begin
            if Hibyte(WAbil.MC) >= ps.NeedLevel then
               Result := TRUE;
         end;
      3: //SC
         begin
            if Hibyte(WAbil.SC) >= ps.NeedLevel then
               Result := TRUE;
         end;
   end;
   if not Result then
      SysMsg ('不符合你的要求。', 0);
end;

{ GetDropPosition - 获取掉落位置
  功能: 获取物品掉落的位置
  参数:
    x, y - 中心坐标
    wide - 搜索范围
    dx, dy - 掉落坐标(输出)
  返回值: 是否找到空位 }
function  TCreature.GetDropPosition (x, y, wide: integer; var dx, dy: integer): Boolean;
var
   i, j, k, dropcount, icount, ssx, ssy: integer;  // 循环、掉落数、临时坐标
   pm: PTMapItem;  // 地图物品指针
begin
   icount := 999;
   Result := FALSE;
   ssx    := dx;
   ssy    := dy;
   // 从内向外搜索空位
   for k:=1 to wide do begin
      for j:=-k to k do begin
         for i:=-k to k do begin
            dx := x + i;
            dy := y + j;
            if PEnvir.GetItemEx (dx, dy, dropcount) = nil then begin
               if PEnvir.BoCanGetItem then begin
                  Result := TRUE;
                  break;
               end;
            end else begin
               // 记录物品最少的位置
               if PEnvir.BoCanGetItem then begin
                  if icount > dropcount then begin
                     icount := dropcount;
                     ssx := dx;
                     ssy := dy;
                  end;
               end;
            end;
         end;
         if Result then break;
      end;
      if Result then break;
   end;
   // 找不到空位时使用物品最少的位置
   if not Result then begin
      if icount < 8 then begin
         dx := ssx;
         dy := ssy;
      end else begin
         dx := x;
         dy := y;
      end;
   end;
end;

{ GetRecallPosition - 获取召回位置
  功能: 获取召唤兽召回的位置
  参数:
    x, y - 中心坐标
    wide - 搜索范围
    dx, dy - 召回坐标(输出)
  返回值: 是否找到空位 }
function  TCreature.GetRecallPosition (x, y, wide: integer; var dx, dy: integer): Boolean;
var
   i, j, k: integer;  // 循环变量
   pm: PTMapItem;     // 地图物品指针
begin
   Result := FALSE;
   // 先检查中心位置
   if PEnvir.GetCreature (x, y, TRUE) = nil then begin
      Result := TRUE;
      dx := x;
      dy := y;
   end;
   // 从内向外搜索空位
   if not Result then begin
      for k:=1 to wide do begin
         for j:=-k to k do begin
            for i:=-k to k do begin
               dx := x + i;
               dy := y + j;
               if PEnvir.GetCreature (dx, dy, TRUE) = nil then begin
                  Result := TRUE;
                  break;
               end;
            end;
            if Result then break;
         end;
         if Result then break;
      end;
   end;
   // 找不到空位时使用中心位置
   if not Result then begin
      dx := x;
      dy := y;
   end;
end;

{ DropItemDown - 掉落物品
  功能: 将物品掉落到地上
  参数:
    ui - 用户物品
    scatterrange - 散落范围
    diedrop - 是否死亡掉落
    ownership - 所有权
    droper - 掉落者
  返回值: 是否成功 }
function  TCreature.DropItemDown (ui: TUserItem; scatterrange: integer; diedrop: Boolean; ownership, droper: TObject): Boolean;
var
   dx, dy, idura: integer;  // 坐标、耐久
   pmi, pr: PTMapItem;      // 地图物品指针
   ps: PTStdItem;           // 标准物品指针
   logcap: string;          // 日志标题
begin
   Result := FALSE;
   ps := UserEngine.GetStdItem (ui.Index);
   if ps <> nil then begin
      // 肉类掉落时品质下降
      if ps.StdMode = 40 then begin
         idura := ui.Dura;
         idura := idura - 2000;
         if idura < 0 then idura := 0;
         ui.Dura := idura;
      end;

      new (pmi);
      pmi.UserItem := ui;
      pmi.Name := ps.Name;
      pmi.Looks := ps.Looks;
      // 骰子、木材等随机外观
      if ps.StdMode = 45 then begin
         pmi.Looks := GetRandomLook (pmi.Looks, ps.Shape);
      end;
      pmi.AniCount := ps.AniCount;
      pmi.Reserved := 0;
      pmi.Count := 1;
      pmi.Ownership := ownership;
      pmi.Droptime := GetTickCount;
      pmi.Droper := droper;

      GetDropPosition (CX, CY, scatterrange, dx, dy);
      // 添加到地图(每格最多5个物品)
      pr := Penvir.AddToMap (dx, dy, OS_ITEMOBJECT, TObject (pmi));
      if pr = pmi then begin
         SendRefMsg (RM_ITEMSHOW, pmi.Looks, integer(pmi), dx, dy, pmi.Name);
         // 记录日志
         if diedrop then logcap := '15'#9  // 死亡掉落
         else logcap := '7'#9;  // 丢弃
         if not IsCheapStuff (ps.StdMode) then
            AddUserLog (logcap +
                        MapName + ''#9 +
                        IntToStr(CX) + ''#9 +
                        IntToStr(CY) + ''#9 +
                        UserName + ''#9 +
                        UserEngine.GetStdItemName (ui.Index) + ''#9 +
                        IntToStr(ui.MakeIndex) + ''#9 +
                        IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                        '0');
         Result := TRUE;
      end else begin
         // 失败时释放内存
         Dispose (pmi);
      end;
   end;
end;

{ DropGoldDown - 掉落金币
  功能: 将金币掉落到地上
  参数:
    goldcount - 金币数量
    diedrop - 是否死亡掉落
    ownership - 所有权
    droper - 掉落者
  返回值: 是否成功
  注意: 不会减少生物的金币 }
function  TCreature.DropGoldDown (goldcount: integer; diedrop: Boolean; ownership, droper: TObject): Boolean;
var
   dx, dy: integer;     // 坐标
   pmi, pr: PTMapItem;  // 地图物品指针
   ps: PTStdItem;       // 标准物品指针
   logcap: string;      // 日志标题
begin
   Result := FALSE;
   new (pmi);
   FillChar (pmi^, sizeof(TMapItem), #0);
   pmi.Name := '쏜귑';
   pmi.Count := goldcount;
   pmi.Looks := GetGoldLooks (goldcount);
   pmi.Ownership := ownership;
   pmi.Droptime := GetTickCount;
   pmi.Droper := droper;

   GetDropPosition (CX, CY, 3, dx, dy);
   pr := PEnvir.AddToMap (dx, dy, OS_ITEMOBJECT, TObject (pmi));
   if pr <> nil then begin
      if pr <> pmi then begin
         Dispose (pmi);
         pmi := pr;
      end;
      SendRefMsg (RM_ITEMSHOW, pmi.Looks, integer(pmi), dx, dy, '쏜귑');
      //떨어뜨림
      if RaceServer = RC_USERHUMAN then begin
         if diedrop then logcap := '15'#9  //떨굼_
         else logcap := '7'#9;  //버림_
         AddUserLog (logcap +
                     MapName + ''#9 +
                     IntToStr(CX) + ''#9 +
                     IntToStr(CY) + ''#9 +
                     UserName + ''#9 +
                     '쏜귑' + ''#9 +
                     IntToStr(goldcount) + ''#9 +
                     IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                     '0');
      end;
      Result := TRUE;
   end else begin
      //실패인경우
      Dispose (pmi);
   end;
end;

{ UserDropItem - 用户丢弃物品
  功能: 将物品丢弃到地上
  参数:
    itmname - 物品名称
    itemindex - 物品制造索引
  返回值: 是否成功 }
function  TCreature.UserDropItem (itmname: string; itemindex: integer): Boolean;
var
   i: integer;        // 循环变量
   pu: PTUserItem;    // 用户物品指针
   pstd: PTStdItem;   // 标准物品指针
begin
   Result := FALSE;
   if pos(' ', itmname) >= 0 then
      GetValidStr3 (itmname, itmname, [' ']);
   // 防止交易窗口关闭后误丢物品
   if GetTickCount - DealItemChangeTime > 3000 then begin
      for i:=0 to ItemList.Count-1 do begin
         pu := PTUserItem(ItemList[i]);
         pstd := UserEngine.GetStdItem (pu.Index);
         if pstd = nil then continue;
         // 活动物品不能丢弃
         if pstd.StdMode <> TAIWANEVENTITEM then begin
            if (pu.MakeIndex = itemindex) then begin
               if CompareText (UserEngine.GetStdItemName (pu.Index), itmname) = 0 then begin
                  if DropItemDown (pu^, 1, FALSE, nil, self) then begin
                     Dispose (PTUserItem(ItemList[i]));
                     ItemList.Delete (i);
                     Result := TRUE;
                     break;
                  end;
               end;
            end;
         end;
      end;
      if Result then
         WeightChanged;
   end;
end;

{ UserDropGold - 用户丢弃金币
  功能: 将金币丢弃到地上
  参数:
    dropgold - 丢弃数量
  返回值: 是否成功 }
function  TCreature.UserDropGold (dropgold: integer): Boolean;
begin
   Result := FALSE;
   if ( dropgold > 0 ) and ( dropgold <= Gold) then begin
      Gold := Gold - dropgold;
      // 失败时恢复金币
      if not DropGoldDown (dropgold, FALSE, nil, self) then
         Gold := Gold + dropgold;
      GoldChanged;
      Result := true;
   end;
end;

{ PickUp - 拾取
  功能: 拾取地上的物品或金币
  返回值: 是否成功 }
function  TCreature.PickUp: Boolean;
   // 检查是否可以拾取
   function canpickup (ownership: TObject): Boolean;
   begin
      if (ownership = nil) or (ownership = self) then
         Result := TRUE
      else
         Result := FALSE;
   end;
   // 检查组队成员是否可以拾取
   function cangrouppickup (ownership: TObject): Boolean;
   var
      i: integer;
      cret: TCreature;
   begin
      Result := FALSE;
      if GroupOwner <> nil then
         for i:=0 to GroupOwner.GroupMembers.Count-1 do begin
            cret := TCreature(GroupOwner.GroupMembers.Objects[i]);
            if (cret = ownership) then begin
               Result := TRUE;
               break;
            end;
         end;
   end;
var
   i: integer;          // 循环变量
   pu: PTUserItem;      // 用户物品指针
   pmi: PTMapItem;      // 地图物品指针
   ps: PTStdItem;       // 标准物品指针
   hum: TUserHuman;     // 玩家对象
   questnpc: TMerchant; // 任务NPC
   dropername: string;  // 掉落者名称
begin
   Result := FALSE;
   // 交易中不能拾取
   if BoDealing then exit;
   hum := nil;
   pmi := PEnvir.GetItem (CX, CY);
   if pmi <> nil then begin
      // 超时后取消所有权
      if (GetTickCount - pmi.droptime > ANTI_MUKJA_DELAY) then
         pmi.ownership := nil;

      if canpickup (pmi.ownership) or cangrouppickup(pmi.ownership) then begin
         if CompareText (pmi.Name, '쏜귑') = 0 then begin
            if PEnvir.DeleteFromMap (CX, CY, OS_ITEMOBJECT, TObject(pmi)) = 1 then begin
               if IncGold (pmi.Count) then begin
                  SendRefMsg (RM_ITEMHIDE, 0, integer(pmi), CX, CY, '');
                  //로그남김
                  AddUserLog ('4'#9 + //줍기_
                              MapName + ''#9 +
                              IntToStr(CX) + ''#9 +
                              IntToStr(CY) + ''#9 +
                              UserName + ''#9 +
                              '쏜귑'#9 +
                              IntToStr(pmi.count) + ''#9 +
                              '1'#9 +
                              '0');
                  GoldChanged;
                  Dispose (pmi);
               end else
                  PEnvir.AddToMap (CX, CY, OS_ITEMOBJECT, TObject(pmi));
            end;
         end else
            if IsEnoughBag then begin
               if PEnvir.DeleteFromMap (CX, CY, OS_ITEMOBJECT, TObject(pmi)) = 1 then begin
                  new (pu);
                  pu^ := pmi.UserItem;
                  ps := UserEngine.GetStdItem (pu.Index);
                  if (ps <> nil) and IsAddWeightAvailable (UserEngine.GetStdItemWeight(pu.Index)) then begin
                     SendMsg (self, RM_ITEMHIDE, 0, integer(pmi), CX, CY, '');
                     AddItem (pu);

                     //맵퀘스트가 있는지
                     if PEnvir.HasMapQuest then begin
                        dropername := '';
                        if pmi.Droper <> nil then
                           dropername := TCreature (pmi.Droper).UserName;
                        questnpc := TMerchant (PEnvir.GetMapQuest (self, dropername, ps.Name, FALSE));
                        if questnpc <> nil then
                           questnpc.UserCall (self);
                     end;


                     //로그남김
                     if not IsCheapStuff (ps.StdMode) then begin
                        AddUserLog ('4'#9 + //줍기
                                    MapName + ''#9 +
                                    IntToStr(CX) + ''#9 +
                                    IntToStr(CY) + ''#9 +
                                    UserName + ''#9 +
                                    UserEngine.GetStdItemName (pu.Index) + ''#9 +
                                    IntToStr(pu.MakeIndex) + ''#9 +
                                    '1'#9 +
                                    '0');
                     end;

                     if RaceServer = RC_USERHUMAN then begin
                        if self is TUserHuman then begin
                           hum := TUserHuman(self);
                           TUserHuman(hum).SendAddItem (pu^);
                        end;
                     end;

                     if ps.StdMode = TAIWANEVENTITEM then begin  //대만 이벤트, 이벤트 아이템을 주으면 표시남
                        if hum <> nil then begin
                        hum.TaiwanEventItemName := ps.Name;
                        hum.BoTaiwanEventUser := TRUE;
                        //캐릭의 색깔을 바꾼다.
                        StatusArr[STATE_BLUECHAR] := 60000;  //타임 아웃 없음;
                        CharStatus := GetCharStatus;
                        CharStatusChanged;
                        Light := GetMyLight;
                        SendRefMsg (RM_CHANGELIGHT, 0, 0, 0, 0, '');
                        UserNameChanged;
                     end;
                     end;

                     Dispose (pmi);
                     Result := TRUE;
                  end else begin
                     Dispose (pu);
                     PEnvir.AddToMap (CX, CY, OS_ITEMOBJECT, TObject(pmi));
                  end;
               end;
            end;
      end else begin
         SysMsg ('这个物品有主人，不能拾取。', 0);
      end;
   end;
end;

{ EatItem - 使用物品
  功能: 使用药品、食物、卷轴等物品
  参数:
    std - 标准物品
    pu - 用户物品指针
  返回值: 是否成功 }
function  TCreature.EatItem (std: TStdItem; pu: PTUserItem): Boolean;
var
   boneedrecalc: Boolean;  // 是否需要重算能力
begin
   Result := FALSE;
   // 禁药地图
   if PEnvir.NoDrug then begin
      SysMsg ('这个地图不能使用药品。', 0);
      exit;
   end;
   case std.StdMode of
      0: // 药品类
         begin
            case std.Shape of
               FASTFILL_ITEM: // 仙水
                  begin
                     // 恢复生命和法力
                     IncHealthSpell (std.AC, std.MAC);
                     Result := TRUE;
                  end;
               FREE_UNKNOWN_ITEM: // 解除未知物品
                  begin
                     // 解除未知物品的效果
                     BoNextTimeFreeCurseItem := TRUE;
                     Result := TRUE;
                  end;
               else
                  begin
                     // 普通药品恢复
                     if (IncHealth + std.AC < 500) and (std.AC > 0) then begin
                        IncHealth := IncHealth + std.AC;
                     end;
                     if (IncSpell + std.MAC < 500) and (std.MAC > 0) then begin
                        IncSpell := IncSpell + std.MAC;
                     end;
                     Result := TRUE;
                  end;
            end;
         end;
      1: // 肉类
         begin
            // 使用肉类物品
            Result := TRUE;
         end;
      2: // 餐厅食物
         begin
            // 使用餐厅食物
            Result := TRUE;
         end;
      3: // 卷轴类
         begin
            case std.Shape of
               INSTANTABILUP_DRUG: // 能力提升药水
                  begin
               boneedrecalc := FALSE;
               if lobyte(std.DC) > 0 then begin  //파괴 상승 물약
                  ExtraAbil[EABIL_DCUP] := lobyte(std.DC);
                  ExtraAbilTimes[EABIL_DCUP] := GetTickCount + hibyte(std.MAC) * 1000; //초단위
                  SysMsg ('묑샌제董珂藤속:' + IntToStr(hibyte(std.MAC)) + '취', 1);
                  boneedrecalc := TRUE;
               end;
               if lobyte(std.MC) > 0 then begin  //마력 상승 물약
                  ExtraAbil[EABIL_MCUP] := lobyte(std.MC);
                  ExtraAbilTimes[EABIL_MCUP] := GetTickCount + hibyte(std.MAC) * 1000; //초단위
                  SysMsg ('침랬제董珂藤속:' + IntToStr(hibyte(std.MAC)) + '취', 1);
                  boneedrecalc := TRUE;
               end;
               if lobyte(std.SC) > 0 then begin  //도력 상승 물약
                  ExtraAbil[EABIL_SCUP] := lobyte(std.SC);
                  ExtraAbilTimes[EABIL_SCUP] := GetTickCount + hibyte(std.MAC) * 1000; //초단위
                  SysMsg ('쑹�제董珂藤속:' + IntToStr(hibyte(std.MAC)) + '취', 1);
                  boneedrecalc := TRUE;
               end;
               if hibyte(std.AC) > 0 then begin  //묑샌醵똑 상승 물약
                  ExtraAbil[EABIL_HITSPEEDUP] := hibyte(std.AC);
                  ExtraAbilTimes[EABIL_HITSPEEDUP] := GetTickCount + hibyte(std.MAC) * 1000; //초단위
                  SysMsg ('묑샌醵똑董珂藤속:' + IntToStr(hibyte(std.MAC)) + '취', 1);
                  boneedrecalc := TRUE;
               end;
               if lobyte(std.AC) > 0 then begin  //체력 상승 물약
                  ExtraAbil[EABIL_HPUP] := lobyte(std.AC);
                  ExtraAbilTimes[EABIL_HPUP] := GetTickCount + hibyte(std.MAC) * 1000; //초단위
                  SysMsg ('HP令董珂藤속:' + IntToStr(hibyte(std.MAC)) + '취', 1);
                  boneedrecalc := TRUE;
               end;
               if lobyte(std.MAC) > 0 then begin  //마력 상승 물약
                  ExtraAbil[EABIL_MPUP] := lobyte(std.MAC);
                  ExtraAbilTimes[EABIL_MPUP] := GetTickCount + hibyte(std.MAC) * 1000; //초단위
                  SysMsg ('MP令董珂藤속:' + IntToStr(hibyte(std.MAC)) + '취', 1);
                  boneedrecalc := TRUE;
               end;
               if boneedrecalc then begin
                  RecalcAbilitys;
                  SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
               end;
               Result := TRUE;
                  end;
               INSTANT_EXP_DRUG:
                  begin
                     WinExp (lobyte(std.AC) * 100);
                     Result := TRUE;
                  end;
               else
               Result := UseScroll (std.Shape);
            end;
         end;
   end;
end;

{ IsMyMagic - 是否是我的魔法
  功能: 检查是否已学会指定魔法
  参数:
    magid - 魔法ID
  返回值: 是否已学会 }
function  TCreature.IsMyMagic (magid: integer): Boolean;
var
   i: integer;  // 循环变量
begin
   Result := FALSE;
   for i:=0 to MagicList.Count-1 do begin
      if PTUserMagic(MagicList[i]).MagicId = magid then begin
         Result := TRUE;
         break;
      end;
   end;
end;

{ ReadBook - 阅读技能书
  功能: 学习新魔法/技能
  参数:
    std - 标准物品
  返回值: 是否成功 }
function  TCreature.ReadBook (std: TStdItem): Boolean;
var
   pdm: PTDefMagic;   // 魔法定义指针
   pum: PTUserMagic;  // 用户魔法指针
   hum: TUserHuman;   // 玩家对象
begin
   Result := FALSE;
   pdm := UserEngine.GetDefMagic (std.Name);
   if pdm <> nil then begin
      // 检查是否已学会
      if not IsMyMagic (pdm.MagicId) then begin
         // 检查职业和等级要求
         if ((pdm.Job = 99) or (pdm.Job = Job)) and (Abil.Level >= pdm.NeedLevel[0]) then begin
            new (pum);
            pum.pDef := pdm;
            pum.MagicId := pdm.MagicId;
            pum.Key := #0;
            pum.Level := 0; 
            pum.CurTrain := 0;
            MagicList.Add (pum);  // 添加魔法
            RecalcAbilitys;
            if RaceServer = RC_USERHUMAN then begin
               hum := TUserHuman (self);
               hum.SendAddMagic (pum);  // 通知客户端
            end;
            Result := TRUE;
         end;
      end;
   end;
end;

{ GetSpellPoint - 获取魔法消耗
  功能: 计算魔法消耗的魔法值
  参数:
    pum - 用户魔法指针
  返回值: 魔法消耗值 }
function TCreature.GetSpellPoint (pum: PTUserMagic): integer;
begin
   Result := Round(pum.pDef.Spell / (pum.pDef.MaxTrainLevel+1) * (pum.Level+1))
             + pum.pDef.DefSpell;
end;

{ DoSpell - 施放魔法
  功能: 施放指定魔法
  参数:
    pum - 用户魔法指针
    xx, yy - 目标坐标
    target - 目标生物
  返回值: 是否成功 }
function  TCreature.DoSpell (pum: PTUserMagic; xx, yy: integer; target: TCreature): Boolean;
var
   spell: integer;  // 魔法消耗
begin
   Result := FALSE;
   // 剑法技能不在此处理
   if MagicMan.IsSwordSkill (pum.MagicId) then exit;

   // 检查魔法值是否足够
   spell := GetSpellPoint (pum);
   if (spell > 0) then begin
      if (WAbil.MP >= spell) then begin
         DamageSpell (spell);
         HealthSpellChanged;
      end else
         exit;  // 魔法不足
   end;

   Result := MagicMan.SpellNow (self, pum, xx, yy, target);
end;


{------------------------------ 魔法效果 -----------------------------}

{ MagPassThroughMagic - 穿透魔法
  功能: 从起点到终点的路径上攻击敌人
  参数:
    sx, sy - 起点坐标
    tx, ty - 终点坐标
    ndir - 方向
    magpwr - 魔法伤害
    undeadattack - 是否对亡灵加强
  返回值: 命中数量 }
function  TCreature.MagPassThroughMagic (sx, sy, tx, ty, ndir, magpwr: integer; undeadattack: Boolean): integer;
var
   i, tcount, acpwr: integer;  // 循环、命中数、实际伤害
   cret: TCreature;            // 生物对象
begin
   tcount := 0;
   for i:=0 to 12 do begin
      cret := TCreature (PEnvir.GetCreature (sx, sy, TRUE));
      if cret <> nil then begin
         if IsProperTarget (cret) then begin
            // 魔法抵抗判定
            if cret.AntiMagic <= Random(10) then begin
               // 对亡灵加强伤害
               if undeadattack then
                  acpwr := Round (magpwr * 1.5)
               else
                  acpwr := magpwr;
               cret.SendDelayMsg (self, RM_MAGSTRUCK, 0, acpwr, 0, 0, '', 600);
               Inc (tcount);
            end;
         end;
      end;
      // 移动到下一个位置
      if not ((abs(sx-tx) <= 0) and (abs(sy-ty) <= 0)) then begin
         ndir := GetNextDirection (sx, sy, tx, ty);
         if not GetNextPosition (PEnvir, sx, sy, ndir, 1, sx, sy) then
            break;
      end else
         break;
   end;
   Result := tcount;
end;

{ MagCanHitTarget - 魔法能否命中目标
  功能: 检查魔法是否能命中目标
  参数:
    sx, sy - 起点坐标
    target - 目标生物
  返回值: 是否能命中 }
function  TCreature.MagCanHitTarget (sx, sy: integer; target: TCreature): Boolean;
var
   i, ndir, dis, olddis: integer;  // 循环、方向、距离
begin
   Result := FALSE;
   if target <> nil then begin
      olddis := (abs(sx-Target.CX) + abs(sy-Target.CY));
      for i:=0 to 12 do begin
         ndir := GetNextDirection (sx, sy, target.CX, target.CY);
         if not GetNextPosition (PEnvir, sx, sy, ndir, 1, sx, sy) then
            break;
         // 检查是否可以飞过
         if not PEnvir.CanFireFly(sx, sy) then
            break;
         if (sx=target.CX) and (sy=target.CY) then begin
            Result := TRUE;
            break;
         end;
         dis := (abs(sx-Target.CX) + abs(sy-Target.CY));
         if dis > olddis then begin
            Result := TRUE;
            break;
         end;
         dis := olddis;
      end;
   end;
end;

{ MagDefenceUp - 防御提升
  功能: 提升防御力
  参数:
    sec - 持续时间(秒)
  返回值: 是否成功 }
function  TCreature.MagDefenceUp (sec: integer): Boolean;
begin
   Result := FALSE;
   if StatusArr[STATE_DEFENCEUP] > 0 then begin
      if sec > StatusArr[STATE_DEFENCEUP] then begin
         StatusArr[STATE_DEFENCEUP] := sec;
         Result := TRUE;
      end;
   end else begin
      StatusArr[STATE_DEFENCEUP] := sec;
      Result := TRUE;
   end;
   StatusTimes[STATE_DEFENCEUP] := GetTickCount;

   SysMsg ('防御提升' + IntTostr(sec) + '秒', 1);
   RecalcAbilitys;
   SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
end;

{ MagMagDefenceUp - 魔法防御提升
  功能: 提升魔法防御力
  参数:
    sec - 持续时间(秒)
  返回值: 是否成功 }
function  TCreature.MagMagDefenceUp (sec: integer): Boolean;
begin
   Result := FALSE;
   if StatusArr[STATE_MAGDEFENCEUP] > 0 then begin
      if sec > StatusArr[STATE_MAGDEFENCEUP] then begin
         StatusArr[STATE_MAGDEFENCEUP] := sec;
         Result := TRUE;
      end;
   end else begin
      StatusArr[STATE_MAGDEFENCEUP] := sec;
      Result := TRUE;
   end;
   StatusTimes[STATE_MAGDEFENCEUP] := GetTickCount;

   SysMsg ('魔法防御提升' + IntTostr(sec) + '秒', 1);
   RecalcAbilitys;
   SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
end;

{ MagBubbleDefenceUp - 魔法盾防御
  功能: 开启魔法盾防御
  参数:
    mlevel - 魔法等级
    sec - 持续时间(秒)
  返回值: 是否成功 }
function  TCreature.MagBubbleDefenceUp (mlevel, sec: integer): Boolean;
var
   old: integer;  // 旧状态
begin
   Result := FALSE;
   if StatusArr[STATE_BUBBLEDEFENCEUP] = 0 then begin
      old := CharStatus;
      StatusArr[STATE_BUBBLEDEFENCEUP] := sec;
      StatusTimes[STATE_BUBBLEDEFENCEUP] := GetTickCount;
      CharStatus := GetCharStatus;
      if old <> CharStatus then
         CharStatusChanged;
      BoAbilMagBubbleDefence := TRUE;
      MagBubbleDefenceLevel := mlevel;
      Result := TRUE;
   end;
end;

{ DamageBubbleDefence - 魔法盾受损
  功能: 魔法盾受到伤害时减少持续时间 }
procedure  TCreature.DamageBubbleDefence;
begin
   if StatusArr[STATE_BUBBLEDEFENCEUP] > 0 then begin
      if StatusArr[STATE_BUBBLEDEFENCEUP] > 3 then
         StatusArr[STATE_BUBBLEDEFENCEUP] := StatusArr[STATE_BUBBLEDEFENCEUP] - 3
      else
         StatusArr[STATE_BUBBLEDEFENCEUP] := 1;
   end;
end;

{ MagMakeDefenceArea - 创建防御区域
  功能: 提升周围友方的防御力
  参数:
    xx, yy - 中心坐标
    range - 范围
    sec - 持续时间(秒)
    BoMag - 是否魔法防御
  返回值: 影响数量 }
function  TCreature.MagMakeDefenceArea (xx, yy, range, sec: integer; BoMag: Boolean): integer;
var
   i, j, k, stx, sty, enx, eny, tcount: integer;  // 循环、范围、计数
   pm: PTMapInfo;      // 地图信息指针
   inrange: Boolean;   // 是否在范围内
   cret: TCreature;    // 生物对象
begin
   tcount := 0;
   stx := xx-range;
   enx := xx+range;
   sty := yy-range;
   eny := yy+range;
   for i:=stx to enx do begin
      for j:=sty to eny do begin
         inrange := PEnvir.GetMapXY (i, j, pm);
         if inrange then
            if pm.ObjList <> nil then begin
               for k:=0 to pm.ObjList.Count-1 do begin
                  if PTAThing (pm.ObjList[k]).Shape = OS_MOVINGOBJECT then begin
                     cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                     if cret <> nil then begin
                        if (not cret.BoGhost) then begin
                           // 只对友方生效
                           if IsProperFriend (cret) then begin
                              if not BoMag then cret.MagDefenceUp (sec)
                              else cret.MagMagDefenceUp (sec);
                              Inc (tcount);
                           end;
                        end;
                     end;
                  end;
               end;
            end;
      end;
   end;
   Result := tcount;
end;

{ MagDcUp - 攻击力提升
  功能: 提升攻击力(包括召唤兽)
  参数:
    sec - 持续时间(秒)
    BoSlaveDCUp - 是否包括召唤兽
  返回值: 是否成功 }
function   TCreature.MagDcUp (sec: integer; BoSlaveDCUp: Boolean): Boolean;
var
   i, UpDC : integer;  // 循环、提升值
   cret: TCreature;    // 生物对象
begin
   // 根据精神力计算提升值
   UpDC := ((Hibyte(WAbil.SC)-1) div 5) + 1;
   if(UpDC > 8) then UpDC := 8;
{
   if      (WAbil.SC) <=  5 then UpDC := 1
   else if (WAbil.SC) <= 10 then UpDC := 2
   else if (WAbil.SC) <= 15 then UpDC := 3
   else if (WAbil.SC) <= 20 then UpDC := 4
   else if (WAbil.SC) <= 25 then UpDC := 5
   else if (WAbil.SC) <= 30 then UpDC := 6
   else if (WAbil.SC) <= 35 then UpDC := 7
   else                          UpDC := 8;
}
   ExtraAbil[EABIL_DCUP] := UpDC;
   ExtraAbilTimes[EABIL_DCUP] := GetTickCount + sec * 1000; //초단위
   SysMsg ('攻击力提升' + IntToStr(sec) + '秒', 1);

   RecalcAbilitys;
   SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');

   if SlaveList.Count >= 1 then begin
      for i := 0 to SlaveList.Count -1 do begin
         cret := TCreature(SlaveList[i]);
         if (cret <> nil) then begin
            cret.ExtraAbil[EABIL_DCUP] := UpDC;
            cret.ExtraAbilTimes[EABIL_DCUP] := GetTickCount + sec * 1000; //초단위
            cret.RecalcAbilitys;
         end;
      end;
   end;

   Result := TRUE;
end;

{ CheckMagicLevelup - 检查魔法升级
  功能: 检查魔法是否可以升级
  参数:
    pum - 用户魔法指针
  返回值: 是否升级 }
function  TCreature.CheckMagicLevelup (pum: PTUserMagic): Boolean;
var
   lv: integer;  // 当前等级
begin
   Result := FALSE;
   if (pum.Level in [0..3]) and (pum.Level <= pum.pDef.MaxTrainLevel) then lv := pum.Level
   else lv := 0;

   if pum.Level < pum.pDef.MaxTrainLevel then begin
      // 检查经验是否足够
      if pum.CurTrain >= pum.pDef.MaxTrain[lv] then begin
         if pum.Level < pum.pDef.MaxTrainLevel then begin
            // 升级
            pum.CurTrain := pum.CurTrain - pum.pDef.MaxTrain[lv];
            pum.Level := pum.Level + 1;
            UpdateDelayMsgCheckParam1 (self, RM_MAGIC_LVEXP, 0, pum.pDef.MagicId, pum.Level, pum.CurTrain, '', 800);
            CheckMagicSpecialAbility (pum);
         end else begin
            // 已满级
            pum.CurTrain := pum.pDef.MaxTrain[lv];
         end;
         Result := TRUE;
      end;
   end;
end;

{ CheckMagicSpecialAbility - 检查魔法特殊能力
  功能: 检查魔法升级后是否解锁特殊能力
  参数:
    pum - 用户魔法指针 }
procedure TCreature.CheckMagicSpecialAbility (pum: PTUserMagic);
begin
   // 探气波烟2级以上可以看到血量
   if pum.pDef.MagicId = 28 then begin
      if pum.Level >= 2 then begin
         BoAbilSeeHealGauge := TRUE;
      end;
   end;
end;

{ GetDailyQuest - 获取日常任务
  功能: 获取当前日常任务编号
  返回值: 任务编号(过期或无任务返回0) }
function   TCreature.GetDailyQuest: integer;
var
   ayear, amon, aday: word;  // 年、月、日
   calcdate: word;           // 计算日期
begin
   DecodeDate (Date, ayear, amon, aday);
   calcdate := amon * 31 + aday;
   if (DailyQuestNumber = 0) or (DailyQuestGetDate <> calcdate) then
      Result := 0
   else
      Result := DailyQuestNumber;
end;

{ SetDailyQuest - 设置日常任务
  功能: 设置新的日常任务
  参数:
    qnumber - 任务编号 }
procedure  TCreature.SetDailyQuest (qnumber: integer);
var
   ayear, amon, aday: word;  // 年、月、日
begin
   DecodeDate (Date, ayear, amon, aday);
   DailyQuestNumber := qnumber;
   DailyQuestGetDate := amon * 31 + aday;
end;



{%%%%%%%%%%%%%%%%%%% *RunMsg* %%%%%%%%%%%%%%%%%%%%}

{ RunMsg - 运行消息
  功能: 处理延迟消息
  参数:
    msg - 消息信息 }
procedure TCreature.RunMsg (msg: TMessageInfo);
var
   str: string;                              // 字符串
   n, v1, v2, magx, magy, magpwr, range: integer;  // 临时变量
   hiter, target: TCreature;                 // 攻击者、目标
begin
   case msg.Ident of
      RM_REFMESSAGE:  // 广播消息
         begin
            with msg do
               SendRefMsg (Integer(Sender), wParam, lParam1, lParam2, lParam3, Description);
            if Integer(msg.Sender) = RM_STRUCK then
               if RaceServer <> RC_USERHUMAN then begin
                  with msg do
                     SendMsg (self, Integer(msg.Sender), wParam, lParam1, lParam2, lParam3, Description);
               end;
         end;
      RM_DELAYMAGIC:  // 延迟魔法
         begin
            magpwr := msg.wParam;
            magx := Loword (msg.lparam1);
            magy := Hiword (msg.lparam1);
            range := msg.lparam2;
            target := TCreature(msg.lparam3);
            if target <> nil then begin
               // 计算魔法伤害
               n := target.GetMagStruckDamage (self, magpwr);
               if n > 0 then begin
                  SelectTarget (target);
                  // 对怪物伤害120%
                  if target.RaceServer >= RC_ANIMAL then begin
                     magpwr := Round(magpwr * 1.2);
                  end;
                  if (abs(magx-target.CX) <= range) and (abs(magy-target.CY) <= range) then
                     target.SendMsg (self, RM_MAGSTRUCK, 0, magpwr, 0, 0, '');
               end;
            end;
         end;
      RM_MAGSTRUCK,      // 魔法击中
      RM_MAGSTRUCK_MINE:
         begin
            // 怪物被魔法攻击后移动延迟
            if (msg.Ident = RM_MAGSTRUCK) and (RaceServer >= RC_ANIMAL) and (not RushMode) then begin
               if Abil.Level < MAXLEVEL-1 then
                  WalkTime := WalkTime + 800 + Random(1000);
            end;

            // 计算魔法伤害
            v1 := GetMagStruckDamage (nil, msg.lparam1);

            if v1 > 0 then begin
               StruckDamage (v1);
               HealthSpellChanged;
               SendRefMsg (RM_STRUCK_MAG, v1, WAbil.HP, WAbil.MP, integer(msg.sender), '');
               if RaceServer <> RC_USERHUMAN then begin
                  // 动物被魔法攻击后肉质下降
                  if BoAnimal then begin
                     MeatQuality := MeatQuality - v1 * 1000;
                  end;
                  SendMsg (self, RM_STRUCK, v1, WAbil.HP, WAbil.MP, integer(msg.sender), '');
               end;
            end;
         end;
      RM_MAGHEALING:  // 魔法治疗
         begin
            if IncHealing + msg.lparam1 < 300 then begin
               if RaceServer = RC_USERHUMAN then begin
                  IncHealing := IncHealing + msg.lparam1;
                  PerHealing := 5;
               end else begin
                  IncHealing := IncHealing + msg.lparam1;
                  PerHealing := 5;
               end;
            end else IncHealing := 300;
         end;
      RM_MAKEPOISON:  // 中毒
         begin
            hiter := TCreature(msg.lparam2);
            if hiter <> nil then begin
               if IsProperTarget (hiter) then begin
                  SelectTarget (hiter);
                  // 记录PK攻击者
                  if (RaceServer = RC_USERHUMAN) and (hiter.RaceServer = RC_USERHUMAN) then begin
                     AddPkHiter (hiter);
                  end;
                  // BOSS等级不给经验
                  if Abil.Level < 60 then
                     SetLastHiter (hiter);
               end;
               MakePoison (msg.wparam, msg.lparam1, msg.lparam3);
            end else begin
               MakePoison (msg.wparam, msg.lparam1, msg.lparam3);
            end;
         end;
      RM_DOOPENHEALTH:  // 开启血量显示
         begin
            MakeOpenHealth;
         end;
      RM_TRANSPARENT:  // 隐身
         begin
            MagicMan.MagMakePrivateTransparent (self, msg.lparam1);
         end;
      RM_RANDOMSPACEMOVE:  // 随机传送
         begin
            RandomSpaceMove (msg.Description, msg.wParam);
         end;

   end;
end;

procedure TCreature.UseLamp;
var
   old, dura: integer;
   hum: TUserHuman;
begin
   try
      if UseItems[U_RIGHTHAND].Index > 0 then begin
         old := Round (UseItems[U_RIGHTHAND].Dura / 1000);
         // 2003/03/04 0.5秒에서 1초로 바뀜에 따라 내구를 -1에서 -2로 조정
         dura := integer(UseItems[U_RIGHTHAND].Dura) - 2;   //1;
         if dura <= 0 then begin
            UseItems[U_RIGHTHAND].Dura := 0;
            //초가 사라짐나.
            if RaceServer = RC_USERHUMAN then begin
               hum := TUserHuman(self);
               hum.SendDelItem (UseItems[U_RIGHTHAND]); //클라이언트에 없어진거 보냄
            end;

            UseItems[U_RIGHTHAND].Index := 0;
            Light := GetMyLight;
            SendRefMsg (RM_CHANGELIGHT, 0, 0, 0, 0, '');
            SendMsg (self, RM_LAMPCHANGEDURA, 0, UseItems[U_RIGHTHAND].Dura, 0, 0, '');
         end else
            UseItems[U_RIGHTHAND].Dura := dura;
         if old <> Round(dura / 1000) then begin
            //내구성이 변함
            SendMsg (self, RM_LAMPCHANGEDURA, 0, UseItems[U_RIGHTHAND].Dura, 0, 0, '');
         end;
      end;
   except
      MainOutMessage ('[Exception] TCreature.UseLamp');
   end;
end;

{ UseLamp - 使用灯
  功能: 消耗灯的耐久 }
procedure TCreature.UseLamp;
var
   old, dura: integer;  // 旧耐久、当前耐久
   hum: TUserHuman;     // 玩家对象
begin
   try
      if UseItems[U_RIGHTHAND].Index > 0 then begin
         old := Round (UseItems[U_RIGHTHAND].Dura / 1000);
         // 每秒消耗2点耐久
         dura := integer(UseItems[U_RIGHTHAND].Dura) - 2;
         if dura <= 0 then begin
            UseItems[U_RIGHTHAND].Dura := 0;
            // 灯燃尽
            if RaceServer = RC_USERHUMAN then begin
               hum := TUserHuman(self);
               hum.SendDelItem (UseItems[U_RIGHTHAND]);
            end;

            UseItems[U_RIGHTHAND].Index := 0;
            Light := GetMyLight;
            SendRefMsg (RM_CHANGELIGHT, 0, 0, 0, 0, '');
            SendMsg (self, RM_LAMPCHANGEDURA, 0, UseItems[U_RIGHTHAND].Dura, 0, 0, '');
         end else
            UseItems[U_RIGHTHAND].Dura := dura;
         // 耐久变化时通知
         if old <> Round(dura / 1000) then begin
            SendMsg (self, RM_LAMPCHANGEDURA, 0, UseItems[U_RIGHTHAND].Dura, 0, 0, '');
         end;
      end;
   except
      MainOutMessage ('[Exception] TCreature.UseLamp');
   end;
end;

{%%%%%%%%%%%%%%%%%%%%% *Run* %%%%%%%%%%%%%%%%%%%%%}

{ Run - 运行
  功能: 生物主循环处理
  实现原理:
    - 处理消息队列
    - 生命魔法恢复
    - 状态检查
    - 死亡处理 }
procedure TCreature.Run;
var
   msg: TMessageInfo;                        // 消息信息
   i, n, hp, mp, ablmask, plus: integer;     // 临时变量
   inchstime: longword;                      // 恢复时间
   cret, lhiter, lmaster: TCreature;         // 生物对象
   chg, needrecalc: Boolean;                 // 标志
begin

	try
      // 处理消息队列
      while GetMsg (msg) do RunMsg (msg);
   except
   	MainOutMessage ('[Exception] TCreature.Run 0');
   end;

   try
      // NPC不死
      if NeverDie then begin
         WAbil.HP := WAbil.MaxHP;
         WAbil.MP := WAbil.MaxMP;
      end;

      // 计算时间增量(每秒50次)
      n := (GetTickCount - ticksec) div 20;
      ticksec := GetTickCount;
      Inc (HealthTick, n);
      Inc (SpellTick, n);

      if not Death then begin
         // 生命自然恢复
         if WAbil.HP < WAbil.MaxHP then begin
            if HealthTick >= HEALTHFILLTICK then begin
               plus := WAbil.MaxHP div 75 + 1;
               if WAbil.HP+plus < WAbil.MaxHP then WAbil.HP := WAbil.HP + plus
               else WAbil.HP := WAbil.MaxHP;
               HealthSpellChanged;
            end;
         end;
         // 魔法自然恢复
         if WAbil.MP < WAbil.MaxMP then begin
            if SpellTick >= SPELLFILLTICK then begin
               plus := WAbil.MaxMP div 18 + 1;
               if WAbil.MP+plus < WAbil.MaxMP then WAbil.MP := WAbil.MP + plus
               else WAbil.MP := WAbil.MaxMP;
               HealthSpellChanged;
            end;
         end;
         // 死亡检查
         if WAbil.HP = 0 then begin
            // 复活能力
            if BoAbilRevival then begin
               if GetTickCount - LatestRevivalTime > 60 * 1000 then begin
                  LatestRevivalTime := GetTickCount;
                  ItemDamageRevivalRing;
                  WAbil.HP := WAbil.MaxHP;
                  HealthSpellChanged;
                  SysMsg ('复活戒指救了你一命。', 1);
               end;
            end;
            if WAbil.HP = 0 then
               Die;
         end;
         if HealthTick >= HEALTHFILLTICK then HealthTick := 0;
         if SpellTick >= SPELLFILLTICK then SpellTick := 0;
      end else begin
         // 死亡3分钟后消失
         if GetTickCount - DeathTime > 3 * 60 * 1000 then begin
            MakeGhost;
         end;
      end;
   except
   	MainOutMessage ('[Exception] TCreature.Run 1');
   end;


   // 药品效果处理(红药、蓝药等)
   try
      if (not Death) and ((IncSpell > 0) or (IncHealth > 0) or (IncHealing > 0)) then begin
         // 根据等级计算恢复间隔
         inchstime := 600 - _MIN(400, Abil.Level * 10);
         if (GetTickCount - IncHealthSpellTime >= inchstime) and
            (not Death) then begin

            n := _MIN(200, (GetTickCount - IncHealthSpellTime) - inchstime);
            IncHealthSpellTime := GetTickCount + longword(n);

            if (IncHealth > 0) or (IncSpell > 0) or (PerHealing > 0) then begin
               if PerHealth <= 0 then PerHealth := 1;
               if PerSpell <= 0 then PerSpell := 1;
               if PerHealing <= 0 then PerHealing := 1;
               // 生命恢复
               if IncHealth < PerHealth then begin
                  hp := IncHealth;
                  IncHealth := 0;
               end else begin
                  hp := PerHealth;
                  IncHealth := IncHealth - PerHealth;
               end;
               // 魔法恢复
               if IncSpell < PerSpell then begin
                  mp := IncSpell;
                  IncSpell := 0;
               end else begin
                  mp := PerSpell;
                  IncSpell := IncSpell - PerSpell;
               end;
               // 治疗恢复
               if IncHealing < PerHealing then begin
                  hp := hp + IncHealing;
                  IncHealing := 0;
               end else begin
                  hp := hp + PerHealing;
                  IncHealing := IncHealing - PerHealing;
               end;
               // 根据等级调整恢复速度
               PerHealth  := 5 + (Abil.Level div 10);
               PerSpell   := 5 + (Abil.Level div 10);
               PerHealing := 5;

               IncHealthSpell (hp, mp);

               // 满血满蓝时清空
               if WAbil.HP = WAbil.MaxHP then begin
                  IncHealth := 0;
                  IncHealing := 0;
               end;
               if WAbil.MP = WAbil.MaxMP then begin
                  IncSpell := 0;
               end;
            end;
         end;
      end else begin
         IncHealthSpellTime := GetTickCount;
      end;

      // 负生命恢复(中毒等)
      if HealthTick < -HEALTHFILLTICK then begin
         if WAbil.HP > 1 then begin
            WAbil.HP := WAbil.HP - 1;
            HealthTick := HealthTick + HEALTHFILLTICK;
            HealthSpellChanged;
         end;
      end;
   except
   	MainOutMessage ('[Exception] TCreature.Run 2');
   end;

   // 目标和状态检查
   try
      // 目标超时30秒或死亡或距离过远则清空
      if TargetCret <> nil then begin
         if (GetTickCount - TargetFocusTime > 30 * 1000) or
            (TargetCret.Death) or (TargetCret.BoGhost) or
            (Abs(TargetCret.CX-CX) > 15) or
            (Abs(TargetCret.CY-CY) > 15) then begin
            TargetCret := nil;
         end;
      end;
      // 最后攻击者超时30秒则清空
      if LastHiter <> nil then begin
         if (GetTickCount - LastHitTime > 30 * 1000) or
            (LastHiter.Death) or
            (LastHiter.BoGhost) then
             LastHiter := nil;
      end;
      // 经验攻击者超时6秒则清空
      if ExpHiter <> nil then begin
         if (GetTickCount - ExpHitTime > 6 * 1000) or
            (ExpHiter.Death) or
            (ExpHiter.BoGhost) then
             ExpHiter := nil;
      end;
      // 召唤兽主人死亡则召唤兽也死
      if Master <> nil then begin
         BoNoItem := TRUE;
         if (Master.Death and (GetTickCount - Master.DeathTime > 1000)) or
            (Master.BoGhost and (GetTickCount - Master.GhostTime > 1000)) then begin
            WAbil.HP := 0;
         end;
      end;
      // 清理死亡的召唤兽
      for i:=SlaveList.Count-1 downto 0 do begin
         if TCreature(SlaveList[i]).Death or TCreature(SlaveList[i]).BoGhost or (TCreature(SlaveList[i]).Master <> self) then begin
            SlaveList.Delete (i);
         end;
      end;
      // 圣言术超时
      if BoHolySeize then begin
         if GetTickCount - HolySeizeStart > HolySeizeTime then begin
            BreakHolySeize;
         end;
      end;
      // 狂暴模式超时
      if BoCrazyMode then begin
         if GetTickCount - CrazyModeStart > CrazyModeTime then begin
            BreakCrazyMode;
         end;
      end;
      // 开放血量超时
      if BoOpenHealth then begin
         if GetTickCount - OpenHealthStart > OpenHealthTime then begin
            BreakOpenHealth;
         end;
      end;
   except
      MainOutMessage ('[Exception] TCreature.Run 3');
   end;

   // 定时处理
   try
      // 2分钟一次
      if GetTickCount - time10min > 2 * 1000 * 60 then begin
         time10min := GetTickCount;
         // PK点数每2分钟减1
         if PlayerKillingPoint > 0 then begin
            DecPkPoint (1);
         end;
      end;

      // 1秒一次
      if GetTickCount - time500ms > 1000 then begin
         time500ms := time500ms + 1000;
         if RaceServer = RC_USERHUMAN then begin
            // 灯消耗
            UseLamp;
         end;
      end;

      // 5秒一次
      if GetTickCount - time5sec > 5 * 1000 then begin
         time5sec := GetTickCount;
         // 检查PK攻击者列表超时
         if RaceServer = RC_USERHUMAN then
            CheckTimeOutPkHiterList;
      end;

      // 10秒一次
      if GetTickCount - time10sec > 10 * 1000 then begin
         time10sec := GetTickCount;

         // 召唤兽忠诚度检查
         if Master <> nil then begin
            // 背叛检查
            if GetTickCount > MasterRoyaltyTime then begin
               for i:=Master.SlaveList.Count-1 downto 0 do begin
                  if Master.SlaveList[i] = self then begin
                     Master.SlaveList.Delete (i);
                     break;
                  end;
               end;
               Master := nil;  // 背叛
               WAbil.HP := WAbil.HP div 10;  // 体力急剧下降
               UserNameChanged;
            end;

            // 召唤兽寿命检查(12小时后死亡)
            if SlaveLifeTime <> 0 then
               if GetTickCount - SlaveLifeTime > 12 * 60 * 60 * 1000 then begin
                  WAbil.HP := 0;
               end;
         end;

      end;
      
      // 30秒一次
      if GetTickCount - time30sec > 30 * 1000 then begin
         time30sec := GetTickCount;
         // 检查队伍状态
         if GroupOwner <> nil then
            if GroupOwner.Death or GroupOwner.BoGhost then begin
               GroupOwner := nil;
            end;
         // 清理死亡的队员
         if GroupOwner = self then
            for i:=GroupMembers.Count-1 downto 0 do begin
               cret := TCreature(GroupOwner.GroupMembers.Objects[i]);
               if cret.Death or cret.BoGhost then
                  GroupMembers.Delete (i);
            end;
         // 检查交易对象
         if DealCret <> nil then
            if DealCret.BoGhost then begin
               DealCret := nil;
            end;

         PEnvir.VerifyMapTime (CX, CY, self);
      end;

   except
      MainOutMessage ('[Exception] TCreature.Run 4');
   end;

   try
      // 状态持续时间检查
      chg := FALSE;
      needrecalc := FALSE;
      // 检查各种状态是否超时
      for i:=0 to 11 do begin
         if StatusArr[i] > 0 then begin
            if StatusArr[i] < 60000 then
               if GetTickCount - StatusTimes[i] > 1000 then begin
                  StatusArr[i] := StatusArr[i] - 1;
                  StatusTimes[i] := StatusTimes[i] + 1000;
                  if StatusArr[i] = 0 then begin
                     chg := TRUE;
                     case i of
                        STATE_DEFENCEUP:  // 防御提升结束
                           begin
                              needrecalc := TRUE;
                              SysMsg ('防御提升效果结束', 1);
                           end;
                        STATE_MAGDEFENCEUP:  // 魔法防御提升结束
                           begin
                              needrecalc := TRUE;
                              SysMsg ('魔法防御提升效果结束', 1);
                           end;
                        STATE_TRANSPARENT:  // 隐身结束
                           begin
                              BoHumHideMode := FALSE;
                           end;
                        STATE_BUBBLEDEFENCEUP:  // 魔法盾结束
                           begin
                              BoAbilMagBubbleDefence := FALSE;
                           end;
                        else ;
                     end;
                  end;
               end;
         end;
      end;
      // 检查额外能力是否超时
      for i:=0 to 5 do begin
         if ExtraAbil[i] > 0 then begin
            if GetTickCount > ExtraAbilTimes[i] then begin
               ExtraAbil[i] := 0;
               needrecalc := TRUE;
               case i of
                  EABIL_DCUP:  SysMsg ('攻击力提升效果结束', 1);
                  EABIL_MCUP:  SysMsg ('魔法提升效果结束', 1);
                  EABIL_SCUP:  SysMsg ('精神提升效果结束', 1);
                  EABIL_HITSPEEDUP: SysMsg ('攻击速度提升效果结束', 1);
                  EABIL_HPUP:  SysMsg ('生命上限提升效果结束', 1);
                  EABIL_MPUP:  SysMsg ('魔法上限提升效果结束', 1);
               end;
            end;
         end;
      end;
      // 状态变化时更新
      if chg then begin
         CharStatus := GetCharStatus;
         CharStatusChanged;
      end;
      // 需要重算能力时重算
      if needrecalc then begin
         RecalcAbilitys;
         SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
      end;
   except
      MainOutMessage ('[Exception] TCreature.Run 5');
   end;

   // 中毒处理
   try
      if GetTickCount - poisontime > 2500 then begin
         poisontime := GetTickCount;
         if StatusArr[POISON_DECHEALTH] > 0 then begin
            // 动物中毒后肉质下降
            if BoAnimal then begin
               MeatQuality := MeatQuality - 1000;
            end;
            DamageHealth (1 + PoisonLevel);

            // 中毒时不恢复生命和魔法
            HealthTick := 0;
            SpellTick := 0;
            HealthSpellChanged;
         end;
      end;
	except
   	MainOutMessage ('[Exception] TCreature.Run 6');
   end;

end;


{ CheckAttackRule2 - 检查攻击规则2
  功能: 检查是否可以攻击目标(安全区、等级限制)
  参数:
    target - 目标生物
  返回值: 是否可以攻击 }
function  TCreature.CheckAttackRule2 (target: TCreature): Boolean;
begin
   Result := TRUE;
   if target = nil then exit;

   // 安全区不能攻击
   if (InSafeZone) or (target.InSafeZone) then begin
      Result := FALSE;
   end;

   // 非PK区域的等级限制
   if not target.BoInFreePKArea then begin
      // 高级红名不能攻击低级白名
      if (PKLevel >= 2) and (Abil.Level > 10) then begin
         if (target.Abil.Level <= 10) and (target.PKLevel < 2) then
            Result := FALSE;
      end;
      // 低级白名不能攻击高级红名
      if (Abil.Level <= 10) and (PKLevel < 2) then begin
         if (target.PKLevel >= 2) and (target.Abil.Level > 10) then
            Result := FALSE;
      end;
   end;

   // 地图移动后3秒内不能攻击
   if (GetTickCount - MapMoveTime < 3000) or (GetTickCount - target.MapMoveTime < 3000) then
      Result := FALSE;

end;

//target <> nil
function  TCreature._IsProperTarget (target: TCreature): Boolean;
   function GetNonPKServerRule (rslt: Boolean): Boolean;
   begin
      Result := rslt;
      if target.RaceServer = RC_USERHUMAN then begin
         if (not PEnvir.FightZone) and (not PEnvir.Fight3Zone) and (target.RaceServer = RC_USERHUMAN) then
            Result := FALSE;
         if UserCastle.BoCastleUnderAttack then
            if (BoInFreePKArea) or (UserCastle.IsCastleWarArea (PEnvir, CX, CY)) then
               Result := TRUE;
         if (MyGuild <> nil) and (target.MyGuild <> nil) then
            if GetGuildRelation (self, target) = 2 then //문전중임
               Result := TRUE;
      end;
   end;
begin
   Result := FALSE;
   if target = nil then exit;
   if target = self then exit;
   if RaceServer >= RC_ANIMAL then begin  //자신이 동물
      if Master <> nil then begin
         //주인이 있는 몹
         //if (target.RaceServer >= RC_ANIMAL) and (target.Master = nil) then
         //   Result := TRUE;
         if (Master.LastHiter = target) or (Master.ExpHiter = target) or (Master.TargetCret = target) then
            Result := TRUE;
         if target.TargetCret <> nil then begin
            if (target.TargetCret = Master) or  //주인을 공격
               (target.TargetCret.Master = Master) and (target.RaceServer <> 0) //동료를 공격, 사람인경우 제외
            then
               Result := TRUE;
         end;
         if (target.TargetCret = self) and (target.RaceServer >= RC_ANIMAL) then  //몹이면서 자신을 공격하는 자
            Result := TRUE;
         if target.Master <> nil then begin  //상대가 소환몹
            if (target.Master = Master.LastHiter) or (target.Master = Master.TargetCret) then
               Result := TRUE;
         end;
         if target.Master = Master then Result := FALSE;  //주인이 같으면 공격안함
         if target.BoHolySeize then Result := FALSE;  //결계에 걸려 있으면 공격안함
         if Master.BoSlaveRelax then Result := FALSE;
         if target.RaceServer = RC_USERHUMAN then begin  //상대가 사람인 경우
            if (target.InSafeZone) then begin  //상대가 안전지대에 있는 경우
               Result := FALSE;
            end;
         end;
         BreakCrazyMode;  //주인있는 몹..
      end else begin
         //일반 몹
         if target.RaceServer = RC_USERHUMAN then
            Result := TRUE;
         if (RaceServer > RC_PEACENPC) and (RaceServer < RC_ANIMAL) then begin //공격력을 가진 NPC는 아무나 공격한다.
            Result := TRUE;
         end;
         if target.Master <> nil then
            Result := TRUE;
      end;
      if BoCrazyMode then  //미침, 아무나 공격, 적 안가림... (소환몹에게도 통한다.)
         Result := TRUE;
   end else begin //npc혹은 사람인경우
      if RaceServer = RC_USERHUMAN then begin
         //공격형태 설정에 따라 다름
         case HumAttackMode of
            HAM_ALL: begin
                  if not ((target.RaceServer >= RC_NPC) and (target.RaceServer <= RC_PEACENPC)) then
                     Result := TRUE;
                  if BoNonPKServer then
                     Result := GetNonPKServerRule (Result);
               end;
            HAM_PEACE: begin
                  if target.RaceServer >= RC_ANIMAL then
                     Result := TRUE;
               end;
            HAM_GROUP: begin
                  if not ((target.RaceServer >= RC_NPC) and (target.RaceServer <= RC_PEACENPC)) then
                     Result := TRUE;
                  if target.RaceServer = RC_USERHUMAN then
                     if IsGroupMember (target) then
                        Result := FALSE;
                  if BoNonPKServer then
                     Result := GetNonPKServerRule (Result);
               end;
            HAM_GUILD: begin
                  if not ((target.RaceServer >= RC_NPC) and (target.RaceServer <= RC_PEACENPC)) then
                     Result := TRUE;
                  if target.RaceServer = RC_USERHUMAN then
                     if MyGuild <> nil then begin
                        if TGuild(MyGuild).IsMember(target.UserName) then
                           Result := FALSE;
                        if BoGuildWarArea and (target.MyGuild <> nil) then begin  //문파전,공성전 지역에 있음
                           if TGuild(MyGuild).IsAllyGuild(TGuild(target.MyGuild)) then
                              Result := FALSE;
                        end;
                     end;
                  if BoNonPKServer then
                     Result := GetNonPKServerRule (Result);
               end;
            HAM_PKATTACK: begin
                  if not ((target.RaceServer >= RC_NPC) and (target.RaceServer <= RC_PEACENPC)) then
                     Result := TRUE;
                  if target.RaceServer = RC_USERHUMAN then begin
                     if self.PKLevel >= 2 then begin  //공격하는 자가 빨갱이
                        if target.PKLevel < 2 then Result := TRUE
                        else Result := FALSE;
                     end else begin
                        //공격하는 자가 흰둥이
                        if target.PKLevel >= 2 then Result := TRUE
                        else Result := FALSE;
                     end;
                  end;
                  if BoNonPKServer then
                     Result := GetNonPKServerRule (Result);
               end;
         end;
      end else
         Result := TRUE;
   end;

   // 运营者或石化状态不能攻击
   if target.BoSysopMode or target.BoStoneMode then
      Result := FALSE;
end;

{ IsProperTarget - 是否是合适目标
  功能: 检查目标是否可以攻击
  参数:
    target - 目标生物
  返回值: 是否可以攻击 }
function  TCreature.IsProperTarget (target: TCreature): Boolean;
begin
   Result := _IsProperTarget (target);
   if Result then
      if (RaceServer = RC_USERHUMAN) and (target.RaceServer = RC_USERHUMAN) then begin
         // 根据地区检查PK规则
         Result := CheckAttackRule2 (target);
         // 活动用户可以被攻击
         if target.BoTaiwanEventUser then
            Result := TRUE;
      end;

   // 玩家攻击召唤兽的处理
   if (target <> nil) and (RaceServer = RC_USERHUMAN) then begin
      if (target.Master <> nil) and (target.RaceServer <> RC_USERHUMAN) then begin
         // 自己的召唤兽
         if target.Master = self then begin
            // 只有全体攻击模式才能攻击自己的召唤兽
            if HumAttackMode <> HAM_ALL then
               Result := FALSE;
         end else begin
            // 别人的召唤兽
            Result := _IsProperTarget(target.Master);
            if (InSafeZone) or (target.InSafeZone) then begin
               Result := FALSE;
            end;
         end;
      end;
   end;
end;


{ IsProperFriend - 是否是合适友方
  功能: 检查目标是否是友方(可以治疗等)
  参数:
    target - 目标生物
  返回值: 是否是友方 }
function  TCreature.IsProperFriend (target: TCreature): Boolean;
   // 检查是否是友方
   function IsFriend (cret: TCreature): Boolean;
   begin
      Result := FALSE;
      // 只对玩家判定
      if cret.RaceServer = RC_USERHUMAN then begin
         // 根据攻击模式判定
         case HumAttackMode of
            HAM_ALL:  Result := TRUE;   // 全体攻击
            HAM_PEACE: Result := TRUE;  // 和平模式
            HAM_GROUP:  // 组队模式
               begin
                  if cret = self then
                     Result := TRUE;
                  if IsGroupMember (cret) then
                     Result := TRUE;
               end;
            HAM_GUILD:  // 行会模式
               begin
                  if cret = self then
                     Result := TRUE;
                  if MyGuild <> nil then begin
                     if TGuild(MyGuild).IsMember(cret.UserName) then
                        Result := TRUE;
                     // 行会战区域检查联盟
                     if BoGuildWarArea and (cret.MyGuild <> nil) then begin
                        if TGuild(MyGuild).IsAllyGuild(TGuild(cret.MyGuild)) then
                           Result := TRUE;
                     end;
                  end;
               end;
            HAM_PKATTACK:  // PK攻击模式
               begin
                  if cret = self then Result := TRUE;
                  // 红名对红名
                  if PKLevel >= 2 then begin
                     if cret.PKLevel >= 2 then Result := TRUE;
                  end else begin
                     // 白名对白名
                     if cret.PKLevel < 2 then Result := TRUE;
                  end;
               end;
         end;
      end;
   end;
begin
   Result := FALSE;
   if target = nil then exit;
   // 动物对动物
   if RaceServer >= RC_ANIMAL then begin
      if target.RaceServer >= RC_ANIMAL then
         Result := TRUE;
      // 召唤兽不能被治疗
      if target.Master <> nil then
         Result := FALSE;
   end else begin
      // 玩家判定
      if RaceServer = RC_USERHUMAN then begin
         Result := IsFriend (target);
         // 对召唤兽的判定
         if target.RaceServer >= RC_ANIMAL then begin
            if target.Master = self then
               Result := TRUE
            else if target.Master <> nil then
               Result := IsFriend (target.Master);
         end;
      end else
         Result := TRUE;
   end;
end;

{ SelectTarget - 选择目标
  功能: 设置当前攻击目标
  参数:
    target - 目标生物 }
procedure TCreature.SelectTarget (target: TCreature);
begin
   TargetCret := target;
   TargetFocusTime := GetTickCount;
end;

{ LoseTarget - 丢失目标
  功能: 清空当前攻击目标 }
procedure TCreature.LoseTarget;
begin
   TargetCret := nil;
end;



{%%%%%%%%%%%%%%%%%%%%%% *TAnimal* %%%%%%%%%%%%%%%%%%%}
{ TAnimal类 - 动物/怪物基类
  功能: 实现怪物的基本行为
  用途: 所有怪物类的父类 }

{ Create - 构造函数
  功能: 初始化动物对象 }
constructor TAnimal.Create;
begin
   inherited Create;
   TargetX := -1;  // 无目标位置
   FindPathRate := 1000 + Random(4) * 500;  // 寻路频率
   FindpathTime := GetTickCount;
   RaceServer := RC_ANIMAL;
   HitTime := GetCurrentTime - Random(3000);
   WalkTime := GetCurrentTime - Random(3000);
   SearchEnemyTime := GetTickCount;
   BoRunAwayMode := FALSE;
   RunAwayStart := GetTickCount;
   RunAwayTime := 0;
end;

{ RunMsg - 运行消息
  功能: 处理动物的消息
  参数:
    msg - 消息信息 }
procedure TAnimal.RunMsg (msg: TMessageInfo);
var
   str: string;  // 字符串
begin
   case msg.Ident of
      RM_STRUCK:  // 被击中
         begin
            if (msg.Sender = self) and (msg.lParam3 <> 0) then begin
               SetLastHiter (TCreature(msg.lparam3));
               Struck (TCreature(msg.lParam3));
               BreakHolySeize;
               // 召唤兽被攻击时记录攻击者
               if (Master <> nil) and (TCreature(msg.lparam3) <> Master) then begin
                  if TCreature(msg.lparam3).RaceServer = RC_USERHUMAN then begin
                     Master.AddPkHiter (TCreature(msg.lparam3));
                  end;
               end;
            end;
         end;
      else
         inherited RunMsg (msg);
   end;
end;

{ Run - 运行
  功能: 动物主循环 }
procedure TAnimal.Run;
begin
   inherited Run;
end;

{ Attack - 攻击
  功能: 执行攻击动作
  参数:
    target - 目标生物
    dir - 方向 }
procedure TAnimal.Attack (target: TCreature; dir: byte);
begin
   inherited HitHit (target, HM_HIT, dir);
end;

{ Struck - 被击中
  功能: 处理被攻击时的反应
  参数:
    hiter - 攻击者 }
procedure TAnimal.Struck (hiter: TCreature);
var
   targdir: byte;  // 目标方向
begin
   StruckTime := GetTickCount;
   if hiter <> nil then begin
      // 有几率切换目标
      if (TargetCret = nil) or (not TargetInAttackRange (TargetCret, targdir)) or (Random(6) = 0) then
         if IsProperTarget (hiter) then
            SelectTarget (hiter);
   end;
   // 动物被攻击后肉质下降
   if BoAnimal then begin
      MeatQuality := MeatQuality - Random (300);
      if MeatQuality < 0 then MeatQuality := 0;
   end;
   // 被攻击后攻击延迟
   if Abil.Level < MAXLEVEL-1 then
      HitTime  := HitTime + (150 - _MIN(130, Abil.Level * 4));
end;

{ LoseTarget - 丢失目标
  功能: 清空目标和目标位置 }
procedure TAnimal.LoseTarget;
begin
   inherited LoseTarget;
   TargetX := -1;
   TargetY := -1;
end;

{ MonsterNormalAttack - 怪物普通攻击
  功能: 搜索并选择最近的敌人 }
procedure TAnimal.MonsterNormalAttack;
var
   i, d, dis: integer;        // 循环、距离
   cret, nearcret: TCreature; // 生物、最近生物
begin
   nearcret := nil;
   dis := 999;
   for i:=0 to VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
      // 检查是否可攻击且未隐身
      if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
         d := abs(CX-cret.CX) + abs(CY-cret.CY);
         if d < dis then begin
            dis := d;
            nearcret := cret;
         end;
      end;
   end;
   if nearcret <> nil then
      SelectTarget (nearcret);
end;

{ MonsterDetecterAttack - 怪物侦测攻击
  功能: 搜索并选择最近的敌人(可侦测隐身) }
procedure TAnimal.MonsterDetecterAttack;
var
   i, d, dis: integer;        // 循环、距离
   cret, nearcret: TCreature; // 生物、最近生物
begin
   nearcret := nil;
   dis := 999;
   for i:=0 to VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
      if (not cret.Death) and (IsProperTarget(cret)) then begin
         d := abs(CX-cret.CX) + abs(CY-cret.CY);
         if d < dis then begin
            dis := d;
            nearcret := cret;
         end;
      end;
   end;
   if nearcret <> nil then
      SelectTarget (nearcret);
end;

{ SetTargetXY - 设置目标坐标
  功能: 设置移动目标位置
  参数:
    x, y - 目标坐标 }
procedure TAnimal.SetTargetXY (x, y: integer);
begin
   TargetX := x;
   TargetY := y;
end;

{ GotoTargetXY - 走向目标坐标
  功能: 向目标位置移动 }
procedure TAnimal.GotoTargetXY;
var
   wantdir, i, targx, targy, oldx, oldy, rand: integer;  // 方向、坐标等
begin
   // 有目标位置时
   if (CX <> TargetX) or (CY <> TargetY) then begin
      targx := TargetX;
      targy := TargetY;
      FindPathTime := GetCurrentTime;
      wantdir := DR_DOWN;
      // 计算移动方向
      while TRUE do begin
         if targx > self.CX then begin
            wantdir := DR_RIGHT;
            if targy > self.CY then
               wantdir := DR_DOWNRIGHT;
            if targy < self.CY then
               wantdir := DR_UPRIGHT;
            break;
         end;
         if targx < self.CX then begin
            wantdir := DR_LEFT;
            if targy > self.CY then
               wantdir := DR_DOWNLEFT;
            if targy < self.CY then
               wantdir := DR_UPLEFT;
            break;
         end;
         if targy > self.CY then begin
            wantdir := DR_DOWN;
            break;
         end;
         if targy < self.CY then begin
            wantdir := DR_UP;
            break;
         end;
         break;
      end;

      oldx := self.CX;
      oldy := self.CY;
      WalkTo (wantdir, FALSE);
      rand := Random (3);
      // 前方被堵时尝试其他方向
      for i:=1 to 7 do begin
         if (oldx = self.CX) and (oldy = self.CY) then begin
            if rand <> 0 then Inc (wantdir)
            else if wantdir > 0 then Dec (wantdir)
            else wantdir := 7;
            if wantdir > 7 then wantdir := 0;
            WalkTo (wantdir, FALSE);
         end else
            break;
      end;
   end;
end;

{ Wondering - 徘徊
  功能: 随机移动或转向 }
procedure TAnimal.Wondering;
begin
   if Random(20) = 0 then begin
      if Random(4) = 1 then Turn (Random(8))  // 随机转向
      else begin
         inherited WalkTo (self.Dir, FALSE);  // 向前走
      end;
   end;
end;


{%%%%%%%%%%%%%%%%%%%%%% *TUserHuman* %%%%%%%%%%%%%%%%%%%}
{ TUserHuman类 - 玩家类
  功能: 实现玩家的所有功能
  用途: 处理玩家的操作、网络通信等 }

{ Create - 构造函数
  功能: 初始化玩家对象 }
constructor TUserHuman.Create;
begin
   inherited Create;
   RaceServer := RC_USERHUMAN;
   EmergencyClose := FALSE;
   BoChangeServer := FALSE;
   SoftClosed := FALSE;
   UserRequestClose := FALSE;
   UserSocketClosed := FALSE;
   ReadyRun := FALSE;  // 加载完成后设为TRUE
   PriviousCheckCode := 0;
   CrackWanrningLevel := 0;  // 包复制等作弊检测
   LastSaveTime := GetTickCount;
   WantRefMsg := TRUE;
   BoSaveOk := FALSE;
   MustRandomMove := FALSE;
   CurQuest := nil;
   CurSayProc := nil;

   // 定时回城
   BoTimeRecall := FALSE;
   TimeRecallMap := '';
   TimeRecallX := 0;
   TimeRecallY := 0;

   RunTime := GetCurrentTime;
   RunNextTick := 250;
   SearchRate := 1000;
   SearchTime := GetTickCount;
   ViewRange := 12;
   FirstTimeConnection := FALSE;
   LoginSign := FALSE;
   BoServerShifted := FALSE;
   BoAccountExpired := FALSE;

   BoSendNotice := FALSE;
   operatetime := GetTickCount;
   operatetime_sec := GetTickCount;
   operatetime_500m := GetTickCount;
   boGuildwarsafezone := FALSE;

   // 加速检测
   ClientMsgCount := 0;
   ClientSpeedHackDetect := 0;
   LatestSpellTime := GetTickCount;
   LatestSpellDelay := 0;
   LatestHitTime := GetTickCount;
   LatestWalkTime := GetTickCount;
   HitTimeOverCount := 0;
   HitTimeOverSum := 0;
   SpellTimeOverCount := 0;
   WalkTimeOverCount := 0;
   WalkTimeOverSum := 0;
   SpeedHackTimerOverCount := 0;

   SendBuffers := TList.Create;
   PrevServerSlaves := TList.Create;  // 跨服召唤兽列表

   // 发言限制
   LatestSayStr := '';
   BombSayCount := 0;
   BombSayTime := GetTickCount;
   BoShutUpMouse := FALSE;
   ShutUpMouseTime := GetTickCount;

   // 登录时间
   LoginDateTime := Now;
   LoginTime := GetTickCount;

   // 时间同步
   FirstClientTime := 0;
   FirstServerTime := 0;

   // 换服
   BoChangeServer := FALSE;
   BoChangeServerNeedDelay := FALSE;
   WriteChangeServerInfoCount := 0;

   LineNoticeTime := GetTickCount;
   LineNoticeNumber := 0;

end;

{ Destroy - 析构函数
  功能: 释放玩家对象资源 }
destructor TUserHuman.Destroy;
var
   i: integer;  // 循环变量
begin
   // 释放发送缓冲区
   for i:=0 to SendBuffers.Count-1 do
      FreeMem (SendBuffers[i]);
   SendBuffers.Free;
   PrevServerSlaves.Free;
   inherited Destroy;
end;

{ GetUserMassCount - 获取周围玩家数
  功能: 获取周围区域内的玩家数量
  返回值: 玩家数量 }
function  TUserHuman.GetUserMassCount: integer;
begin
   Result := UserEngine.GetAreaUserCount (PEnvir, CX, CY, 10);
end;

{ ResetCharForRevival - 重置角色复活状态
  功能: 死亡后重置状态 }
procedure TUserHuman.ResetCharForRevival;
begin
   FillChar (StatusArr, sizeof(word)*12, #0);
end;

{ Clear_5_9_bugitems - 清除BUG物品
  功能: 清除索引异常的物品 }
procedure  TUserHuman.Clear_5_9_bugitems;
var
   i: integer;  // 循环变量
begin
   // 清除背包中的异常物品
   for i:=ItemList.Count-1 downto 0 do begin
      if PTUserItem(ItemList[i]).Index >= 164 then begin
         Dispose (PTUserItem(ItemList[i]));
         ItemList.Delete (i);
      end;
   end;
   // 清除仓库中的异常物品
   for i:=SaveItems.Count-1 downto 0 do begin
      if PTUserItem(SaveItems[i]).Index >= 164 then begin
         Dispose (PTUserItem(SaveItems[i]));
         SaveItems.Delete (i);
      end;
   end;
end;

{ Reset_6_28_bugitems - 修复BUG物品
  功能: 修复最大耐久为0的物品 }
procedure TUserHuman.Reset_6_28_bugitems;
var
   i: integer;     // 循环变量
   ps: PTStdItem;  // 标准物品指针
begin
   // 修复装备栏物品
   for i:=0 to 12 do begin
      if UseItems[i].DuraMax = 0 then begin
         ps := UserEngine.GetStdItem (UseItems[i].Index);
         if ps <> nil then
            UseItems[i].DuraMax := ps.DuraMax;
      end;
   end;
   // 修复背包物品
   for i:=ItemList.Count-1 downto 0 do begin
      if PTUserItem(ItemList[i]).DuraMax = 0 then begin
         ps := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
         if ps <> nil then
            PTUserItem(ItemList[i]).DuraMax := ps.DuraMax;
      end;
   end;
   // 修复仓库物品
   for i:=SaveItems.Count-1 downto 0 do begin
      if PTUserItem(SaveItems[i]).Index >= 164 then begin
         ps := UserEngine.GetStdItem (PTUserItem(SaveItems[i]).Index);
         if ps <> nil then
            PTUserItem(SaveItems[i]).DuraMax := ps.DuraMax;
      end;
   end;
end;

{ Initialize - 初始化
  功能: 初始化玩家数据
  实现原理:
    - 检查测试服务器设置
    - 清理无效物品
    - 初始化状态
    - 检查装备有效性 }
procedure  TUserHuman.Initialize;
var
   i, k, sidx: integer;   // 循环变量
   iname: string;         // 物品名称
   pi: PTUserItem;        // 用户物品指针
   u: TUserItem;          // 用户物品
   ps: PTStdItem;         // 标准物品指针
   plsave: PTSlaveInfo;   // 召唤兽信息指针
begin
   // 测试服务器设置
   if BoTestServer then begin
      if Abil.Level < TestLevel then begin
         Abil.Level := TestLevel;
      end;
      if Gold < TestGold then begin
         Gold := TestGold;
      end;
   end;
   // 体验模式
   if BoTestServer or BoServiceMode then begin
      ApprovalMode := 3;
   end;

   MapMoveTime := GetTickCount;
   LoginDateTime := Now;
   LoginTime := GetTickCount;

   inherited Initialize;

   // 清除已删除的物品
   for i:=ItemList.Count-1 downto 0 do begin
      if UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index) = '' then begin
         Dispose (PTUserItem(ItemList[i]));
         ItemList.Delete (i);
         continue;
      end;
      //Desc, 추가 능력치에 버그 발생...
      //pi := PTUserItem(ItemList[i]);
      //if (pi.Desc[0] > 10) or (pi.Desc[1] > 10) or (pi.Desc[2] > 10) or (pi.Desc[3] > 10) then
      //   FillChar (pi.Desc, 12, #0);
   end;
   //for i:=0 to 12 do begin   // 8->12
   //   pi := @UseItems[i];
   //   if pi.Index > 0 then
   //      if (pi.Desc[0] > 10) or (pi.Desc[1] > 10) or (pi.Desc[2] > 10) or (pi.Desc[3] > 10) then
   //         FillChar (pi.Desc, 12, #0);
   //end;

   // 初始化状态时间
   for i:=0 to 11 do begin
      if StatusArr[i] > 0 then begin
         StatusTimes[i] := GetTickCount;
      end;
   end;
   CharStatus := GetCharStatus;

   // 检查装备有效性
   for i:=0 to 12 do begin
      if UseItems[i].Index > 0 then begin
         ps := UserEngine.GetStdItem (UseItems[i].Index);
         if ps <> nil then begin
            // 检查装备是否可以穿戴在该位置
            if not IsTakeOnAvailable (i, ps) then begin
               new (pi);
               pi^ := UseItems[i];
               AddItem (pi);
               UseItems[i].Index := 0;
            end;
         end else
            UseItems[i].Index := 0;
      end;
   end;

   // 检查活动物品
   for i:=0 to ItemList.Count-1 do begin
      ps := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
      if ps <> nil then begin
         if not BoServerShifted then begin
            // 活动物品死亡后掉落，登录时不能持有
            if ps.StdMode = TAIWANEVENTITEM then begin
               Dispose (PTUserItem(ItemList[i]));
               ItemList.Delete(i);
               continue;
            end;
         end else begin
            // 跨服时保留活动物品
            if ps.StdMode = TAIWANEVENTITEM then begin
               TaiwanEventItemName := ps.Name;
               BoTaiwanEventUser := TRUE;
               StatusArr[STATE_BLUECHAR] := 60000;
               Light := GetMyLight;
               SendRefMsg (RM_CHANGELIGHT, 0, 0, 0, 0, '');
               CharStatus := GetCharStatus;
            end;
         end;
      end;
   end;

   // 检查重复物品
   for i:=ItemList.Count-1 downto 0 do begin
      iname := UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index);
      sidx  := PTUserItem(ItemList[i]).MakeIndex;
      for k:=i-1 downto 0 do begin
         pi := PTUserItem(ItemList[k]);
         if (iname = UserEngine.GetStdItemName (pi.Index)) and (sidx = pi.MakeIndex) then begin
            Dispose (pi);
            ItemList.Delete(k);
            break;
         end;
      end;
   end;

   // 发送登录消息
   SendMsg (self, RM_LOGON, 0, 0, 0, 0, '');

{$IFDEF FOR_ABIL_POINT}
   // 有奖励点数时发送调整消息
   if BonusPoint > 0 then
      SendMsg (self, RM_ADJUST_BONUS, 0, 0, 0, 0, '');
{$ENDIF}

   // 人数过多时随机移动
   if Abil.Level <= EXPERIANCELEVEL then
      if (GetUserMassCount >= 80) then begin
         RandomSpaceMove (PEnvir.MapName, 0);
      end;

   // 需要随机移动时
   if MustRandomMove then begin
      RandomSpaceMove (PEnvir.MapName, 0);
   end;

   UserDegree := UserEngine.GetMyDegree (UserName);
   CheckHomePos;  // PK玩家从PK区开始

   // 检查魔法特殊能力
   for i:=0 to MagicList.Count-1 do begin
      CheckMagicSpecialAbility (PTUserMagic (MagicList[i]));
   end;

   // 新角色初始物品
   if FirstTimeConnection then begin
      new (pi);
      if UserEngine.CopyToUserItemFromName (__Candle, pi^) then
         ItemList.Add (pi)
      else Dispose (pi);
      new (pi);
      if UserEngine.CopyToUserItemFromName (__BasicDrug, pi^) then
         ItemList.Add (pi)
      else Dispose (pi);
      new (pi);
      if UserEngine.CopyToUserItemFromName (__WoodenSword, pi^) then
         ItemList.Add (pi)
      else Dispose (pi);
      // 根据性别给予初始衣服
      if Sex = 0 then begin
         new (pi);
         if UserEngine.CopyToUserItemFromName (__ClothsForMan, pi^) then
            ItemList.Add (pi)
         else Dispose (pi);
      end else begin
         new (pi);
         if UserEngine.CopyToUserItemFromName (__ClothsForWoman, pi^) then
            ItemList.Add (pi)
         else Dispose (pi);
      end;
   end;

   // 重新计算能力
   RecalcLevelAbilitys;
   RecalcAbilitys;
   Abil.MaxExp := GetNextLevelExp (Abil.Level);
   Wabil.MaxExp := Abil.MaxExp;

   // 免罪次数
   if FreeGulityCount = 0 then begin
      PlayerKillingPoint := 0;
      Inc (FreeGulityCount);
   end;

   // 金币上限
   if Gold > BAGGOLD*2 then Gold := BAGGOLD*2;

   if not BoServerShifted then begin
     { if (ClientVersion < VERSION_NUMBER) or
         (ClientVersion <> LoginClientVersion) or
         ((ClientCheckSum <> ClientCheckSumValue1) and
          (ClientCheckSum <> ClientCheckSumValue2) and
          (ClientCheckSum <> ClientCheckSumValue3)
          ) then
      begin

         SysMsg ('넋埼경굶댄轎', 0);  //랙箇와빵똥댄轎斤口

         if CHINAVERSION then
            SysMsg ('(http://www.legendofmir.com.cn)', 0);
         if KOREANVERSION then
            SysMsg ('(http://www.mir2.co.kr)', 0);
         if ENGLISHVERSION then
            SysMsg ('(http://www.legendofmir.net)', 0);
         if TAIWANVERSION then
            SysMsg ('(http://www.mir2.com.tw)', 0);

         SysMsg ('櫓뙤젯窟', 0);

         EmergencyClose := TRUE;
         exit;

      end;}

      case HumAttackMode of
         HAM_ALL:    SysMsg ('[홍竟묑샌]', 1);
         HAM_PEACE:  SysMsg ('[뵨틱묑샌]', 1);
         HAM_GROUP:  SysMsg ('[긍莉묑샌]', 1);
         HAM_GUILD:  SysMsg ('[契삔묑샌]', 1);
         HAM_PKATTACK: SysMsg ('[�띳뚤묑]', 1);
      end;
      SysMsg ('뫘맣묑샌친駕: CTRL-H ', 1);

      if BoTestServer then begin
         SysMsg ('헌루�撚多헐 齡鱗諒QQ:14967593', 1);

         //인원 제한
         if UserEngine.GetUserCount > TestServerMaxUser then begin
            if UserDegree < UD_OBSERVER then begin
               SysMsg ('豚冀돨鯤소훙鑒綠찮。', 0);
               SysMsg ('젯쌈굳老岺。', 0);
               EmergencyClose := TRUE;
            end;
         end;
      end;

      if ApprovalMode = 1 then begin //체험판 사용자, 테스트 서버는 공짜
         if not BoServerShifted then
            SysMsg ('콱君瞳뇹黨꿎桿櫓，콱옵鹿瞳펌섬鹿품賈痰，뎃角삔掘齡콱돨寧硅묘콘。', 1);
         AvailableGold := 100000;  //체험판 사용자는 들 수 있는 돈이 제한됨
         if (Abil.Level > EXPERIANCELEVEL) then begin  //체험판으로 접속이 안됨
            SysMsg ('꿎桿친駕옵賈痰돕펌섬。', 0);
            SysMsg ('젯窟櫓뙤，헝逞굶踏狗貢籃꽝敦澗롤宮밑祇口。http://www.mir2.com.cn) ', 0);
            EmergencyClose := TRUE;
         end;
      end;
      // 2003/03/18 테스트 서버 인원 제한
      if ApprovalMode > 3 then begin //무료사용자, 20일 한정 사용자
//    if ApprovalMode = 3 then begin //무료사용자, 20일 한정 사용자
         if not BoServerShifted then
            SysMsg ('君瞳角출롤桿鯤珂쇌。', 1);
      end;

      if BoVentureServer then begin  //모험서버
         SysMsg ('뻑短밟줄챨麴륩蛟포。', 1);
      end;

   end;

   Bright := MirDayTime;
   SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
   SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
   SendMsg (self, RM_DAYCHANGING, 0, 0, 0, 0, '');
   SendMsg (self, RM_SENDUSEITEMS, 0, 0, 0, 0, '');
   SendMsg (self, RM_SENDMYMAGIC, 0, 0, 0, 0, '');

   //문파에 가입되어 있는지..
   MyGuild := GuildMan.GetGuildFromMemberName (UserName);
   if MyGuild <> nil then begin  //길드에 가입되어 있는 경우
      GuildRankName := TGuild (MyGuild).MemberLogin (self, GuildRank);
      //SendMsg (self, RM_CHANGEGUILDNAME, 0, 0, 0, 0, '');
      for i:=0 to TGuild(MyGuild).KillGuilds.Count-1 do begin
         SysMsg (TGuild(MyGuild).KillGuilds[i] + '瞳宅콱돨契삔쏵契契삔濫轢.', 1);
      end;
   end;

   if PLongHitSkill <> nil then
      if not BoAllowLongHit then begin
         BoAllowLongHit := TRUE;
         SendSocket (nil, '+LNG');  //원거리 공격을 하게 한다.
      end;
   {if PWideHitSkill <> nil then  //
      if not BoAllowWideHit then begin
         BoAllowWideHit := TRUE;
         SendSocket (nil, '+WID');  //
      end;}

   //재접이 안되는 맵
   if PEnvir.NoReconnect then begin
      RandomSpaceMove (PEnvir.BackMap, 0);
   end;

   // 2003/06/12 슬레이브 패치
   //소환 몹 부른다.
   if PrevServerSlaves.Count > 0 then begin
      for i:=0 to PrevServerSlaves.Count-1 do begin
          plsave := PTSlaveInfo(PrevServerSlaves[i]);
          RmMakeSlaveProc(plsave);
          Dispose(plsave);
      end;
      PrevServerSlaves.Clear;
   end;

   if not BoServerShifted then begin  ////
      SendMsg (self, RM_DOSTARTUPQUEST, 0, 0, 0, 0, '');

   end;

end;

{ Finalize - 结束处理
  功能: 玩家下线时的清理工作 }
procedure  TUserHuman.Finalize;
begin
   try
      // 登录成功后才消失
      if ReadyRun then Disappear;
   except
   end;

   // 清除隐身状态
   if BoFixedHideMode then begin
      if BoHumHideMode then begin
         StatusArr[STATE_TRANSPARENT] := 0;
      end;
   end;
   // 清除活动状态
   if BoTaiwanEventUser then begin
      StatusArr[STATE_BLUECHAR] := 0;
   end;

   // 退出队伍
   try
      if GroupOwner <> nil then
         GroupOwner.DelGroupMember (self);
   except
   end;

   // 退出行会
   try
      if MyGuild <> nil then
         TGuild (MyGuild).MemberLogout (self);
   except
   end;

   // 记录连接日志
   WriteConLog;

   inherited Finalize;
end;

{ WriteConLog - 写连接日志
  功能: 记录玩家连接信息 }
procedure TUserHuman.WriteConLog;
var
   contime: longword;  // 连接时间
   contype: integer;   // 连接类型
begin
   // 计算连接时间(秒)
   if (ApprovalMode = 2) or (BoTestServer) then begin
      contime := (GetTickCount - LoginTime) div 1000;
   end else begin
      contime := 0;
   end;

   // 体验版等级以下不计费
   contype := AvailableMode;
   if (AvailableMode = 2) and ( Abil.Level <= EXPERIANCELEVEL ) then
      contype := 5;

   AddConLog (UserAddress + ''#9 +
              UserId + ''#9 +
              UserName + ''#9 +
              IntToStr(contime) + ''#9 +
              FormatDateTime('yyyy-mm-dd hh:mm:ss', LoginDateTime) + ''#9 +
              FormatDateTime('yyyy-mm-dd hh:mm:ss', Now) + ''#9 +
              IntToStr(contype));

   // 计时卡用户扣除时间
   if (AvailableMode = 2) and ( Abil.Level > EXPERIANCELEVEL ) then
      FrmIDSoc.SendGameTimeOfTimeCardUser (UserId, contime div 60);
end;

{ SendSocket - 发送套接字数据
  功能: 向客户端发送数据包
  参数:
    pmsg - 消息指针
    body - 消息体 }
procedure TUserHuman.SendSocket (pmsg: PTDefaultMessage; body: string);
var
   packetlen: integer;  // 包长度
   header: TMsgHeader;  // 消息头
   pbuf: PChar;         // 缓冲区
begin
   pbuf := nil;
   try
   header.Code := integer($aa55aa55);
   header.SNumber := Userhandle;
   header.UserGateIndex := UserGateIndex;
   header.Ident := GM_DATA;

   if pmsg <> nil then begin
      // 有消息和消息体
      if body <> '' then begin
         header.Length := sizeof(TDefaultMessage) + Length(body) + 1;
         packetlen := sizeof(TMsgHeader) + header.Length;
         GetMem (pbuf, packetlen + 4);
         Move (packetlen, pbuf^, 4);
         Move (header, (@pbuf[4])^, sizeof(TMsgHeader));
         Move (pmsg^, (@pbuf[4+sizeof(TMsgHeader)])^, sizeof(TDefaultMessage));
         Move ((@body[1])^, (@pbuf[4+sizeof(TMsgHeader)+sizeof(TDefaultMessage)])^, Length(body)+1);
      end else begin
         // 只有消息
         header.Length := sizeof(TDefaultMessage);
         packetlen := sizeof(TMsgHeader) + header.Length;
         GetMem (pbuf, packetlen + 4);
         Move (packetlen, pbuf^, 4);
         Move (header, (@pbuf[4])^, sizeof(TMsgHeader));
         Move (pmsg^, (@pbuf[4+sizeof(TMsgHeader)])^, sizeof(TDefaultMessage));
      end;
   end else begin
      // 简单消息
      if body <> '' then begin
         header.Length := - (Length(body) + 1);
         packetlen := sizeof(TMsgHeader) + abs(header.Length);
         GetMem (pbuf, packetlen + 4);
         Move (packetlen, pbuf^, 4);
         Move (header, (@pbuf[4])^, sizeof(TMsgHeader));
         Move ((@body[1])^, (@pbuf[4+sizeof(TMsgHeader)])^, Length(body)+1);
      end;
   end;

   RunSocket.SendUserSocket (GateIndex, pbuf);
   except
      MainOutMessage ('Exception SendSocket..');
   end;
end;

{ SendDefMessage - 发送默认消息
  功能: 发送标准格式消息
  参数:
    msg - 消息类型
    recog, param, tag, series - 参数
    addstr - 附加字符串 }
procedure TUserHuman.SendDefMessage (msg, recog, param, tag, series: integer; addstr: string);
begin
   Def := MakeDefaultMsg (msg, recog, param, tag, series);
   if addstr <> '' then
      SendSocket (@Def, EncodeString (addstr))
   else SendSocket (@Def, '');
end;

{ GuildRankChanged - 行会联盟变更
  功能: 更新行会联盟信息
  参数:
    rank - 联盟等级
    rname - 联盟名称 }
procedure  TUserHuman.GuildRankChanged (rank: integer; rname: string);
begin
   GuildRank := rank;
   GuildRankName := rname;
   SendMsg (self, RM_CHANGEGUILDNAME, 0, 0, 0, 0, '');
function  TUserHuman.TurnXY (x, y, dir: integer): Boolean;
begin
   Result := FALSE;
   if (x = self.CX) and (y = self.CY) then begin
      self.Dir := dir;
      if Walk(RM_TURN) then begin
         Result := TRUE;
      end;
   end;
end;

{ WalkXY - 走到指定位置
  功能: 走到指定坐标
  参数:
    x, y - 目标坐标
  返回值: 是否成功 }
function  TUserHuman.WalkXY (x, y: integer): Boolean;
var
   ndir, oldx, oldy, dis: integer;  // 方向、坐标
   allowdup: Boolean;               // 允许重叠
begin
   Result := FALSE;

   // 加速检测
   if GetTickCount - LatestWalkTime < 600 then begin
      Inc (WalkTimeOverCount);
      Inc (WalkTimeOverSum);
   end else begin
      WalkTimeOverCount := 0;
      if WalkTimeOverSum > 0 then
         Dec (WalkTimeOverSum);
   end;

   LatestWalkTime := GetTickCount;

   if (WalkTimeOverCount < 4) and (WalkTimeOverSum < 6) then begin
      SpaceMoved := FALSE;
      oldx := self.CX;
      oldy := self.CY;
      ndir := GetNextDirection (CX, CY, x, y);

      allowdup := TRUE;  // 平时允许重叠

      if WalkTo (ndir, allowdup) then begin
         if SpaceMoved or (CX = x) and (CY = y) then
            Result := TRUE;
         Dec (HealthTick, 10);   //20
      end else begin           //걷기 실패
         WalkTimeOverCount := 0;
         WalkTimeOverSum := 0;
      end;
      end else begin
      Inc (SpeedHackTimerOverCount);
      if SpeedHackTimerOverCount > 8 then
         EmergencyClose := TRUE;

      if BoViewHackCode then
         MainOutMessage ('[11002-Walk] ' + UserName + ' ' + TimeToStr(Time));
   end;
end;

function  TUserHuman.RunXY (x, y: integer): Boolean;
var
   ndir: byte;
   dis: integer;
   allowdup: Boolean;
begin
   Result := FALSE;
   if GetTickCount - LatestWalkTime < 600 then begin
      Inc (WalkTimeOverCount);
      Inc (WalkTimeOverSum);
   end else begin
      WalkTimeOverCount := 0;
      if WalkTimeOverSum > 0 then
         Dec (WalkTimeOverSum);
   end;

   //dis := GetTickCount - LatestWalkTime;
   //MainOutMessage (IntToStr(dis));

   LatestWalkTime := GetTickCount;

   if (WalkTimeOverCount < 4) and (WalkTimeOverSum < 6) then begin
      SpaceMoved := FALSE;
      ndir := GetNextDirection (CX, CY, x, y);

      allowdup := TRUE;  //평상시에는 뛸때 겹칠 수 있음
      if UserCastle.BoCastleUnderAttack then begin  //공성전 중인 경우
         if BoInFreePKArea then //프리피케이존(전쟁터)에 있음, 공성 지역에 있음
            allowdup := FALSE;  //공성전 지역에서는 겹칠 수 없음 
      end;

      if RunTo (ndir, allowdup) then begin
         if BoFixedHideMode then begin //고정 은신술..
            if BoHumHideMode then begin  //이동한경우에는 은신술이 풀린다.
               StatusArr[STATE_TRANSPARENT] := 1;
            end;
         end;
         if SpaceMoved or (CX = x) and (CY = y) then
            Result := TRUE;
         Dec (HealthTick, 60); //150
         Dec (SpellTick, 10);  SpellTick := _MAX(0, SpellTick);
         Dec (PerHealth);
         Dec (PerSpell);
      end else begin
         WalkTimeOverCount := 0;
         WalkTimeOverSum := 0;
      end;
   end else begin
      // 加速检测
      Inc (SpeedHackTimerOverCount);
      if SpeedHackTimerOverCount > 8 then
         EmergencyClose := TRUE;
      if BoViewHackCode then
         MainOutMessage ('[11002-Run] ' + UserName + ' ' + TimeToStr(Time));
   end;
end;

{ GetRandomMineral - 获取随机矿石
  功能: 随机获得一种矿石 }
procedure TUserHuman.GetRandomMineral;
   // 获取纯度
   function GetPurity: integer;
   begin
      Result := 3000 + Random(13000);
      if Random(20) = 0 then
         Result := Result + Random(10000);
   end;
var
   pi: PTUserItem;  // 物品指针
begin
   if Itemlist.Count < MAXBAGITEM then begin
      case Random(120) of
         1..2:  // 金矿石
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__GoldStone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         3..20:  // 银矿石
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__SilverStone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         21..45:  // 铁矿石
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__SteelStone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         46..56:  // 黑铁
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName ('붚屆웁柯', pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         else  // 铜矿石
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName ('階웁', pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
      end;
   end;
end;

{ GetRandomGems - 获取随机宝石
  功能: 随机获得一种宝石 }
procedure TUserHuman.GetRandomGems;
   // 获取纯度
   function GetPurity: integer;
   begin
      Result := 3000 + Random(13000);
      if Random(20) = 0 then
         Result := Result + Random(10000);
   end;
var
   pi: PTUserItem;  // 物品指针
begin
   if Itemlist.Count < MAXBAGITEM then begin
      case Random(120) of
         1..2:  // 白金
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__Gem1Stone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         3..20:  // 软玉
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__Gem2Stone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         21..45:  // 红玉石
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__Gem4Stone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
         else  // 紫水晶
            begin
               new (pi);
               if UserEngine.CopyToUserItemFromName (__Gem3Stone, pi^) then begin
                  //광석의 순도 적용....
                  pi.Dura := GetPurity;
                  ItemList.Add (pi);
                  WeightChanged;
                  SendAddItem (pi^);
               end else
                  Dispose (pi);
            end;
      end;
   end;
end;

{ DigUpMine - 挖矿
  功能: 挖掘矿石
  参数:
    x, y - 坐标
  返回值: 是否成功 }
function  TUserHuman.DigUpMine (x, y: integer): Boolean;
var
   event, ev2: TEvent;  // 事件
   desc: string;        // 描述
begin
   Result := FALSE;
   desc := '';
   event := TEvent (PEnvir.GetEvent (x, y));
   if event <> nil then begin
      if (event.EventType = ET_MINE) or (event.EventType = ET_MINE2) then
         if TStoneMineEvent(event).MineCount > 0 then begin
            TStoneMineEvent(event).MineCount := TStoneMineEvent(event).MineCount - 1;
            // 挖掘成功
            if Random(4) = 0 then begin
               ev2 := TEvent (PEnvir.GetEvent (CX, CY));
               if ev2 = nil then begin
                  ev2 := TPileStones.Create (PEnvir, CX, CY, ET_PILESTONES, 5 * 60 * 1000, TRUE);
                  EventMan.AddEvent (ev2);
               end else begin
                  if ev2.EventType = ET_PILESTONES then
                     TPileStones(ev2).EnlargePile;
               end;
               // 获得矿石
               if Random(12) = 0 then begin
                  if event.EventType = ET_MINE then GetRandomMineral
                  else GetRandomGems;
               end;
               desc := '1';
               DoDamageWeapon (5+Random(15));
               Result := TRUE;
            end;
         end else begin
            // 10分钟后重新填充
            if GetTickCount - TStoneMineEvent(event).RefillTime > 10 * 60 *1000 then
               TStoneMineEvent(event).Refill;
         end;
   end;
   SendRefMsg (RM_HEAVYHIT, self.Dir, CX, CY, 0, desc);
end;

{ TargetInSwordLongAttackRange - 目标在剑法远程攻击范围内
  功能: 检查是否有目标在2格远的攻击范围内
  返回值: 是否有目标 }
function  TUserHuman.TargetInSwordLongAttackRange: Boolean;
var
   i, j, k, xx, yy: integer;  // 循环、坐标
   pm: PTMapInfo;             // 地图信息
   inrange: Boolean;          // 是否在范围内
   cret: TCreature;           // 生物
begin
   Result := FALSE;
   // 检查2格前方
   if GetNextPosition (PEnvir, CX, CY, Dir, 2, xx, yy) then begin
      for i:=xx-1 to xx+1 do
         for j:=yy-1 to yy+1 do begin
            inrange := PEnvir.GetMapXY (i, j, pm);
            if inrange then begin
               if pm.ObjList <> nil then
                  for k:=0 to pm.ObjList.Count-1 do begin
                     cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                     if cret <> nil then
                        if (not cret.BoGhost) and (not cret.Death) and (cret <> self) and ((abs(CX-cret.CX) >= 2) or (abs(CY-cret.CY) >= 2)) then begin
                           if IsProperTarget (cret) then begin
                              Result := TRUE;
                              exit;
                           end;
                        end;
                  end;
            end;
         end;
   end;
end;

{ HitXY - 攻击指定位置
  功能: 在指定位置执行攻击
  参数:
    hitid - 攻击类型
    x, y - 坐标
    dir - 方向
  返回值: 是否成功 }
function  TUserHuman.HitXY (hitid, x, y, dir: integer): Boolean;
var
   fx, fy: integer;    // 前方坐标
   pstd: PTStdItem;    // 标准物品
begin
   Result := FALSE;
   // 加速检测
   if GetTickCount - LatestHitTime < longword(900) - longword(HitSpeed * 60) then begin
      Inc (HitTimeOverCount);
      Inc (HitTimeOverSum);
   end else begin
      HitTimeOverCount := 0;
      if HitTimeOverSum > 0 then
         Dec (HitTimeOverSum);
   end;

   if (HitTimeOverCount < 4) and (HitTimeOverSum < 6) then begin
      if not self.Death then begin
         // 只能在自己位置攻击
         if (x = self.CX) and (y = self.CY) then begin
            Result := TRUE;
            LatestHitTime := GetTickCount;

            // 检查挖矿
            if (hitid = CM_HEAVYHIT) and
               (UseItems[U_WEAPON].Index > 0) and
               GetFrontPosition (self, fx, fy)
            then begin
               if not PEnvir.CanWalk (fx, fy, FALSE) then begin
                  pstd := UserEngine.GetStdItem (UseItems[U_WEAPON].Index);
                  if pstd <> nil then begin
                     // 镐子
                     if pstd.Shape = 19 then begin
                        if DigUpMine (fx, fy) then
                           SendSocket (nil, '=DIG');
                        Dec (HealthTick, 30);
                        Dec (SpellTick, 50);  SpellTick := _MAX(0, SpellTick);
                        Dec (PerHealth, 2);
                        Dec (PerSpell, 2);
                        exit;
                     end;
                  end;
               end;
            end;
            // 执行各种攻击
            if hitid = CM_HIT then inherited HitHit (nil, HM_HIT, dir);
            if hitid = CM_HEAVYHIT then inherited HitHit (nil, HM_HEAVYHIT, dir);
            if hitid = CM_BIGHIT then inherited HitHit (nil, HM_BIGHIT, dir);
            if hitid = CM_POWERHIT then inherited HitHit (nil, HM_POWERHIT, dir);
            if hitid = CM_LONGHIT then inherited HitHit (nil, HM_LONGHIT, dir);
            if hitid = CM_WIDEHIT then inherited HitHit (nil, HM_WIDEHIT, dir);
            if hitid = CM_FIREHIT then inherited HitHit (nil, HM_FIREHIT, dir);
            if hitid = CM_CROSSHIT then inherited HitHit (nil, HM_CROSSHIT, dir);

            // 烈火剑法触发检测
            if (PPowerHitSkill <> nil) and (UseItems[U_WEAPON].Index > 0) then begin
               Dec (AttackSkillCount);
               if AttackSkillPointCount = AttackSkillCount then begin
                  BoAllowPowerHit := TRUE;
                  SendSocket (nil, '+PWR');
               end;
               if AttackSkillCount <= 0 then begin
                  AttackSkillCount := 7 - PPowerHitSkill.Level;
                  AttackSkillPointCount := Random(AttackSkillCount);
               end;
            end;

            // 消耗体力和魔法
            Dec (HealthTick, 30);
            Dec (SpellTick, 100);  SpellTick := _MAX(0, SpellTick);
            Dec (PerHealth, 2);
            Dec (PerSpell, 2);
         end else
            Result := FALSE;
      end;
   end else begin
      // 加速检测
      LatestHitTime := GetTickCount;
      Inc (SpeedHackTimerOverCount);
      if SpeedHackTimerOverCount > 8 then
         EmergencyClose := TRUE;
      if BoViewHackCode then
         MainOutMessage ('[11000-Hit] ' + UserName + ' ' + TimeToStr(Time));
   end;
end;

{ GetMagic - 获取魔法
  功能: 根据魔法ID获取魔法信息
  参数:
    mid - 魔法ID
  返回值: 魔法指针 }
function TUserHuman.GetMagic (mid: integer): PTUserMagic;
var
   i: integer;  // 循环变量
begin
   Result := nil;
   for i:=0 to MagicList.Count-1 do begin
      if PTUserMagic(MagicList[i]).pDef.MagicId = mid then begin
         Result := PTUserMagic(MagicList[i]);
         break;
      end;
   end;
end;

{ SpellXY - 施放魔法
  功能: 在指定位置施放魔法
  参数:
    magid - 魔法ID
    targetx, targety - 目标坐标
    targcret - 目标生物
  返回值: 是否成功 }
function  TUserHuman.SpellXY (magid, targetx, targety, targcret: integer): Boolean;
var
   i, ndir, magnum, spell: integer;  // 方向、魔法消耗
   targ: TCreature;                  // 目标生物
   pum: PTUserMagic;                 // 魔法指针
   fail: Boolean;                    // 是否失败
begin
   Result := FALSE;
   // 麻痹状态无法施法
   if StatusArr[POISON_STONE] <> 0 then begin
      exit;
   end;

   // 施法延迟检测
   if GetTickCount - LatestSpellTime > longword(LatestSpellDelay) then
      SpellTimeOverCount := 0
   else
      Inc (SpellTimeOverCount);

   if SpellTimeOverCount < 2 then begin
      pum := nil;
      Dec (SpellTick, 450); SpellTick := _MAX(0, SpellTick);

      pum := GetMagic (magid);
      if pum <> nil then begin

         // 剑法和魔法延迟不同
         if MagicMan.IsSwordSkill (pum.MagicId) then
            LatestSpellDelay := 0
         else
            LatestSpellDelay := pum.pDef.DelayTime + 800;
         LatestSpellTime := GetTickCount;

         case pum.MagicId of
            SWD_LONGHIT:  // 刺杀剑法
               begin
                  if PLongHitSkill <> nil then begin
                     if not BoAllowLongHit then begin
                        SetAllowLongHit (TRUE);
                        SendSocket (nil, '+LNG');  //원거리 공격을 하게 한다.
                     end else begin
                        SetAllowLongHit (FALSE);
                        SendSocket (nil, '+ULNG');  //원거리 공격을 하게 한다.
                     end;
                  end;
                  Result := TRUE;
               end;
            SWD_WIDEHIT:  //반월검법
               begin
                  if PWideHitSkill <> nil then begin
                     if not BoAllowWideHit then begin
                        if BoAllowCrossHit then begin
                           SetAllowCrossHit (FALSE);
                           SendSocket (nil, '+UCRS');  // 광풍참 사용안함
                        end;
                        SetAllowWideHit (TRUE);
                        SendSocket (nil, '+WID');  // 반월검법 사용
                     end else begin
                        SetAllowWideHit (FALSE);
                        SendSocket (nil, '+UWID');  // 반월검법 사용안함
                     end;
                  end;
                  Result := TRUE;
               end;
            SWD_FIREHIT:  //염화결
               begin
                  if PFireHitSkill <> nil then begin
                     if SetAllowFireHit then begin
                        spell := GetSpellPoint (pum);
                        if (WAbil.MP >= spell) then begin
                           if (spell > 0) then begin
                              DamageSpell (spell);
                              HealthSpellChanged;
                           end;
                           SendSocket (nil, '+FIR');
                        end else
                           ;
                     end;
                     Result := TRUE;
                  end;
               end;
            SWD_RUSHRUSH:  //무태보
               begin
                  Result := TRUE;
                  if GetTickCount - LatestRushRushTime > 3000 then begin
                     LatestRushRushTime := GetTickCount;
                     Dir := targetx; //방향 전환
                     //if GetTickCount - LatestRushRushTime >= 3000
                     spell := GetSpellPoint (pum);
                     if (spell > 0) then begin
                        if (WAbil.MP >= spell) then begin
                           DamageSpell (spell);
                           HealthSpellChanged;
                        end else
                           exit;  //마력모자람
                     end;
                     if CharRushRush (Dir, pum.Level) then begin
                        if (pum.Level < 3) then
                           if Abil.Level >= pum.pDef.NeedLevel[pum.Level] then begin
                              //수련레벨에 도달한 경우
                              TrainSkill (pum, 1 + Random(3));
                              if not CheckMagicLevelup (pum) then
                                 SendDelayMsg (self, RM_MAGIC_LVEXP, 0, pum.pDef.MagicId, pum.Level, pum.CurTrain, '', 1000);
                           end;
                     end;
                  end;
               end;
            // 2003/03/15 신규무공
            SWD_CROSSHIT:   // 광풍참
               begin
                  if PCrossHitSkill <> nil then begin
                     if not BoAllowCrossHit then begin
                        if BoAllowWideHit then begin
                           SetAllowWideHit (FALSE);
                           SendSocket (nil, '+UWID');  // 반월검법 사용안함
                        end;
                        SetAllowCrossHit (TRUE);
                        SendSocket (nil, '+CRS');  // 광풍참 사용
                     end else begin
                        SetAllowCrossHit (FALSE);
                        SendSocket (nil, '+UCRS');  // 광풍참 사용안함
                     end;
                  end;
                  Result := TRUE;
               end;
            else begin
               ndir := GetNextDirection (CX, CY, targetx, targety);
               Dir := ndir;
               targ := nil;
               if CretInNearXY (TCreature(targcret), targetx, targety) then begin
                  targ := TCreature (targcret);
                  targetx := targ.CX;
                  targety := targ.CY;
               end;
               if not DoSpell (pum, targetx, targety, targ) then
                  SendRefMsg (RM_MAGICFIRE_FAIL, 0, 0, 0, 0, '');
               Result := TRUE;
            end;

         end;
      end;
   end else begin
      pum := GetMagic (magid);
      if pum <> nil then begin
         if MagicMan.IsSwordSkill (pum.MagicId) then begin
            SpellTimeOverCount := 0;
            exit;  //검법키..
         end;    
      end;
      LatestSpellTime := GetTickCount;

      Inc (SpeedHackTimerOverCount);
      if SpeedHackTimerOverCount > 8 then
         EmergencyClose := TRUE;
         
      if BoViewHackCode then
         MainOutMessage ('[11001-Mag] ' + UserName + ' ' + TimeToStr(Time));

      //SysMsg ('쉥굳셩쩌槨벨와넋駕賈痰諒，', 0);
      //SysMsg ('헝鬧雷퀭돨琅뵀쉥굳꿴룐。', 0);
      //MakePoison (POISON_DECHEALTH, 30, 1);
      //MakePoison (POISON_STONE, 5, 0); //중독에 걸리게 함
      //SysMsg (' CODE=11001헝宅踏狗밗잿逃젬溝(mir2@tgl.com.tw) ', 0);
      //EmergencyClose := TRUE;
   end;
end;

{ SitdownXY - 坐下
  功能: 在指定位置坐下
  参数:
    x, y - 坐标
    dir - 方向
  返回值: 是否成功 }
function  TUserHuman.SitdownXY (x, y, dir: integer): Boolean;
begin
   SendRefMsg (RM_SITDOWN, 0, 0, 0, 0, '');
   Result := TRUE;
end;


{----------------------------------------------------------}
{ GM命令处理 }

{ ChangeSkillLevel - 改变技能等级
  功能: 修改指定技能的等级
  参数:
    magname - 技能名称
    lv - 等级 }
procedure TUserHuman.ChangeSkillLevel (magname: string; lv: byte);
var
   i: integer;
begin
   lv := _MIN(3, lv);
   for i:=MagicList.Count-1 downto 0 do begin
      if CompareText (PTUserMagic(MagicList[i]).pDef.MagicName, magname) = 0 then begin
         PTUserMagic(MagicList[i]).Level := lv;
         SendMsg (self, RM_MAGIC_LVEXP, 0,
                  PTUserMagic(MagicList[i]).pDef.MagicId,
                  PTUserMagic(MagicList[i]).Level,
                  PTUserMagic(MagicList[i]).CurTrain,
                  '');
         SysMsg (magname + '錦조된섬긴뫘槨' + IntToStr(lv) + '섬, 瓜제댕류瓊�', 1);
      end;
   end;
end;

{ CmdMakeFullSkill - 满级技能命令
  功能: 将技能设为指定等级 }
procedure TUserHuman.CmdMakeFullSkill (magname: string; lv: byte);
begin
   ChangeSkillLevel (magname, lv);
end;

{ CmdMakeOtherChangeSkillLevel - 修改他人技能等级
  功能: 修改其他玩家的技能等级 }
procedure TUserHuman.CmdMakeOtherChangeSkillLevel (who, magname: string; lv: byte);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (who);
   if hum <> nil then begin
      hum.ChangeSkillLevel (magname, lv);
   end else
      SysMsg (who + ' 轟랬꿴璂', 0);
end;

{ CmdDeletePKPoint - 清除PK点
  功能: 清除指定玩家的PK点 }
procedure TUserHuman.CmdDeletePKPoint (whostr: string);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (whostr);
   if hum <> nil then begin
      hum.PlayerKillingPoint := 0; //면죄
      hum.ChangeNameColor;
      SysMsg (whostr + ' : PK point = 0.', 1);
   end else
      SysMsg (whostr + ' 轟랬꿴璂', 0);
end;

{ CmdSendPKPoint - 查询PK点
  功能: 查询指定玩家的PK点 }
procedure TUserHuman.CmdSendPKPoint (whostr: string);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (whostr);
   if hum <> nil then begin
      SysMsg (whostr + ' PK point = ' + IntToStr(hum.PlayerKillingPoint), 1)
   end else
      SysMsg (whostr + ' 轟랬꿴璂', 0);
end;

{ CmdChangeJob - 改变职业
  功能: 改变玩家职业 }
procedure TUserHuman.CmdChangeJob (jobname: string);
begin
   if CompareText (jobname, '战士') = 0 then Job := 0;
   if CompareText (jobname, '法师') = 0 then Job := 1;
   if CompareText (jobname, '道士') = 0 then Job := 2;
end;

{ CmdChangeSex - 改变性别
  功能: 切换玩家性别 }
procedure TUserHuman.CmdChangeSex;
begin
   if Sex = 0 then Sex := 1
   else Sex := 0;
end;

{ CmdCallMakeMonster - 创建怪物
  功能: 在前方创建指定怪物 }
procedure TUserHuman.CmdCallMakeMonster (monname, param: string);
var
   nx, ny, i, count: integer;
begin
   count := _MIN (100, Str_ToInt (param, 1));
   GetFrontPosition (self, nx, ny);
   for i:=0 to count-1 do begin
      UserEngine.AddCreatureSysop (MapName, nx, ny, monname);
   end;
end;

{ CmdCallMakeSlaveMonster - 创建召唤怪物
  功能: 创建召唤怪物作为嬋物 }
procedure TUserHuman.CmdCallMakeSlaveMonster (monname, param: string; momlv: byte);
var
   nx, ny, i, count: integer;
   cret: TCreature;
begin
   count := Str_ToInt (param, 1);
   if not (momlv in [0..7]) then momlv := 0;
   for i:=0 to count-1 do begin
      if SlaveList.Count < 20 then begin
         GetFrontPosition (self, nx, ny);
         cret := UserEngine.AddCreatureSysop (MapName, nx, ny, monname);
         if cret <> nil then begin
            //if cret.LifeAttrib <> LA_UNDEAD then begin
               cret.Master := self;  //소환몹을 뺏어온다.
               cret.MasterRoyaltyTime := GetTickCount + 24 * 60 * 60 * 1000;
               cret.SlaveMakeLevel := 3;
               cret.SlaveExpLevel := momlv;
               cret.UserNameChanged;
               // 2003/03/04 리콜몹 능력치 재계산
               cret.RecalcAbilitys;
               SlaveList.Add (cret);
            //end;
         end;
      end;
   end;
end;

{ CmdMissionSetting - 设置任务
  功能: 设置任务目标点 }
procedure TUserHuman.CmdMissionSetting (xstr, ystr: string);
var
   xx, yy: integer;
begin
   if xstr = '' then begin
      BoSysHasMission := FALSE;
      SysMsg ('훨蛟呵겨', 1);
   end else begin
      xx := Str_ToInt (xstr, 0);
      yy := Str_ToInt (ystr, 0);
      BoSysHasMission := TRUE;
      SysMission_Map := MapName;
      SysMission_X := xx;
      SysMission_Y := yy;
      SysMsg ('훨蛟묑샌커깃' + MapName + ' ' + IntToStr(xx) + ':' + IntToStr(yy), 1);
   end;
end;

{ CmdCallMakeMonsterXY - 在指定位置创建怪物
  功能: 在指定坐标创建怪物 }
procedure TUserHuman.CmdCallMakeMonsterXY (xstr, ystr, monname, countstr: string);
var
   i, count, xx, yy: integer;
   penv: TEnvirnoment;
   cret: TCreature;
begin
   if not BoSysHasMission then begin
      SysMsg ('청唐寧땍훨蛟', 0);
      exit;
   end;
   count := _MIN(500, Str_ToInt (countstr, 0));
   xx := Str_ToInt (xstr, 0);
   yy := Str_ToInt (ystr, 0);
   penv := GrobalEnvir.GetEnvir (SysMission_Map);
   if (penv <> nil) and (count > 0) and (xx > 0) and (yy > 0) then begin
      for i:=0 to count-1 do begin
         cret := UserEngine.AddCreatureSysop (SysMission_Map, xx, yy, monname);
         if (cret <> nil) and (BoSysHasMission) then begin
            cret.BoHasMission := TRUE;
            cret.Mission_X := SysMission_X;
            cret.Mission_Y := SysMission_Y;
         end;
      end;
      SysMsg (SysMission_Map + ' ' + IntToStr(xx) + ':' + IntToStr(yy) + ' => ' + monname + ' ' + IntToStr(count) + '怜', 1);
   end else
      SysMsg ('츱즈댄轎： X Y 밍膠 鑒좆', 0);
end;

{ CmdMakeItem - 创建物品
  功能: 创建指定物品 }
procedure TUserHuman.CmdMakeItem (itmname: string; count: integer);
var
   i: integer;
   pu: PTUserItem;
   pstd: PTStdItem;
begin
   for i:=0 to count-1 do begin
      if ItemList.Count >= MAXBAGITEM then break;
      new (pu);
      if UserEngine.CopyToUserItemFromName (itmname, pu^) then begin
         pstd := UserEngine.GetStdItem (pu.Index);

         if pstd.Price >= 15000 then begin  //가격이 15000원 이상은 superadmin만 만들 수 있다.
            if not BoTestServer and (UserDegree < UD_SUPERADMIN) then begin
               Dispose (pu);
               exit;
            end;
         end;

         //pu.Dura := Round((pu.Dura / 100) * (100 + Random(100)));

         if Random(10) = 0 then
            UserEngine.RandomUpgradeItem (pu);

         //미지 시리즈 아이템인 경우
         if pstd.StdMode in [15,19,20,21,22,23,24,26,52,53,54] then begin
            if (pstd.Shape = RING_OF_UNKNOWN) or
               (pstd.Shape = BRACELET_OF_UNKNOWN) or
               (pstd.Shape = HELMET_OF_UNKNOWN)
            then begin
               UserEngine.RandomSetUnknownItem (pu);
            end;      
         end;

         ItemList.Add (pu);
         SendAddItem (pu^);

         if BoEcho then begin
            MainOutMessage ('MakeItem] ' + UserName + ' : ' + itmname + ' ' + IntToStr(pu.MakeIndex));
            //로그남김
            AddUserLog ('5'#9 + //운만_
                        MapName + ''#9 +
                        IntToStr(CX) + ''#9 +
                        IntToStr(CY) + ''#9 +
                        UserName + ''#9 +
                        UserEngine.GetStdItemName (pu.Index) + ''#9 +
                        IntToStr(pu.MakeIndex) + ''#9 +
                        '1'#9 +
                        '0');
         end;
      end else begin
         Dispose (pu);
         break;
      end;
   end;
end;

{ CmdRefineWeapon - 精炼武器
  功能: 精炼武器属性 }
procedure TUserHuman.CmdRefineWeapon (dc, mc, sc, acc: integer);
begin
   if dc + mc + sc > 10 then exit;
   if UseItems[U_WEAPON].Index > 0 then begin
      UseItems[U_WEAPON].Desc[0] := dc;
      UseItems[U_WEAPON].Desc[1] := mc;
      UseItems[U_WEAPON].Desc[2] := sc;
      UseItems[U_WEAPON].Desc[5] := acc;
      SendUpdateItem (UseItems[U_WEAPON]);
      RecalcAbilitys;
      SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
      SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
   end;
end;

{ CmdDeleteUserGold - 删除金币
  功能: 删除指定玩家的金币 }
procedure TUserHuman.CmdDeleteUserGold (whostr, goldstr: string);
var
   hum: TUserHuman;
   igold, svidx: integer;
begin
   hum := UserEngine.GetUserHuman (whostr);
   igold := Str_ToInt (goldstr, 0);
   if igold <= 0 then exit;
   if hum <> nil then begin
      if hum.Gold > igold then hum.Gold := hum.Gold - igold
      else begin
         igold := hum.Gold; //실제로 사라진양
         hum.Gold := 0;
      end;
      hum.GoldChanged;
      SysMsg (whostr + '돨쏜귑' + IntToStr(igold) + '굳숑�', 1);
      //로그남김
      AddUserLog ('13'#9 + //돈삭_
                  MapName + ''#9 +
                  IntToStr(CX) + ''#9 +
                  IntToStr(CY) + ''#9 +
                  UserName + ''#9 +
                  '쏜귑'#9 +
                  IntToStr(igold) + ''#9 +
                  '1'#9 +
                  whostr);
   end else begin
      if UserEngine.FindOtherServerUser (whostr, svidx) then begin
         SysMsg (whostr + '角' + IntToStr(svidx) + '뚤렘瞳컸憩륩蛟포�。', 1);
      end else
         FrontEngine.ChangeUserInfos (UserName, whostr, -igold);
      //SysMsg (whostr + '돨쏜귑' + IntToStr(igold) + '쏜귑굳숑�。', 1);
   end;
end;

{ CmdAddUserGold - 增加金币
  功能: 给指定玩家增加金币 }
procedure TUserHuman.CmdAddUserGold (whostr, goldstr: string);
var
   hum: TUserHuman;
   igold, svidx: integer;
begin
   hum := UserEngine.GetUserHuman (whostr);
   igold := Str_ToInt (goldstr, 0);
   if igold <= 0 then exit;
   if hum <> nil then begin
      if hum.Gold + igold < AvailableGold then hum.Gold := hum.Gold + igold
      else begin
         igold := AvailableGold - hum.Gold; //실제로 사라진양
         hum.Gold := AvailableGold;
      end;
      hum.GoldChanged;
      SysMsg (whostr + '藤속' + IntToStr(igold) + '쏜귑', 1);
      //로그남김
      AddUserLog ('14'#9 + //돈추
                  MapName + ''#9 +
                  IntToStr(CX) + ''#9 +
                  IntToStr(CY) + ''#9 +
                  UserName + ''#9 +
                  '쏜귑'#9 +
                  IntToStr(igold) + ''#9 +
                  '1'#9 +
                  whostr);
   end else begin
      if UserEngine.FindOtherServerUser (whostr, svidx) then begin
         SysMsg (whostr + '角' + IntToStr(svidx) + '뚤렘瞳컸憩륩蛟포�。', 1);
      end else
         FrontEngine.ChangeUserInfos (UserName, whostr, igold);
      //SysMsg ('轟랬璣冷', 0);
   end;
end;

{ RCmdUserChangeGoldOk - 金币变更确认
  功能: 确认金币变更成功 }
procedure TUserHuman.RCmdUserChangeGoldOk (whostr: string; igold: integer);
var
   cmdstr, msgstr: string;
begin
   if igold > 0 then begin
      cmdstr := '14'#9; //돈추_;
      msgstr := '藤속供냥';
   end else begin
      cmdstr := '13'#9; //돈삭_;
      msgstr := '�숑供냥';
      igold := -igold;
   end;
   SysMsg (whostr + '藤속' + IntToStr(igold) + '쏜귑' + msgstr, 1);
   //로그 남김
   AddUserLog (cmdstr +
               MapName + ''#9 +
               IntToStr(CX) + ''#9 +
               IntToStr(CY) + ''#9 +
               UserName + ''#9 +
               '쏜귑'#9 +
               IntToStr(igold) + ''#9 +
               '1'#9 +
               whostr);
end;

{ CmdFreeSpaceMove - 自由传送
  功能: 传送到指定地图坐标 }
procedure TUserHuman.CmdFreeSpaceMove (map, xstr, ystr: string);
var
   x, y: integer;
   pev: TEnvirnoment;
begin
   pev := GrobalEnvir.GetEnvir (map);
   if pev <> nil then begin
      x := Str_ToInt(xstr, 0);
      y := Str_ToInt(ystr, 0);
      if pev.CanWalk (x, y, TRUE) then begin
         SpaceMove (map, x, y, 0);
      end else
         SysMsg ('Fail', 0);
   end else
      SysMsg ('Fail', 0);
end;

{ CmdRushAttack - 冲锋攻击
  功能: 执行冲锋攻击 }
procedure TUserHuman.CmdRushAttack;
begin
   CharRushRush (Dir, 3);
end;

{ CmdManLevelChange - 修改等级
  功能: 修改指定玩家的等级 }
procedure TUserHuman.CmdManLevelChange (man: string; level: integer);
var
   oldlv: integer;
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (man);
   if hum <> nil then begin
      MainOutMessage ('ChgLv] ' + man + ' : ' + IntToStr(hum.Abil.Level) + ' -> ' + IntToStr(level) + ' by ' + UserName);
      oldlv := hum.Abil.Level;
      hum.ChangeLevel (level);
      hum.HasLevelUp (oldlv);
      //로그를 남긴다
      AddUserLog ('17'#9 + //타레_
                  man + ''#9 +
                  IntToStr(oldlv) + ''#9 +
                  IntToStr(level) + ''#9 +
                  UserName + ''#9 +
                  '0'#9 +
                  '0'#9 +
                  '1'#9 +
                  '0');
      SysMsg ('[된섬딧憐] ' + man + ' ' + IntToStr(oldlv) + '->' + IntToStr(level), 1);
   end;
end;

{ CmdManExpChange - 修改经验
  功能: 修改指定玩家的经验值 }
procedure TUserHuman.CmdManExpChange (man: string; exp: integer);
var
   hum: TUserHuman;
   oldexp: integer;
begin
   hum := UserEngine.GetUserHuman (man);
   if hum <> nil then begin
      MainOutMessage ('ChgExp] ' + man + ' : ' + IntToStr(hum.Abil.Exp) + ' -> ' + IntToStr(Exp) + ' by ' + UserName);
      oldexp := hum.Abil.Exp;
      hum.Abil.Exp := exp;  //ChangeLevel (level);
      hum.HasLevelUp (Abil.Level);
      //로그를 남긴다
      AddUserLog ('18'#9 + //타레_
                  man + ''#9 +
                  IntToStr(oldexp) + ''#9 +
                  IntToStr(exp) + ''#9 +
                  UserName + ''#9 +
                  '0'#9 +
                  '0'#9 +
                  '1'#9 +
                  '0');
      SysMsg ('[쒔駱딧憐] ' + man + ' ' + IntToStr(exp), 1);
   end;
end;

{ CmdEraseItem - 删除物品
  功能: 删除背包中的指定物品 }
procedure TUserHuman.CmdEraseItem (itmname, countstr: string);
var
   i, k, count: integer;
   pu: PTUserItem;
begin
   count := Str_ToInt(countstr, 1);
   for k:=1 to count do begin
      for i:=0 to ItemList.Count-1 do begin
         pu := PTUserItem(ItemList[i]);
         if CompareText (UserEngine.GetStdItemName (pu.Index), itmname) = 0 then begin
            //로그남김
            AddUserLog ('6'#9 + //운삭_ +
                        MapName + ''#9 +
                        IntToStr(CX) + ''#9 +
                        IntToStr(CY) + ''#9 +
                        UserName + ''#9 +
                        UserEngine.GetStdItemName (pu.Index) + ''#9 +
                        IntToStr(pu.MakeIndex) + ''#9 +
                        '1'#9 +
                        '0');
            SendDelItem (pu^);
            Dispose (pu);
            ItemList.Delete (i);
            break;
         end;
      end;
   end;      
end;

{ CmdRecallMan - 召唤玩家
  功能: 将指定玩家传送到自己身边 }
procedure TUserHuman.CmdRecallMan (man: string);
var
   hum: TUserHuman;
   nx, ny, dx, dy: integer;
begin
   hum := UserEngine.GetUserHuman (man);
   if hum <> nil then begin
      if GetFrontPosition (self, nx, ny) then begin
         if GetRecallPosition (nx, ny, 3, dx, dy) then begin
            hum.SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
            hum.SpaceMove (MapName, dx, dy, 0); //무작위 공간이동
         end;
      end else
         SysMsg ('梁뻥呵겨', 0);
   end else
      SysMsg (man + ' 轟랬璣冷', 0);
end;

{ CmdReconnection - 重新连接
  功能: 重新连接到指定服务器 }
procedure TUserHuman.CmdReconnection (saddr, sport: string);
begin
   if (saddr <> '') and (sport <> '') then
      SendMsg (self, RM_RECONNECT, 0, 0, 0, 0, saddr + '/' + sport);
end;

{ CmdReloadGuild - 重新加载行会
  功能: 重新加载指定行会数据 }
procedure TUserHuman.CmdReloadGuild (gname: string);
var
   g: TGuild;
begin
   if ServerIndex = 0 then begin
      g := GuildMan.GetGuild (gname);
      if g <> nil then begin
         g.LoadGuild;
         SysMsg (gname + '：契삔뫘劤供냥。', 0);
         UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, gname);
      end;
   end else
      SysMsg ('侶몸츱즈怜콘賈痰瞳寮륩蛟포�。', 0);
end;

{ CmdReloadGuildAll - 重新加载所有行会
  功能: 重新加载所有行会数据 }
procedure TUserHuman.CmdReloadGuildAll (gname: string);
begin
   GuildMan.ClearGuildList;
   GuildMan.LoadGuildList;
   SysMsg ('已重新加载所有行会信息。', 1);
end;

{ CmdKickUser - 踢出玩家
  功能: 将指定玩家踢下线 }
procedure TUserHuman.CmdKickUser (uname: string);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (uname);
   if hum <> nil then begin
      hum.UserRequestClose := TRUE;
   end;
end;

{ CmdTingUser - 传送玩家回城
  功能: 将指定玩家传送回出生点 }
procedure TUserHuman.CmdTingUser (uname: string);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (uname);
   if hum <> nil then begin
      //hum.UserRequestClose := TRUE;
      hum.RandomSpaceMove (hum.HomeMap, 0);
   end else
      SysMsg (uname + ' 轟랬꿴冷', 0);
end;

{ CmdTingRangeUser - 传送范围内玩家
  功能: 将指定玩家周围的玩家传送回出生点 }
procedure TUserHuman.CmdTingRangeUser (uname, rangestr: string);
var
   i, range: integer;
   hum: TUserHuman;
   ulist: TList;
begin
   hum := UserEngine.GetUserHuman (uname);
   range := _MIN(Str_ToInt (rangestr, 2), 10);
   if hum <> nil then begin
      ulist := TList.Create;
      UserEngine.GetAreaUsers (hum.PEnvir, hum.CX, hum.CY, range, ulist);
      for i:=0 to ulist.Count-1 do begin
         hum := TUserHuman(ulist[i]);
         hum.RandomSpaceMove (hum.HomeMap, 0);
      end;
      ulist.Free;
   end else
      SysMsg (uname + ' 轟랬꿴冷', 0);
end;

{ CmdEraseMagic - 删除魔法
  功能: 删除指定魔法 }
procedure TUserHuman.CmdEraseMagic (magname: string);
var
   i: integer;
begin
   for i:=MagicList.Count-1 downto 0 do begin
      if CompareText (PTUserMagic(MagicList[i]).pDef.MagicName, magname) = 0 then begin
         SendDelMagic (PTUserMagic(MagicList[i]));
         Dispose (PTUserMagic(MagicList[i]));
         MagicList.Delete (i);
         break;
      end;
   end;
   RecalcAbilitys;
end;

{ CmdThisManEraseMagic - 删除他人魔法
  功能: 删除指定玩家的指定魔法 }
procedure TUserHuman.CmdThisManEraseMagic (whostr, magname: string);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (whostr);
   if hum <> nil then begin
      hum.CmdEraseMagic (magname);
   end else
      SysMsg (whostr + ' 轟랬꿴冷', 0);
end;

{ GuildDeclareWar - 行会宣战
  功能: 向指定行会宣战 }
procedure TUserHuman.GuildDeclareWar (gname: string);
var
   guild: TGuild;
   pgw: PTGuildWarInfo;
   flag: Boolean;
begin
   if IsGuildMaster then begin //문주만 사용할 수 있는 명령
      if ServerIndex <> 0 then begin
         SysMsg ('侶몸츱즈角꼇옵痰돨侶憩륩蛟포賈痰。', 0);
         exit;
      end;
      guild := GuildMan.GetGuild (gname);
      if guild <> nil then begin
         flag := FALSE;
         pgw := TGuild(MyGuild).DeclareGuildWar (guild);
         if pgw <> nil then begin
            if guild.DeclareGuildWar (TGuild(MyGuild)) = nil then begin
               pgw.WarStartTime := 0;  //타임아웃
            end else flag := TRUE;
         end;
         if flag then begin //성공
            UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
            UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, gname);
         end;
      end else
         SysMsg (gname + '契삔꼇닸瞳。', 0);
   end else
      SysMsg ('怜唐契삔밗잿逃꼽콘�헝。', 0);
end;

{ CmdCreateGuild - 创建行会
  功能: 创建新行会 }
procedure TUserHuman.CmdCreateGuild (gname, mastername: string);
var
   hum: TUserHuman;
   flag: Boolean;
begin
   if ServerIndex <> 0 then begin
      SysMsg ('侶몸츱즈怜콘賈痰瞳寮륩蛟포�。', 0);
      exit;
   end;
   flag := FALSE;
   hum := UserEngine.GetUserHuman (mastername);
   if hum <> nil then begin
      if GuildMan.GetGuildFromMemberName (mastername) = nil then begin
         if GuildMan.AddGuild (gname, mastername) then begin
            UserEngine.SendInterMsg (ISM_ADDGUILD, ServerIndex, gname + '/' + mastername);
            SysMsg ('속흙契삔' + gname + ' ' + '廊쳔훙' + ':' + mastername, 0);
            flag := TRUE;
         end;
      end;

      //문파정보를 다시 읽는다.
      with hum do begin
         MyGuild := GuildMan.GetGuildFromMemberName (UserName);
         if MyGuild <> nil then begin  //길드에 가입되어 있는 경우
            hum.GuildRankName := TGuild (MyGuild).MemberLogin (self, hum.GuildRank);
            //SendMsg (self, RM_CHANGEGUILDNAME, 0, 0, 0, 0, '');
         end;
      end;
   end;
   if not flag then
      SysMsg ('劤契삔눼접呵겨', 0);
end;

{ CmdDeleteGuild - 删除行会
  功能: 删除指定行会 }
procedure TUserHuman.CmdDeleteGuild (gname: string);
begin
   if ServerIndex <> 0 then begin
      SysMsg ('侶몸츱즈怜콘賈痰瞳寮륩蛟포。', 0);
      exit;
   end;
   if GuildMan.DelGuild (gname) then begin
      UserEngine.SendInterMsg (ISM_DELGUILD, ServerIndex, gname);
      SysMsg ('�뇜契삔' + gname, 0);
   end else SysMsg ('契삔�뇜呵겨', 0);
end;

{ CmdGetGuildMatchPoint - 获取行会比赛分数
  功能: 查询指定行会的比赛分数 }
procedure TUserHuman.CmdGetGuildMatchPoint (gname: string);
var
   guild: TGuild;
begin
   guild := GuildMan.GetGuild (gname);
   if guild <> nil then begin
      SysMsg (gname + '''s point : ' + IntToStr(guild.MatchPoint), 1);
   end else
      SysMsg (gname + ' 契삔츰냔轟槻', 0);
end;

{ CmdStartGuildMatch - 开始行会比赛
  功能: 初始化行会比赛变量 }
procedure TUserHuman.CmdStartGuildMatch;
var
   i, k: integer;
   ulist, glist: TList;
   hum: TUserHuman;
   flag: Boolean;
   str: string;
begin
   if PEnvir.Fight3Zone then begin
      ulist := TList.Create;
      glist := TList.Create;
      UserEngine.GetAreaUsers (PEnvir, CX, CY, 1000, ulist);  //현맵의 모든 사람
      for i:=0 to ulist.Count-1 do begin
         hum := TUserHuman(ulist[i]);
         if not hum.BoSuperviserMode and not hum.BoSysopMode then begin //운영자모드로 있는 사람은 점수에게 제외
            hum.FightZoneDieCount := 0;  //죽은 카운트 초기화
            if hum.MyGuild <> nil then begin
               flag := FALSE;
               for k:=0 to glist.Count-1 do begin
                  if glist[k] = hum.MyGuild then begin
                     flag := TRUE;
                     break;
                  end;
               end;
               if not flag then
                  glist.Add (hum.MyGuild);
            end;
         end;
      end;
      SysMsg ('契삔濫轢역迦。', 1);
      UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000, '쭯 契삔濫轢역迦。');
      str := '';
      for i:=0 to glist.Count-1 do begin
         TGuild(glist[i]).TeamFightStart;  //문파대련변수초기화, 점수, 맴버
         for k:=0 to ulist.Count-1 do begin
            hum := TUserHuman(ulist[k]);
            if hum.MyGuild = glist[i] then begin
               TGuild(glist[i]).TeamFightAdd (hum.UserName);  //문파대련맴버 자동 추가
            end;
         end;
         str := str + TGuild(glist[i]).GuildName + ' ';
      end;
      UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000, '쭯꽝속돨契삔：' + str);
      ulist.Free;
      glist.Free;
   end else
      SysMsg ('맡츱즈꼇옵痰瞳侶몸뒈暠!', 0);
end;

{ CmdEndGuildMatch - 结束行会比赛
  功能: 结束行会比赛并统计结果 }
procedure TUserHuman.CmdEndGuildMatch;
var
   i, k: integer;
   ulist, glist: TList;
   hum: TUserHuman;
   flag: Boolean;
begin
   if PEnvir.Fight3Zone then begin
      ulist := TList.Create;
      glist := TList.Create;
      UserEngine.GetAreaUsers (PEnvir, CX, CY, 1000, ulist);  //현맵의 모든 사람
      for i:=0 to ulist.Count-1 do begin
         hum := TUserHuman(ulist[i]);
         if not hum.BoSuperviserMode and not hum.BoSysopMode then begin //운영자모드로 있는 사람은 점수에게 제외
            if hum.MyGuild <> nil then begin
               flag := FALSE;
               for k:=0 to glist.Count-1 do begin
                  if glist[k] = hum.MyGuild then begin
                     flag := TRUE;
                     break;
                  end;
               end;
               if not flag then
                  glist.Add (hum.MyGuild);
            end;
         end;
      end;
      for i:=0 to glist.Count-1 do begin
         TGuild(glist[i]).TeamFightEnd;
         UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000, ' -' + TGuild(glist[i]).GuildName + '궐힙써監');
      end;
      ulist.Free;
      glist.Free;
   end;
end;

{ CmdAnnounceGuildMembersMatchPoint - 宣布行会成员比赛分数
  功能: 宣布指定行会成员的比赛分数 }
procedure TUserHuman.CmdAnnounceGuildMembersMatchPoint (gname: string);
var
   i, k, n: integer;
   hum: TUserHuman;
   flag: Boolean;
   guild: TGuild;
begin
   if PEnvir.Fight3Zone then begin
      guild := GuildMan.GetGuild (gname);
      if guild <> nil then begin
         UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000, ' -' + gname + '契삔忌꼈궐힙듐');
         for i:=0 to guild.FightMemberList.Count-1 do begin
            n := integer(guild.FightMemberList.Objects[i]);
            UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000,
                                 ' -' + guild.FightMemberList[i] + ' : ' +
                                 IntToStr(Hiword(n)) +         //Hiword: 얻은점수
                                 ' point / ' +
                                 IntToStr(Loword(n)) + ' dead'); //Loword: 죽은횟수
         end;
         UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000,
                              ' -[' + guild.GuildName + '] ' +
                              IntToStr(guild.MatchPoint));
         UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 10000, '------------------------------------');
      end;
   end else
      SysMsg ('맡츱즈꼇옵痰瞳侶몸뒈暠!', 0);
end;

{ GetLevelInfoString - 获取等级信息字符串
  功能: 获取生物的详细属性信息字符串 }
function  TUserHuman.GetLevelInfoString (cret: TCreature): string;
begin
   Result := cret.UserName +
              ' Map' + cret.MapName +
              ' X' + IntToStr(cret.CX) +
              ' Y' + IntToStr(cret.CY) +
              ' Lv' + IntToStr(cret.Abil.Level) +
              ' Exp' + IntToStr(cret.Abil.Exp) +
              ' HP' + IntToStr(cret.WAbil.HP) + '/' + IntToStr(cret.WAbil.MaxHP) +
              ' MP' + IntToStr(cret.WAbil.MP) + '/' + IntToStr(cret.WAbil.MaxMP) +
              ' DC' + IntToStr(Lobyte(cret.WAbil.DC)) + '-' + IntToStr(Hibyte(cret.WAbil.DC)) +
              ' MC' + IntToStr(Lobyte(cret.WAbil.MC)) + '-' + IntToStr(Hibyte(cret.WAbil.MC)) +
              ' SC' + IntToStr(Lobyte(cret.WAbil.SC)) + '-' + IntToStr(Hibyte(cret.WAbil.SC)) +
              ' AC' + IntToStr(Lobyte(cret.WAbil.AC)) + '-' + IntToStr(Hibyte(cret.WAbil.AC)) +
              ' MAC' + IntToStr(Lobyte(cret.WAbil.MAC)) + '-' + IntToStr(Hibyte(cret.WAbil.MAC)) +
              ' Hit' + IntToStr(cret.AccuracyPoint) +
              ' Spd' + IntToStr(cret.SpeedPoint) +
              // 2003/03/04 추가부분
              ' HitSpeed' + IntToStr(cret.HitSpeed) +
              ' Holy' + IntToStr(cret.AddAbil.UndeadPower);
end;

{ CmdSendUserLevelInfos - 发送用户等级信息
  功能: 发送指定玩家的等级信息 }
procedure TUserHuman.CmdSendUserLevelInfos (whostr: string);
var
   hum: TUserHuman;
begin
   hum := UserEngine.GetUserHuman (whostr);
   if hum <> nil then begin
      SysMsg (GetLevelInfoString (hum), 1);
   end else
      SysMsg (whostr + '轟랬璣冷', 0);
end;

{ CmdSendMonsterLevelInfos - 发送怪物等级信息
  功能: 发送周围怪物的等级信息 }
procedure TUserHuman.CmdSendMonsterLevelInfos;
var
   i: integer;
   list: TList;
   cret: TCreature;
begin
   list := TList.Create;
   PEnvir.GetCreatureInRange (CX, CY, 2, TRUE, list);
   for i:=0 to list.Count-1 do begin
      cret := TCreature(list[i]);
      SysMsg (GetLevelInfoString (cret), 1);
   end;
   list.Free;
end;

{ CmdChangeUserCastleOwner - 改变城堡所有者
  功能: 改变城堡的所有行会 }
procedure TUserHuman.CmdChangeUserCastleOwner (gldname: string; pass: Boolean);
var
   guild: TGuild;
begin
   guild := GuildMan.GetGuild (gldname);
   if guild <> nil then begin
      //로그남김
      AddUserLog ('27'#9 + //사북_ +
                  UserCastle.OwnerGuildName + ''#9 +
                  '0'#9 +
                  '0'#9 +
                  gldname + ''#9 +
                  UserName + ''#9 +
                  '0'#9 +
                  '1'#9 +
                  '0');
      UserCastle.ChangeCastleOwner (guild);
      if pass then
         UserEngine.SendInterMsg (ISM_CHANGECASTLEOWNER, ServerIndex, gldname);
      SysMsg ('�것옹긴뫘槨: ' + gldname + ' 契삔', 1);
   end else
      SysMsg (gldname + '冷꼇돕寧땍돨契삔。', 0);
end;

{ CmdReloadNpc - 重新加载NPC
  功能: 重新加载NPC信息 }
procedure TUserHuman.CmdReloadNpc (cmdstr: string);
var
   i, n: integer;
   list: TList;
begin
   if CompareText (cmdstr, 'all') = 0 then begin

      FrmDB.ReloadNpcs;  //추가된 npc, 삭제된 npc 적용
      FrmDB.ReloadMerchants;
      
      n := 0;
      for i:=0 to UserEngine.NpcList.Count-1 do begin
         TNormNpc(UserEngine.NpcList[i]).ClearNpcInfos;
         TNormNpc(UserEngine.NpcList[i]).LoadNpcInfos;
         Inc (n);
      end;
      for i:=0 to UserEngine.MerchantList.Count-1 do begin
         TMerchant(UserEngine.MerchantList[i]).ClearMerchantInfos;
         TMerchant(UserEngine.MerchantList[i]).LoadMerchantInfos;
         Inc (n);
      end;
      SysMsg ('Reload npc information is successful : ' + IntToStr(n), 1);
   end else begin
      list := TList.Create;
      UserEngine.GetNpcXY (PEnvir, CX, CY, 9, list);  //화면에 보이는 npc
      for i:=0 to list.Count-1 do begin
         TNormNpc(list[i]).ClearNpcInfos;
         TNormNpc(list[i]).LoadNpcInfos;
         SysMsg (TNormNpc(list[i]).UserName + ' 밗잿NPC路劤속潼供냥', 1);
      end;
      list.Clear;
      UserEngine.GetMerchantXY (PEnvir, CX, CY, 9, list);  //화면에 보이는 npc
      for i:=0 to list.Count-1 do begin
         TMerchant(list[i]).ClearMerchantInfos;
         TMerchant(list[i]).LoadMerchantInfos;
         SysMsg (TNormNpc(list[i]).UserName + ' 슥弄NPC路劤속潼供냥', 1);
      end;
      list.Free;
   end;
end;


{ CmdOpenCloseUserCastleMainDoor - 开关城堡大门
  功能: 开启或关闭城堡大门 }
procedure TUserHuman.CmdOpenCloseUserCastleMainDoor (cmdstr: string);
begin
   if IsGuildMaster and (MyGuild = UserCastle.OwnerGuild) then begin
      if CompareText (cmdstr, '댔역') = 0 then begin

      end;
      if CompareText (cmdstr, '밑균') = 0 then begin
         
      end;
   end else
      SysMsg ('늪츱즈怜唐�것옹돨냘寮꼽옵鹿賈痰。', 0);
end;


{ CmdAddShutUpList - 添加禁言列表
  功能: 将玩家添加到禁言列表 }
procedure TUserHuman.CmdAddShutUpList (whostr, minstr: string; pass: Boolean);
var
   idx, amin: integer;
begin
   amin := Str_ToInt(minstr, 5);
   if whostr <> '' then begin
      idx := ShutUpList.FFind (whostr);
      if idx >= 0 then begin
         ShutUpList.Objects[idx] := TObject(integer(ShutUpList.Objects[idx]) + amin * 60 * 1000);
      end else begin
         ShutUpList.QAddObject (whostr, TObject(GetCurrentTime + (amin * 60 * 1000)));
      end;
      if pass then  //다른 서버에 전달할 것인지
         UserEngine.SendInterMsg (ISM_CHATPROHIBITION, ServerIndex, whostr + '/' + IntToStr(amin));
      SysMsg (whostr + '쐐岺좔莖 + ' + IntToStr(amin) + '롸爐', 1);
   end else
      SysMsg (whostr + '轟랬꿴冷', 0);
end;

{ CmdDelShutUpList - 删除禁言列表
  功能: 从禁言列表中删除玩家 }
procedure TUserHuman.CmdDelShutUpList (whostr: string; pass: Boolean);
var
   hum: TUserHuman;
   idx: integer;
begin
   idx := ShutUpList.FFind (whostr);
   if idx >= 0 then begin
      ShutUpList.Delete (idx);
      hum := UserEngine.GetUserHuman (whostr);
      if hum <> nil then begin
         hum.SysMsg ('닒쐐岺좔莖죗깊�뇜', 1);
      end;
      if pass then  //다른 서버에 전달 여부
         UserEngine.SendInterMsg (ISM_CHATPROHIBITIONCANCEL, ServerIndex, whostr);
      SysMsg (whostr + ' ' + '', 1);
   end else
      SysMsg (whostr + ' 轟랬꿴冷', 0);
end;

{ CmdSendShutUpList - 发送禁言列表
  功能: 发送当前禁言列表 }
procedure TUserHuman.CmdSendShutUpList;
var
   i: integer;
begin
   for i:=0 to ShutUpList.Count-1 do begin
      SysMsg (ShutUpList[i] + ' ' + IntToStr((integer(ShutUpList.Objects[i]) - GetCurrentTime) div 60000) + 'ㅐ', 1);
   end;
end;


{----------------------------------------------------------}
{ 发送物品相关方法 }

{ SendAddItem - 发送添加物品
  功能: 发送添加物品消息给客户端 }
procedure TUserHuman.SendAddItem (ui: TUserItem);
var
   citem: TClientItem;
   ps: PTStdItem;
   std: TStdItem;
begin
   ps := UserEngine.GetStdItem (ui.Index);
   if ps <> nil then begin
      std := ps^;
      ItemMan.GetUpgradeStdItem (ui, std);
      Move (std, citem.S, sizeof(TStdItem));
      citem.MakeIndex := ui.MakeIndex;
      citem.Dura := ui.Dura;
      citem.DuraMax := ui.DuraMax;

      if std.StdMode = 50 then begin  //상품권
         citem.S.Name := citem.S.Name + ' #' + IntToStr(ui.Dura);
      end;
      //미지의속성 프리된 것들
      // 2003/03/15 아이템 인벤토리 확장
      if std.StdMode in [15,19,20,21,22,23,24,26,52,53,54] then begin
         if ui.Desc[8] = 0 then //속성이 프리됨
            citem.S.Shape := 0
         else citem.S.Shape := RING_OF_UNKNOWN;  
      end;

      Def := MakeDefaultMsg (SM_ADDITEM, integer(self), 0, 0, 1{수량});
      SendSocket (@Def, EncodeBuffer (@citem, sizeof(TClientItem)));
   end;
end;

{ SendUpdateItem - 发送更新物品
  功能: 发送更新物品消息给客户端 }
procedure TUserHuman.SendUpdateItem (ui: TUserItem);
var
   citem: TClientItem;
   ps: PTStdItem;
   std: TStdItem;
begin
   ps := UserEngine.GetStdItem (ui.Index);
   if ps <> nil then begin
      std := ps^;
      ItemMan.GetUpgradeStdItem (ui, std);
      Move (std, citem.S, sizeof(TStdItem));
      citem.MakeIndex := ui.MakeIndex;
      citem.Dura := ui.Dura;
      citem.DuraMax := ui.DuraMax;
      if std.StdMode = 50 then begin  //상품권
         citem.S.Name := citem.S.Name + ' #' + IntToStr(ui.Dura);
      end;
      Def := MakeDefaultMsg (SM_UPDATEITEM, integer(self), 0, 0, 1{수량});
      SendSocket (@Def, EncodeBuffer (@citem, sizeof(TClientItem)));
   end;
end;

{ SendDelItem - 发送删除物品
  功能: 发送删除物品消息给客户端 }
procedure TUserHuman.SendDelItem (ui: TUserItem);
var
   citem: TClientItem;
   ps: PTStdItem;
   std: TStdItem;
begin
   ps := UserEngine.GetStdItem (ui.Index);
   if ps <> nil then begin
      std := ps^;
      ItemMan.GetUpgradeStdItem (ui, std);
      Move (std, citem.S, sizeof(TStdItem));
      citem.MakeIndex := ui.MakeIndex;
      citem.Dura := ui.Dura;
      citem.DuraMax := ui.DuraMax;
      citem.MakeIndex := ui.MakeIndex;
      if std.StdMode = 50 then begin  //상품권
         citem.S.Name := citem.S.Name + ' #' + IntToStr(ui.Dura);
      end;
      Def := MakeDefaultMsg (SM_DELITEM, integer(self), 0, 0, 1{수량});
      SendSocket (@Def, EncodeBuffer (@citem, sizeof(TClientItem)));
   end;
end;

{ SendDelItems - 发送删除多个物品
  功能: 发送删除多个物品消息给客户端 }
procedure TUserHuman.SendDelItems (ilist: TStringList);
var
   i: integer;
   data: string;
begin
   data := '';
   for i:=0 to ilist.Count-1 do begin
      data := data + ilist[i] + '/' + IntToStr(Integer(ilist.objects[i])) + '/';
   end;
   Def := MakeDefaultMsg (SM_DELITEMS, 0, 0, 0, ilist.Count);
   SendSocket (@Def, EncodeString(data));
end;

{ SendBagItems - 发送背包物品
  功能: 发送背包中所有物品给客户端 }
procedure TUserHuman.SendBagItems;
var
   i: integer;
   pu: PTUserItem;
   citem: TClientItem;
   ps: PTStdItem;
   std: TStdItem;
   data: string;
begin
   data := '';
   for i:=0 to ItemList.Count-1 do begin
      pu := PTUserItem (ItemList[i]);
      ps := UserEngine.GetStdItem (pu.Index);
      if ps <> nil then begin
         std := ps^;
         ItemMan.GetUpgradeStdItem (pu^, std);
         Move (std, citem.S, sizeof(TStdItem));
         citem.Dura := pu.Dura;
         citem.DuraMax := pu.DuraMax;
         citem.MakeIndex := pu.MakeIndex;
         if std.StdMode = 50 then begin  //상품권
            citem.S.Name := citem.S.Name + ' #' + IntToStr(pu.Dura);
         end;
         data := data + EncodeBuffer (@citem, sizeof(TClientItem)) + '/';
      end;
   end;
   if data <> '' then begin
      Def := MakeDefaultMsg (SM_BAGITEMS, integer(self), 0, 0, ItemList.Count{수량});
      SendSocket (@Def, data);
   end;
end;

{ SendUseItems - 发送装备物品
  功能: 发送装备栏物品给客户端 }
procedure TUserHuman.SendUseItems;
var
   i: integer;
   citem: TClientItem;
   ps: PTStdItem;
   std: TStdItem;
   data: string;
begin
   data := '';
   // 2003/03/15 아이템 인벤토리 확장
   for i:=0 to 12 do begin    // 8->12
      if UseItems[i].Index > 0 then begin
         ps := UserEngine.GetStdItem (UseItems[i].Index);
         if ps <> nil then begin
            std := ps^;
            ItemMan.GetUpgradeStdItem (UseItems[i], std);
            Move (std, citem.S, sizeof(TStdItem));
            citem.Dura := UseItems[i].Dura;
            citem.DuraMax := UseItems[i].DuraMax;
            citem.MakeIndex := UseItems[i].MakeIndex;
            data := data + IntToStr(i) + '/' + EncodeBuffer (@citem, sizeof(TClientItem)) + '/';
         end;
      end;
   end;
   if data <> '' then begin
      Def := MakeDefaultMsg (SM_SENDUSEITEMS, 0, 0, 0, 0);
      SendSocket (@Def, data);
   end;
end;

{----------------------------------------------------------}
{ 魔法相关方法 }

{ SendAddMagic - 发送添加魔法
  功能: 发送添加魔法消息给客户端 }
procedure TUserHuman.SendAddMagic (pum: PTUserMagic);
var
   cmag: TClientMagic;
begin
   cmag.Key := pum.Key;
   cmag.Level := pum.Level;
   cmag.CurTrain := pum.CurTrain;
   cmag.Def := pum.pDef^;
   Def := MakeDefaultMsg (SM_ADDMAGIC, 0, 0, 0, 1);
   SendSocket (@Def, EncodeBuffer (@cmag, sizeof(TClientMagic)));
end;

{ SendDelMagic - 发送删除魔法
  功能: 发送删除魔法消息给客户端 }
procedure TUserHuman.SendDelMagic (pum: PTUserMagic);
begin
   Def := MakeDefaultMsg (SM_DELMAGIC, pum.MagicId, 0, 0, 1);
   SendSocket (@Def, '');
end;

{ SendMyMagics - 发送我的魔法
  功能: 发送所有魔法列表给客户端 }
procedure TUserHuman.SendMyMagics;
var
   i, mdelay: integer;
   data: string;
   pum: PTUserMagic;
   cmag: TClientMagic;
begin
   data := '';
   mdelay := 0;
   for i:=0 to MagicList.Count-1 do begin
      pum := PTUserMagic (MagicList[i]);
      cmag.Key := pum.Key;
      cmag.Level := pum.Level;
      cmag.CurTrain := pum.CurTrain;
      cmag.Def := pum.pDef^;
      mdelay := mdelay + pum.pDef.DelayTime;

      data := data + EncodeBuffer (@cmag, sizeof(TClientMagic)) + '/';
   end;
   Def := MakeDefaultMsg (SM_SENDMYMAGIC, (mdelay xor $773F1A34) xor $4BBC2255, 0, 0, MagicList.Count);
   SendSocket (@Def, data);
end;


{----------------------------------------------------------}
{ 私聊相关方法 }

{ Whisper - 私聊
  功能: 发送私聊消息给指定玩家 }
procedure TUserHuman.Whisper (whostr, saystr: string);
var
   hum: TUserHuman;
   svidx: integer;
begin
   hum := TUserHuman (UserEngine.GetUserHuman (whostr));
   if hum <> nil then begin
      if not hum.ReadyRun then begin
         SysMsg (whostr + '轟랬꿴冷', 0);
         exit;
      end;
      if not hum.BoHearWhisper or hum.IsBlockWhisper (UserName) then begin
         SysMsg (whostr + '앳없쵱刀', 0);
         exit;
      end;
      hum.SendMsg (self, RM_WHISPER, 0, 0, 0, 0, UserName + '=> ' + saystr);
   end else begin
      if UserEngine.FindOtherServerUser (whostr, svidx) then begin
         UserEngine.SendInterMsg (ISM_WHISPER, svidx, whostr + '/' + UserName + '=> ' + saystr);
      end else
         SysMsg (whostr + '轟랬꿴冷', 0);
   end;
end;

{ WhisperRe - 私聊回复
  功能: 接收私聊回复消息 }
procedure TUserHuman.WhisperRe (saystr: string);
var
   sendwho: string;
begin
   GetValidStr3 (saystr, sendwho, [' ', '=', '>']);
   if BoHearWhisper and (not IsBlockWhisper (sendwho)) then
      SendMsg (self, RM_WHISPER, 0, 0, 0, 0, saystr);
end;

{ BlockWhisper - 屏蔽私聊
  功能: 屏蔽指定玩家的私聊 }
procedure TUserHuman.BlockWhisper (whostr: string);
var
   i: integer;
begin
   for i:=0 to WhisperBlockList.Count-1 do
      if CompareText(whostr, WhisperBlockList[i]) = 0 then begin
         WhisperBlockList.Delete (i);
         SysMsg ('[豚冀宅:' + whostr + ' 降좔]', 1);
         exit;
      end;
   WhisperBlockList.Add (whostr);
   SysMsg ('[쐐岺宅:' + whostr + ' 降좔]', 0);
end;

{ IsBlockWhisper - 是否屏蔽私聊
  功能: 检查是否屏蔽了指定玩家的私聊 }
function  TUserHuman.IsBlockWhisper (whostr: string): Boolean;
var
   i: integer;
begin
   Result := FALSE;
   for i:=0 to WhisperBlockList.Count-1 do
      if CompareText(whostr, WhisperBlockList[i]) = 0 then begin
         Result := TRUE;
         break;
      end;
end;

{ GuildSecession - 退出行会
  功能: 退出当前行会 }
procedure TUserHuman.GuildSecession;
begin
   if (MyGuild <> nil) and (GuildRank > 1) then begin  //문주는 안됨
      if TGuild(MyGuild).IsMember (UserName) then
         if TGuild(MyGuild).DelMember (UserName) then begin
            UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
            MyGuild := nil;
            GuildRankChanged (0, '');
            SysMsg ('藁놔죄契삔', 1);
         end;
   end else
      SysMsg ('혤句', 0);
end;

{ CmdSendTestQuestDiary - 发送测试任务日记
  功能: 发送任务日记信息 }
procedure TUserHuman.CmdSendTestQuestDiary (unitnum: integer);
var
   i, k: integer;
   str: string;
   list: TList;
   pqdd: PTQDDinfo;
begin
   if unitnum = 0 then begin
      for i:=0 to QuestDiaryList.Count-1 do begin
         list := TList(QuestDiaryList[i]);
         if list <> nil then begin
            if list.Count > 0 then begin
               if GetQuestOpenIndexMark (i+1) = 1 then str := ' (역迦)'
               else str := ' (硫구)';
               if GetQuestFinIndexMark (i+1) = 1 then str := str + ' (써監)'
               else str := str + ' (쏵契)';
               SysMsg ('[' + IntToStr(PTQDDinfo (list[0]).index) + '] ' + PTQDDinfo (list[0]).title + str, 1);
            end;
         end;
      end;
   end else begin
      unitnum := unitnum - 1;  //유닛을 나타낼 때는 1이 0임
      if unitnum < QuestDiaryList.Count then begin
         list := TList(QuestDiaryList[unitnum]);
         if list <> nil then begin
            for i:=0 to list.Count-1 do begin
               pqdd := PTQDDinfo (list[i]);
               if GetQuestMark (pqdd.index) = 1 then str := ' (써監)'
               else str := ' (灌供)';
               SysMsg ('[' + IntToStr(pqdd.index) + '] ' + pqdd.title + str, 2);
               for k:=0 to pqdd.SList.Count-1 do
                  SysMsg (pqdd.SList[k], 1);
            end;
         end;
      end;
   end;
end;





{ Say - 说话
  功能: 处理玩家说话和命令
  参数:
    saystr - 说话内容或命令 }
procedure TUserHuman.Say (saystr: string);
var
   str, cmd, param1, param2, param3, param4, param5, param6, param7: string;
   hum: TUserhuman;
   pstd: PTStdItem;
   i, idx, n: integer;
   boshutup: Boolean;
   lv_ring, lv_bracelet, lv_necklace, lv_helmet : Boolean;
begin
   if saystr = '' then exit;

   if BoReadyAdminPassword then begin
      BoReadyAdminPassword := FALSE;
      if Str_ToInt (saystr, 0) = GET_A_PASSWD then begin
         UserDegree := UD_ADMIN;
         SysMsg ('밗잿逃되쩍냥묘', 0);
      end;
      exit;
   end;

   if BoReadySuperAdminPassword then begin
      BoReadySuperAdminPassword := FALSE;
      if KOREANVERSION then begin
         if BoTestServer then begin
            if saystr = 'wemade09' then begin  //테섭
               UserDegree := UD_SUPERADMIN;
            end else
               SysMsg ('되쩍呵겨', 0);
         end else begin
            if saystr = 'Le&end0f#ir' then begin
               UserDegree := UD_SUPERADMIN;
            end else
               SysMsg ('되쩍呵겨', 0);
         end;
      end;
      if CHINAVERSION then begin
         if saystr = 'Le&end0f#ir' then begin   //중국
            UserDegree := UD_SUPERADMIN;
         end else
            SysMsg ('되쩍呵겨', 0);
      end;
      if TAIWANVERSION then begin
         if saystr = 'TGL&S0ftW0rld' then begin   //대만
            UserDegree := UD_SUPERADMIN;
         end else
            SysMsg ('되쩍呵겨', 0);
      end;
      if ENGLISHVERSION then begin
         if saystr = 'Le&end0f#ir' then begin   //이탈리아
            UserDegree := UD_SUPERADMIN;
         end else
            SysMsg ('되쩍呵겨', 0);
      end;

      exit;
   end;

   if saystr[1] = '@' then begin
      str := Copy (saystr, 2, Length(saystr)-1);
      str := GetValidStr3 (str, cmd, [' ', ',', ':']);
      str := GetValidStr3 (str, param1, [' ', ',', ':']);
      if str <> '' then str := GetValidStr3 (str, param2, [' ', ',', ':']);
      if str <> '' then str := GetValidStr3 (str, param3, [' ', ',', ':']);
      if str <> '' then str := GetValidStr3 (str, param4, [' ', ',', ':']);
      if str <> '' then str := GetValidStr3 (str, param5, [' ', ',', ':']);
      if str <> '' then str := GetValidStr3 (str, param6, [' ', ',', ':']);
      if str <> '' then str := GetValidStr3 (str, param7, [' ', ',', ':']);

      {
      if BoTestServer and KoreanVersion then begin
         if CompareText (cmd, 'admins') = 0 then begin
            for i:=0 to UserEngine.AdminList.Count-1 do
               SysMsg (UserEngine.AdminList[i], 1);
            exit;
         end;
      end;  }

      if (CompareText(cmd, '앳없降좔') = 0) or (CompareText(cmd, '앳없降좔') = 0) then begin
         BoHearWhisper := not BoHearWhisper;
         if BoHearWhisper then SysMsg ('[豚冀쌈澗降좔斤口]', 1)
         else SysMsg ('[앳없쌈澗降좔斤口]', 1);
         exit;
      end;
      if (CompareText(cmd, '豚冀降좔') = 0) or (CompareText(cmd, '豚冀降좔') = 0) then begin
         BoHearWhisper := TRUE;
         SysMsg ('[豚冀降좔]', 1);
         exit;
      end;
      if (CompareText(cmd, '앳없') = 0) then begin //앳없캐훙降좔
         if param1 <> '' then BlockWhisper (param1);
         if param2 <> '' then BlockWhisper (param2);
         if param3 <> '' then BlockWhisper (param3);
         exit;
      end;
      if (CompareText(cmd, '앳없별뺐') = 0) or (CompareText(cmd, '豚冀별뺐') = 0) then begin
         BoHearCry := not BoHearCry;
         if BoHearCry then SysMsg ('[豚冀쌈澗(뼝奈�俚竟)별뺐]', 1)
         else SysMsg ('[앳없쌈澗(뼝奈�俚竟)별뺐]', 1);
         exit;
      end;;
      if CompareText(cmd, '앳없슥弄') = 0 then begin
         BoExchangeAvailable := not BoExchangeAvailable;
         if BoExchangeAvailable then SysMsg ('[옵鹿슥弄]', 1)
         else SysMsg ('[앳없슥弄]', 1);
         exit;
      end;
      if CompareText(cmd, '속흙쳔탰') = 0 then begin
         AllowEnterGuild := not AllowEnterGuild;
         if AllowEnterGuild then SysMsg ('[옵鹿속흙契삔]', 1)
         else SysMsg ('[앳없속흙契삔]', 1);
         exit;
      end;
      if CompareText(cmd, '豚冀젬촉') = 0 then begin
         if IsGuildMaster then begin
            TGuild(MyGuild).AllowAllyGuild := not TGuild(MyGuild).AllowAllyGuild;
            if TGuild(MyGuild).AllowAllyGuild then SysMsg ('[豚冀젬촉]', 1)
            else SysMsg ('[앳없젬촉]', 1);
         end;
         exit;
      end;
      if CompareText (cmd, '젬촉') = 0 then begin
         if IsGuildMaster then begin
            ServerGetGuildMakeAlly;
         end;
         exit;
      end;
      if CompareText (cmd, '혤句젬촉') = 0 then begin
         if IsGuildMaster then begin
            ServerGetGuildBreakAlly (param1);
         end;
         exit;
      end;
      if CompareText(cmd, '藁놔쳔탰') = 0 then begin
         GuildSecession;
         exit;
      end;
      if (CompareText(cmd, '豚冀契삔좔莖') = 0) or (CompareText(cmd, '앳없契삔좔莖') = 0) then begin
         BoHearGuildMsg := not BoHearGuildMsg;
         if BoHearGuildMsg then SysMsg ('豚冀쌈澗契삔별뺐斤口', 1)
         else SysMsg ('앳없쌈澗契삔별뺐斤口', 1);
         exit;
      end;
      if (UpperCase (cmd) = 'H') or (UpperCase (cmd) = 'HELP') then begin
         for i:=0 to LineHelpList.Count-1 do
            SysMsg (LineHelpList[i], 1);
         exit;   
      end;


      //퀘스트 일지 테스트
      if CompareText(cmd, 'ㅹ퍇') = 0 then begin
         CmdSendTestQuestDiary (Str_ToInt(param1, 0));
         exit;
      end;

      if CompareText(cmd, 'AttackMode') = 0 then begin  //공격방식을 바꾼다.
         if HumAttackMode < HAM_MAXCOUNT-1 then Inc (HumAttackMode)
         else HumAttackMode := 0;
         case HumAttackMode of
            HAM_ALL:    SysMsg ('[홍竟묑샌]', 1);
            HAM_PEACE:  SysMsg ('[뵨틱묑샌]', 1);
            HAM_GROUP:  SysMsg ('[긍莉묑샌]', 1);
            HAM_GUILD:  SysMsg ('[契삔묑샌]', 1);
            HAM_PKATTACK: SysMsg ('[�띳뚤묑]', 1);
         end;
         exit;
      end;
      if CompareText(cmd, 'Rest') = 0 then begin  //공격 or 휴식
         if SlaveList.Count > 0 then begin
            BoSlaveRelax := not BoSlaveRelax;
            if BoSlaveRelax then SysMsg ('苟橄契땡：金口', 1)
            else SysMsg ('苟橄契땡：묑샌', 1);
         end;
         exit;
      end;

      if Str_ToInt (cmd, 0) = GET_A_CMD then begin  //
         SendMsg (self, RM_NEXTTIME_PASSWORD, 0, 0, 0, 0, '');
         SysMsg ('헝渴흙쵱쯤: ', 1);
         BoReadyAdminPassword := TRUE;
         exit;
      end;
      if UserDegree >= UD_SYSOP then begin
         if CompareText (cmd, 'gsa') = 0 then begin
            SendMsg (self, RM_NEXTTIME_PASSWORD, 0, 0, 0, 0, '');
            SysMsg ('헝渴흙쵱쯤: ', 1);
            BoReadySuperAdminPassword := TRUE;
            exit;
         end;
         if Str_ToInt (cmd, 0) = GET_SA_CMD then begin
            SendMsg (self, RM_NEXTTIME_PASSWORD, 0, 0, 0, 0, '');
            SysMsg ('헝渴흙쵱쯤: ', 1);
            BoReadySuperAdminPassword := TRUE;
            exit;
         end;
      end;

      if (MyGuild = UserCastle.OwnerGuild) and (MyGuild <> nil) then begin
         if CompareText(cmd, '쭲�것옹냘쳔') = 0 then begin
            CmdOpenCloseUserCastleMainDoor (param1);  //닫힘,열림
            exit;
         end;

      end;


      //순간이동 반지를 끼고 있으면...
      if BoAbilSpaceMove then begin
         if not BoTaiwanEventUser then begin
            if not PEnvir.NoPositionMove then begin
               if CompareText(cmd, 'Move') = 0 then begin
                  if GetTickCount - LatestSpaceMoveTime > 10 * 1000 then begin
                     LatestSpaceMoveTime := GetTickCount;
                     SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
                     UserSpaceMove ('', param1, param2);
                  end else
                     SysMsg (IntToStr(10 - (GetTickCount - LatestSpaceMoveTime) div 1000) + '취빈꼽옵鹿賈痰', 0);
                  exit;
               end;
            end else begin
               //순간이동반지 사용 불가능 지역
               SysMsg ('꼇콘瞳侶쟁賈痰', 0);
               exit;
            end;
         end else
            SysMsg ('轟랬賈痰', 0);
      end;
      //決꿎淃졍
      if BoAbilSearch or (UserDegree >= UD_SYSOP) then begin
         if CompareText(cmd, 'Searching') = 0 then begin
            if (GetTickCount - LatestSearchWhoTime > 10 * 1000) or (UserDegree >= UD_SYSOP) then begin
               LatestSearchWhoTime := GetTickCount;
               hum := UserEngine.GetUserHuman (param1);
               if hum <> nil then begin
                  if hum.PEnvir = PEnvir then begin
                     SysMsg (param1 + ' 瞳谿寧뒈暠돨 ' + IntToStr(hum.CX) + ' ' + IntToStr(hum.CY) + '.', 1);
                  end else
                     SysMsg (param1 + '늪훙瞳페儉돨貫零。', 1);
               end else
                  SysMsg (param1 + '늪훙轟랬꿴璂。', 1);
            end else
               SysMsg (IntToStr(10 - (GetTickCount - LatestSearchWhoTime) div 1000) + ' 취빈꼽콘賈痰늪츱즈', 0);
            exit;
         end;
      end;
      //션壘敬
      if (CompareText(cmd, '앳없莖뒈북寧') = 0) or (CompareText(cmd, '豚冀莖뒈북寧') = 0) then begin
         BoEnableRecall := not BoEnableRecall;
         if BoEnableRecall then SysMsg ('[豚冀莖뒈북寧]', 1)
         else SysMsg ('[앳없莖뒈북寧]', 1);
      end;
      // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
      if (BoCGHIEnable) or (BoOldVersionUser_Italy) or (UserDegree >= UD_SYSOP) then begin
         if (CompareText(cmd, '莖뒈북寧') = 0) then begin
            if not PEnvir.NoRecall then begin
               n := (GetTickCount - CGHIstart) div 1000;
               CGHIstart := CGHIstart + longword(n * 1000);
               if CGHIUseTime > n then CGHIUseTime := CGHIUseTime - n
               else CGHIUseTime := 0;
               if CGHIUseTime = 0 then begin
                  if GroupOwner = self then begin //자신이 그룹짱
                     for i:=1 to GroupMembers.Count-1 do begin  //자신 빼고
                        if TUserHuman(GroupOwner.GroupMembers.Objects[i]).BoEnableRecall then
                           CmdRecallMan (GroupMembers[i])
                        else
                           SysMsg (GroupMembers[i] + '앳없莖뒈북寧', 0);
                     end;
                     CGHIstart := GetTickCount;
                     CGHIUseTime := 3 * 60;
                  end;
               end else begin
                  SysMsg ('莖뒈북寧' + IntToStr(CGHIUseTime) + '취빈옵鹿賈痰', 0);
               end;
            end else begin
               SysMsg ('瞳侶쟁퀭轟랬賈痰。', 0);
            end;
         end;
         // 2003/03/15 사랑 투구, 사랑 목걸이, 사랑 반지, 사랑 팔찌 추가
         // 소환상대자에게 해당 아이템이 있는지 확인
         if (CompareText(cmd, '톀괌또쯍') = 0) then begin
            if not PEnvir.NoRecall then begin
               if param1 <> '' then begin
                  hum := UserEngine.GetUserHuman (param1);
                  if hum <> nil then begin
                     // 퀘스트 수행 여부 확인 ... 자기 자신의 퀘스트, 상대의 퀘스트, 상대의 아이템 확인, 상대의 소환허용여부
                     if (GetQuestMark (120) = 1) and (hum.GetQuestMark (120) = 1) and (hum.BoEnableRecall) then begin
                        lv_ring     := FALSE;
                        lv_bracelet := FALSE;
                        lv_necklace := FALSE;
                        lv_helmet   := FALSE;
                        for i:=0 to 8 do begin
                           if (hum.UseItems[i].Index > 0) and (hum.UseItems[i].Dura > 0) then begin
                              pstd := UserEngine.GetStdItem (hum.UseItems[i].Index);
                              if pstd <> nil then begin
                                 if ( i = U_NECKLACE) and (pstd.Shape = NECKLACE_OF_LOVE) then lv_necklace := TRUE;
                                 if ((i = U_RINGR) or (i = U_RINGL)) and (pstd.Shape = RING_OF_LOVE) then lv_ring := TRUE;
                                 if ((i = U_ARMRINGL) or (i = U_ARMRINGR)) and (pstd.Shape = BRACELET_OF_LOVE ) then lv_bracelet := TRUE;
                                 if ( i = U_HELMET) and (pstd.Shape = HELMET_OF_LOVE) then lv_helmet := TRUE;
                              end;
                           end;
                        end;
                        if lv_ring then   // or lv_bracelet or lv_necklace or lv_helmet then
                           CmdRecallMan (param1)
                        else
                           SysMsg ('梁뻥呵겨', 0);
                     end else
                        SysMsg ('梁뻥呵겨', 0);
                  end else
                     SysMsg (param1 + ' 轟랬璣冷', 0);
               end else
                  SysMsg (param1 + ' 轟랬璣冷', 0);
            end;
         end;
      end;

      if UserDegree >= UD_OBSERVER then begin
         if Length(saystr) > 2 then begin
            if (saystr[2] = '!') then begin  //"@!" 운영자 전음
               str := Copy (saystr, 3, length(saystr)-2);
               UserEngine.SysMsgAll ('(무멩)' + str);
               UserEngine.SendInterMsg (ISM_SYSOPMSG, ServerIndex, '(*)' + str);
               exit;
            end;
            if (saystr[2] = '$') then begin  //"@$" 운영자 전음, 현서버에서만 전달
               str := Copy (saystr, 3, length(saystr)-2);
               UserEngine.SysMsgAll ('(!)' + str);
               exit;
            end;
            if (saystr[2] = '#') then begin  //"@#" 운영자 전음, 현맵에만 전달
               str := Copy (saystr, 3, length(saystr)-2);
               UserEngine.CryCry (RM_SYSMESSAGE, PEnvir, CX, CY, 10000, '(#)' + str);
               exit;
            end;
         end;
      end;

      if UserDegree >= UD_SYSOP then begin
         if CompareText(cmd, 'Move') = 0 then begin
            if GrobalEnvir.GetEnvir (param1) <> nil then begin
               SendRefMsg (RM_SPACEMOVE_HIDE, 0, 0, 0, 0, '');
               RandomSpaceMove (param1, 0); //무작위 공간이동
            end;
            exit;
         end;
         if (CompareText(cmd, 'PositionMove') = 0) or (CompareText(cmd, 'PMove') = 0) then begin
            CmdFreeSpaceMove (param1, param2, param3);
            exit;
         end;
         if CompareText(cmd, 'Info') = 0 then begin
            CmdSendUserLevelInfos (param1);
            exit;
         end;
         if CompareText(cmd, 'MobLevel') = 0 then begin
            CmdSendMonsterLevelInfos;
            exit;
         end;
         if CompareText(cmd, 'MobCount') = 0 then begin
            SysMsg ('뒈暠: ' + param1 + '뎠품밍膠=' +
                     IntToStr(
                        UserEngine.GetMapMons (GrobalEnvir.GetEnvir(param1), nil)
                     ), 1);
            exit;
         end;
         if CompareText(cmd, 'Human') = 0 then begin
            SysMsg ('뒈暠: ' +param1 + '뎠품훙鑒=' + IntToStr(UserEngine.GetHumCount (param1)), 1);
         end;
         if CompareText(cmd, 'Map') = 0 then begin
            SysMsg ('뒈暠: ' + MapName, 0);
            exit;
         end;
         if (CompareText(cmd,'Kick') = 0) or (CompareText(cmd,'Kick') = 0) then begin
            CmdKickUser (param1);
            exit;
         end;
         if (CompareText(cmd,'Ting') = 0) then begin
            CmdTingUser (param1);
            exit;
         end;
         if (CompareText(cmd,'SuperTing') = 0) then begin
            CmdTingRangeUser (param1, param2);
            exit;
         end;
         if CompareText(cmd, 'Shutup') = 0 then begin
            CmdAddShutUpList (param1, param2, TRUE);
            exit;
         end;
         if CompareText(cmd, 'ReleaseShutup') = 0 then begin
            CmdDelShutUpList (param1, TRUE);
            exit;
         end;
         if CompareText(cmd, 'ShutupList') = 0 then begin
            CmdSendShutUpList;
            exit;
         end;
         if CompareText(cmd, 'GameMaster') = 0 then begin
            BoSysopMode := not BoSysopMode;
            if BoSysopMode then SysMsg ('쏵흙밗잿逃친駕', 1)
            else SysMsg ('藁놔밗잿逃친駕', 1);
            exit;
         end;
         if (CompareText(cmd, 'Observer') = 0) or (CompareText(cmd, 'Ob') = 0) then begin
            BoSuperviserMode := not BoSuperviserMode;
            if BoSuperviserMode then SysMsg ('쏵흙茶�친駕', 1)
            else SysMsg ('藁놔茶�친駕', 1);
            exit;
         end;
         if CompareText(cmd, 'Superman') = 0 then begin
            NeverDie := not NeverDie;
            if NeverDie then SysMsg ('쏵흙轟둔친駕', 1)
            else SysMsg ('藁놔轟둔친駕', 1);
            exit;
         end;
         if CompareText(cmd, 'Level') = 0 then begin
            Abil.Level := _MIN(40, Str_ToInt(param1, 1));
            HasLevelUp (1);
            exit;
         end;
         if CompareText(cmd, 'SabukWallGold') = 0 then begin
            SysMsg ('�것옹샘쏜:' + IntToStr(UserCastle.TotalGold) + ',  쏟莖澗흙:' + IntToStr(UserCastle.TodayIncome), 1);
            exit;
         end;
         if CompareText(cmd, 'Recall') = 0 then begin
            CmdRecallMan (param1);
            exit;
         end;
         if CompareText(cmd, 'flag') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               idx := Str_ToInt(param2, 0);
               if hum.GetQuestMark (idx) = 1 then
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = ON', 1)
               else
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = OFF', 1);
            end else
               SysMsg ('@flag user_name number_of_flag', 0);
         end;
         if CompareText(cmd, 'showopen') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               idx := Str_ToInt(param2, 0);
               if hum.GetQuestOpenIndexMark (idx) = 1 then
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = ON', 1)
               else
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = OFF', 1);
            end else
               SysMsg ('@showopen user_name number_of_unit', 0);
         end;
         if CompareText(cmd, 'showunit') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               idx := Str_ToInt(param2, 0);
               if hum.GetQuestFinIndexMark (idx) = 1 then
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = ON', 1)
               else
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = OFF', 1);
            end else
               SysMsg ('@showunit user_name number_of_unit', 0);
         end;
      end;

      if UserDegree >= UD_ADMIN then begin
         if CompareText(cmd, 'Attack') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               SelectTarget (hum);
            end;
            exit;
         end;
         if CompareText(cmd, 'Mob') = 0 then begin
            CmdCallMakeMonster (param1, param2);
            exit;
         end;
         if CompareText(cmd, 'RecallMob') = 0 then begin
            CmdCallMakeSlaveMonster (param1, param2, Str_ToInt(param3,0));
            exit;
         end;
         if CompareText(cmd, 'LuckyPoint') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then
               SysMsg (param1 + ': BodyLuck= ' + IntToStr(hum.BodyLuckLevel) + '/' + FloatToStr(hum.BodyLuck) + ' Luck = ' + IntToStr(hum.Luck), 1);
            exit;
         end;
         if CompareText(cmd, '꽈튿꿴璂') = 0 then begin
            SysMsg ('櫓쉽꽈튿' + IntToStr(LottoSuccess) + ', ' +
                    '청櫓꽈튿' + IntToStr(LottoFail) + ', ' +
                    '寧된쉽' + IntToStr(Lotto1) + ', ' +
                    '랗된쉽' + IntToStr(Lotto2) + ', ' +
                    '힛된쉽' + IntToStr(Lotto3) + ', ' +
                    '愷된쉽' + IntToStr(Lotto4) + ', ' +
                    '巧된쉽' + IntToStr(Lotto5) + ', ' +
                    '짇된쉽' + IntToStr(Lotto6)
                    , 1);
            exit;
         end;
         if CompareText (cmd, 'ReloadGuild') = 0 then begin
            CmdReloadGuild (param1);
            exit;
         end;
         if CompareText (cmd, 'ReloadLineNotice') = 0 then begin
            if LoadLineNotice (LINENOTICEFILE) then begin
               SysMsg (LINENOTICEFILE + '무멩匡숭路劤속潼...', 1);
            end;
            exit;
         end;
         if CompareText(cmd, 'ReadAbuseInformation') = 0 then begin
            LoadAbusiveList ('!Abuse.txt');
            SysMsg ('路뗍읕痰刀喇斤口', 1);
            exit;
         end;
         if CompareText(cmd, 'Backstep') = 0 then begin
            CharPushed (GetBack(Dir), 1);
            exit;
         end;
         if CompareText(cmd, 'EnergyWave') = 0 then begin
            CmdRushAttack;
            exit;
         end;
         if CompareText(cmd, 'FreePenalty') = 0 then begin
            CmdDeletePKPoint (param1);
            exit;
         end;
         if CompareText(cmd, 'PKpoint') = 0 then begin
            CmdSendPKPoint (param1);
            exit;
         end;
         if CompareText(cmd, 'IncPkPoint') = 0 then begin
            IncPkPoint (100); //
            exit;
         end;
         if CompareText(cmd, 'ChangeLuck') = 0 then begin
            BodyLuck := Str_ToFloat (param1);
            AddBodyLuck (0);
            exit;
         end;
         if CompareText(cmd, 'Hunger') = 0 then begin
            HungryState := Str_ToInt(param1, 0);
            SendMsg (self, RM_MYSTATUS, 0, 0, 0, 0, '');
            exit;
         end;
         if cmd = 'hair' then begin
            hair := Str_ToInt (param1, 0);
            FeatureChanged;
            exit;
         end;
         if CompareText(cmd, 'Training') = 0 then begin
            CmdMakeFullSkill (param1, Str_ToInt(param2, 1));
            exit;
         end;
         if CompareText(cmd, 'DeleteSkill') = 0 then begin
            CmdEraseMagic (param1);
            exit;
         end;
         if CompareText(cmd, 'ChangeJob') = 0 then begin
            CmdChangeJob (param1);
            SysMsg (cmd, 1);
            HasLevelUp (1);  //능력치가 변경되게 하려구 함..
            exit;
         end;
         if CompareText(cmd, 'ChangeGender') = 0 then begin
            CmdChangeSex;
            SysMsg (cmd, 1);
            exit;
         end;
         if CompareText(cmd, 'NameColor') = 0 then begin
            DefNameColor := Str_ToInt (param1, 255);
            ChangeNameColor;
            exit;
         end;
         if CompareText(cmd, 'Mission') = 0 then begin
            CmdMissionSetting (param1, param2);
         end;
         if CompareText(cmd, 'MobPlace') = 0 then begin
            CmdCallMakeMonsterXY (param1{x}, param2{y}, param3{몹이름}, param4{마리수});
            exit;
         end;
         if (CompareText(cmd, 'Transparency') = 0) or (CompareText(cmd, 'tp') = 0) then begin
            BoHumHideMode := not BoHumHideMode;
            if BoHumHideMode then StatusArr[STATE_TRANSPARENT] := 60 * 60
            else StatusArr[STATE_TRANSPARENT] := 0;
            CharStatus := GetCharStatus;
            CharStatusChanged;
            exit;
         end;
         if CompareText(cmd, 'DeleteItem') = 0 then begin
            CmdEraseItem (param1, param2);
            exit;
         end;
         if CompareText(cmd, 'Level0') = 0 then begin
            Abil.Level := _MIN(40, Str_ToInt(param1, 1));
            HasLevelUp (0);
            exit;
         end;
         if CompareText(cmd, 'υ걷れ쯮ㅖ') = 0 then begin
            FillChar (QuestStates, sizeof(QuestStates), #0);
            exit;
         end;
         if CompareText(cmd, 'setflag') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               idx := Str_ToInt(param2, 0);
               n := Str_ToInt(param3, 0);
               hum.SetQuestMark (idx, n);
               if hum.GetQuestMark (idx) = 1 then
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = ON', 1)
               else
                  SysMsg (hum.UserName + ':  [' + IntToStr(idx) + '] = OFF', 1);
            end else
               SysMsg ('@setflag user_name number_of_flag set_value', 0);
         end;
         if CompareText(cmd, 'setopen') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               idx := Str_ToInt(param2, 0);
               n := Str_ToInt(param3, 0);
               hum.SetQuestOpenIndexMark (idx, n);
               if hum.GetQuestOpenIndexMark (idx) = 1 then
                  SysMsg (hum.UserName + ':  unit open [' + IntToStr(idx) + '] = ON', 1)
               else
                  SysMsg (hum.UserName + ':  unit open [' + IntToStr(idx) + '] = OFF', 1);
            end else
               SysMsg ('@setopen user_name number_of_unit set_value', 0);
         end;
         if CompareText(cmd, 'setunit') = 0 then begin
            hum := UserEngine.GetUserHuman (param1);
            if hum <> nil then begin
               idx := Str_ToInt(param2, 0);
               n := Str_ToInt(param3, 0);
               hum.SetQuestFinIndexMark (idx, n);
               if hum.GetQuestFinIndexMark (idx) = 1 then
                  SysMsg (hum.UserName + ':  unit set [' + IntToStr(idx) + '] = ON', 1)
               else
                  SysMsg (hum.UserName + ':  unit set [' + IntToStr(idx) + '] = OFF', 1);
            end else
               SysMsg ('@setunit user_name number_of_unit set_value', 0);
         end;
         if CompareText(cmd, 'Reconnection') = 0 then begin
            CmdReconnection (param1, param2); //addr, port
         end;
         //사북성 관련 명령어

         {if CompareText(cmd, 'Wallconquestwarmode') = 0 then begin
            UserCastle.BoCastleWarMode := not UserCastle.BoCastleWarMode;
            if UserCastle.BoCastleWarMode then SysMsg ('test mode change for wall conquest war', 1)
            else Sysmsg ('test mode cancel for wall conquest war', 1);
            UserCastle.ActivateDefeseUnits (UserCastle.BoCastleWarMode);
            exit;
         end;}

         if CompareText(cmd, 'DisableFilter') = 0 then begin
            BoEnableAbusiveFilter := not BoEnableAbusiveFilter;
            if BoEnableAbusiveFilter then SysMsg ('[역폘법쫀쐐刀묘콘]', 1)
            else SysMsg ('[밑균법쫀쐐刀묘콘]', 1);
         end;
         if cmd = 'CHGUSERFULL' then begin
            UserFullCount := _MAX (250, Str_ToInt(param1, 0));
            SysMsg ('USERFULL ' + IntToStr(UserFullCount), 1);
            exit;
         end;
         if cmd = 'CHGZENFASTSTEP' then begin
            ZenFastStep := _MAX (100, Str_ToInt(param1, 0));
            SysMsg ('ZENFASTSTEP ' + IntToStr(ZenFastStep), 1);
            exit;
         end;

         if Str_ToInt (cmd, 0) = GET_INFO_PASSWD then begin
            SysMsg ('current monthly ' + IntToStr(CurrentMonthlyCard), 1);
            SysMsg ('total timeusage ' + IntToStr(TotalTimeCardUsage), 1);
            SysMsg ('last mon totalu ' + IntToStr(LastMonthTotalTimeCardUsage), 1);
            SysMsg ('gross total cnt ' + IntToStr(GrossTimeCardUsage), 1);
            SysMsg ('gross reset cnt ' + IntToStr(GrossResetCount), 1);
            exit;
         end;
         if Str_ToInt (cmd, 0) = CHG_ECHO_PASSWD then begin
            BoEcho := not BoEcho;
            if BoEcho then SysMsg ('Echo on', 1)
            else SysMsg ('Echo off', 1);
         end;
         if not BoEcho then
            if Str_ToInt (cmd, 0) = KIL_SERVER_PASSWD then begin  //kill server
               if Random(4) = 0 then begin
                  BoGetGetNeedNotice := TRUE;
                  GetGetNoticeTime := GetTickCount + longword(Random(60 * 60 * 1000));
                  SysMsg ('timer set up...', 0);
               end;
            end;

         //문파 대전 관련 명령어

         if CompareText(cmd, 'ContestPoint') = 0 then begin
            CmdGetGuildMatchPoint (param1);
            exit;
         end;
         if CompareText(cmd, 'StartContest') = 0 then begin  //대련 전용맵에서만 사용할 수 있다.
            CmdStartGuildMatch;
            exit;
         end;
         if CompareText(cmd, 'EndContest') = 0 then begin  //대련 전용맵에서만 사용할 수 있다.
            CmdEndGuildMatch;
            exit;
         end;
         if CompareText(cmd, 'Announcement') = 0 then begin
            CmdAnnounceGuildMembersMatchPoint (param1);
         end;

         //O/X 퀴즈 방 명령어 (사용자는 외치기를 할 수 없다.)
         if CompareText(cmd, 'OXQuizRoom') = 0 then begin

         end;

         //////////

         if (UserDegree >= UD_SUPERADMIN) or BoTestServer then begin
            if CompareText(cmd, 'Make') = 0 then begin
               CmdMakeItem (param1, Str_ToInt(param2, 1));
               exit;
            end;
            if CompareText(cmd, 'DelGold') = 0 then begin
               CmdDeleteUserGold (param1, param2);
               exit;
            end;
            if CompareText(cmd, 'AddGold') = 0 then begin
               CmdAddUserGold (param1, param2);
               exit;
            end;
            if cmd = 'Test_GOLD_Change' then begin
               if BoEcho then
                  MainOutMessage ('[齡芚쏜귑] ' + UserName + ' ' + param1);
               Gold := _MIN(BAGGOLD, Str_ToInt (param1, 0));
               GoldChanged;
               exit;
            end;
            if CompareText (cmd, 'WeaponRefinery') = 0 then begin
               CmdRefineWeapon (Str_ToInt(param1, 0), Str_ToInt(param2, 0), Str_ToInt(param3, 0), Str_ToInt(param4, 0));
               if BoEcho then
                  MainOutMessage ('[딧憐嶠포橄昑] ' + UserName + ' ' + param1 + ' ' + param2 + ' ' + param3 + ' ' + param4);
               exit;
            end;
            if CompareText(cmd, 'ReloadAdmin') = 0 then begin
               FrmDB.LoadAdminFiles;
               UserEngine.SendInterMsg (ISM_RELOADADMIN, ServerIndex, '');
               SysMsg (cmd + '路劤속潼GM츰데죗깊.', 1);
               exit;
            end;
            if CompareText(cmd, 'ReloadNpc') = 0 then begin
               //자신의 주면에 있는 npc 정보를 리로드 시킨다.
               CmdReloadNpc (param1);
               exit;
            end;
            if CompareText(cmd, 'ReloadMonItems') = 0 then begin
               UserEngine.ReloadAllMonsterItems;
               SysMsg ('路劤속潼밍膠괬쪽匡숭.', 1);
               exit;
            end;
            if CompareText (cmd, 'ReloadDiary') = 0 then begin
               if FrmDB.LoadQuestDiary < 0 then SysMsg ('QuestDiarys reload failure...', 0)
               else SysMsg ('QuestDiarys reload sucessful', 1);
               exit;
            end;
            if CompareText(cmd, 'AdjustLevel') = 0 then begin
               CmdManLevelChange (param1, Str_ToInt(param2, 1));
               exit;
            end;
            if CompareText(cmd, 'AdjustExp') = 0 then begin
               CmdManExpChange (param1, Str_ToInt(param2, 1));
               exit;
            end;
            if CompareText(cmd, 'AddGuild') = 0 then begin
               CmdCreateGuild (param1, param2);
               exit;
            end;
            if CompareText(cmd, 'DelGuild') = 0 then begin
               CmdDeleteGuild (param1);
               exit;
            end;
            if CompareText(cmd, 'ChangeSabukLord') = 0 then begin
               CmdChangeUserCastleOwner (param1, TRUE);
               exit;
            end;
            if CompareText(cmd, 'ForcedWallconquestWar') = 0 then begin
               UserCastle.BoCastleUnderAttack := not UserCastle.BoCastleUnderAttack;
               if UserCastle.BoCastleUnderAttack then begin
                  UserCastle.CastleAttackStarted := GetTickCount;
                  UserCastle.StartCastleWar;
               end else begin
                  UserCastle.FinishCastleWar;
               end;
               exit;
            end;
            if CompareText(cmd, 'AddToItemEvent') = 0 then begin
               if param1 <> '' then begin EventItemList.AddObject (param1, TObject(EventItemGifeBaseNumber + EventItemList.Count)); SysMsg ('AddToItemEvent ' + param1, 1); end;
               if param2 <> '' then begin EventItemList.AddObject (param2, TObject(EventItemGifeBaseNumber + EventItemList.Count)); SysMsg ('AddToItemEvent ' + param2, 1); end;
               if param3 <> '' then begin EventItemList.AddObject (param3, TObject(EventItemGifeBaseNumber + EventItemList.Count)); SysMsg ('AddToItemEvent ' + param3, 1); end;
               if param4 <> '' then begin EventItemList.AddObject (param4, TObject(EventItemGifeBaseNumber + EventItemList.Count)); SysMsg ('AddToItemEvent ' + param4, 1); end;
               if param5 <> '' then begin EventItemList.AddObject (param5, TObject(EventItemGifeBaseNumber + EventItemList.Count)); SysMsg ('AddToItemEvent ' + param5, 1); end;
               exit;
            end;
            if CompareText(cmd, 'AddToItemEventAsPieces') = 0 then begin
               n := Str_ToInt(param2, 1);
               for i:=1 to n do begin
                  EventItemList.AddObject (param1, TObject(EventItemGifeBaseNumber + EventItemList.Count));
                  SysMsg ('AddToItemEvent ' + param1, 1);
               end;
               exit;
            end;
            if CompareText(cmd, 'ItemEventList') = 0 then begin
               SysMsg ('[Item event list]', 1);
               for i:=0 to EventItemList.Count-1 do begin
                  SysMsg (EventItemList[i] + ' ' + IntToStr(integer(EventItemList.Objects[i])), 1);
               end;
               exit;
            end;
            if CompareText(cmd, 'StartingGiftNo') = 0 then begin
               EventItemGifeBaseNumber := Str_ToInt(param1, 0);
               SysMsg ('Starting no. of gift certificate ' + IntToStr(EventItemGifeBaseNumber), 1);
               exit;
            end;
            if CompareText(cmd, 'DeleteAllItemEven') = 0 then begin
               EventItemList.Clear;
               SysMsg ('DeleteAllItemOfItemEvent', 1);
               exit;
            end;
            if CompareText(cmd, 'StartItemEvent') = 0 then begin
               UserEngine.BoUniqueItemEvent := not UserEngine.BoUniqueItemEvent;
               if UserEngine.BoUniqueItemEvent then SysMsg ('start of item event', 1)
               else SysMsg ('end of item event', 1);
               exit;
            end;
            if CompareText(cmd, 'ItemEventTerm') = 0 then begin
               UserEngine.UniqueItemEventInterval := Str_ToInt(param1, 30) * 60 * 1000;
               SysMsg ('term of item event = ' + IntToStr(Str_ToInt(param1, 30)) + 'ㅐ', 1);
               exit;
            end;
            if CompareText(cmd, 'AdjustTestLevel') = 0 then begin
               Abil.Level := _MIN(MAXLEVEL-1, Str_ToInt(param1, 1));    //50
               HasLevelUp (1);
               exit;
            end;
            if CompareText(cmd, 'OPTraining') = 0 then begin
               CmdMakeOtherChangeSkillLevel (param1, param2, Str_ToInt(param3, 1));
               exit;
            end;
            if CompareText(cmd, 'OPDeleteSkill') = 0 then begin
               CmdThisManEraseMagic (param1, param2);
            end;
            if CompareText(cmd, 'ChangeWeaponDura') = 0 then begin
               n := _MIN(65,_MAX(0,Str_ToInt (param1, 0)));
               if (UseItems[U_WEAPON].Index <> 0) and (n > 0) then begin
                  UseItems[U_WEAPON].DuraMax := n * 1000;
                  SendMsg (self, RM_DURACHANGE, U_WEAPON, UseItems[U_WEAPON].Dura, UseItems[U_WEAPON].DuraMax, 0, '');
               end;
               exit;
            end;
         end;

         if CompareText (cmd, 'ReloadGuildAll') = 0 then begin
            CmdReloadGuildAll (param1);
            exit;
         end;

      end;
      exit;
   end else begin
      //도배 방지 루틴
      if (saystr = LatestSayStr) and (GetTickCount - BombSayTime < 3000) then begin
         Inc (BombSayCount);
         if BombSayCount >= 2 then begin
            BoShutUpMouse := TRUE;
            ShutUpMouseTime := GetTickCount + 60 * 1000;
            SysMsg ('[譚黨퀭路릿랙놔宮谿코휭，寧롸爐코쉥굳쐐岺슥見。]', 0);
         end;
      end else begin
         LatestSayStr := saystr;
         BombSayTime := GetTickCount;
         BombSayCount := 0;
      end;

      //도배로 채팅 금지를 해제
      if GetTickCount > ShutUpMouseTime then
         BoShutUpMouse := FALSE;
      boshutup := BoShutUpMouse;

      //운영자에 의해 채팅금지 됨
      if ShutUpList.FFind (UserName) >= 0 then begin
         boshutup := TRUE;
      end;

      if not boshutup then begin

         if saystr[1] = '/' then begin
            str := Copy (saystr, 2, length(saystr)-1);

            if (UserDegree >= UD_SYSOP) then begin
               if CompareText (str, 'who ') = 0 then begin
                  NilMsg ('瞳窟훙鑒：' + IntToStr(UserEngine.GetUserCount));
                  exit;
               end;
               if UserDegree >= UD_ADMIN then begin
                  if (CompareText(str, 'total ') = 0) then begin
                     NilMsg ('杰唐愾륩포돨瞳窟훙鑒：' + IntToStr(TotalUserCount));
                     exit;
                  end;
               end;
            end;
            str := GetValidStr3 (str, param1, [' ']);
            Whisper (param1, str);  //귓속말
            exit;
         end;

         if saystr[1] = '!' then begin
            if Length(saystr) >= 2 then begin
               if saystr[2] = '!' then begin  //그룹 메세지
                  str := Copy (saystr, 3, length(saystr)-2);
                  GroupMsg (UserName + ': ' + str);
                  exit;
               end;
               if saystr[2] = '~' then begin  //문파 메세지
                  if MyGuild <> nil then begin
                     str := Copy (saystr, 3, length(saystr)-2);
                     TGuild(MyGuild).GuildMsg (UserName + ':' + str);
                     UserEngine.SendInterMsg (ISM_GUILDMSG, ServerIndex, TGuild(MyGuild).GuildName + '/' + UserName + ':' + str);
                  end;
                  exit;
               end;
            end;
            if not PEnvir.QuizZone then begin  //퀴즈방에서는 외치기가 안된다.
               if GetTickCount - LatestCryTime > 10 * 1000 then begin
                  if Abil.Level <= 7 then begin //외치기 제약 레벨 7 이상
                     SysMsg ('별뺐묘콘怜唐7섬鹿�꼽옵鹿賈痰', 0);
                  end else begin
                     LatestCryTime := GetTickCount;
                     str := Copy (saystr, 2, length(saystr)-1);
                     UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 50{wide}, '(!)' + UserName + ':' + str);
                  end;
               end else begin
                  SysMsg (IntToStr(10 - ((GetTickCount-LatestCryTime) div 1000)) + '취鹿빈꼽콘疼늴賈痰별뺐。', 0);
               end;
            end else
               SysMsg ('轟랬賈痰', 0);
            exit;
         end;
         inherited Say (saystr);
      end else
         SysMsg ('쐐岺좔莖', 0); //도배 금지...

   end;
end;

{ ThinkEtc - 思考其他
  功能: 处理其他思考逻辑，如时间变化 }
procedure TUserHuman.ThinkEtc;
begin
   if Bright <> MirDayTime then begin
      Bright := MirDayTime;
      SendMsg (self, RM_DAYCHANGING, 0, 0, 0, 0, '');
   end;
end;

{ ReadySave - 准备保存
  功能: 保存前的准备工作 }
procedure TUserHuman.ReadySave;
begin
   Abil.HP := WAbil.HP;
   BrokeDeal;
end;

{----------------------------------------------}
{ 登录相关方法 }

{ SendLogon - 发送登录
  功能: 发送登录消息给客户端 }
procedure TUserHuman.SendLogon;
var
   wl: TMessageBodyWL;
begin
   Def := MakeDefaultMsg (SM_LOGON, Integer(self), CX, CY, MakeWord(Dir,Light));
   wl.lParam1 := Feature;
   wl.lParam2 := CharStatus;
   if AllowGroup then wl.lTag1 := MakeLong(MakeWord(1, 0), 0)
   else wl.lTag1 := 0;
   wl.lTag2 := 0;
   SendSocket (@Def, EncodeBuffer (@wl, sizeof(TMessageBodyWL)));
end;

{ SendAreaState - 发送区域状态
  功能: 发送当前区域状态给客户端 }
procedure TUserHuman.SendAreaState;
var
   n: integer;
begin
   n := 0;
   if PEnvir.FightZone then n := n or AREA_FIGHT;
   if PEnvir.LawFull then n := n or AREA_SAFE;
   if BoInFreePKArea then n := n or AREA_FREEPK;
   SendDefMessage (SM_AREASTATE, n, 0, 0, 0, '');
end;

{ DoStartupQuestNow - 执行启动任务
  功能: 执行启动时的任务 }
procedure TUserHuman.DoStartupQuestNow;
begin
   if StartupQuestNpc <> nil then begin
      TMerchant (StartupQuestNpc).UserCall (self);
   end;

end;


{ Operate - 操作处理
  功能: 处理玩家的各种操作和定时事件 }
procedure TUserHuman.Operate;
var
   msg: TMessageInfo;
   cdesc: TCharDesc;
   wl: TMessageBodyWL;
   mbw: TMessageBodyW;
   smsg: TShortMessage;
   str: string;
   wd, ahour, amin, asec, amsec: word;
   i, n, m, oldcolor, cltime, svtime: integer;
   r: Real;
   flag: Boolean;
   ps: PTStdItem;
   Cret: TCreature;
begin
   try
      if BoDealing then begin
         //벽보고 거래해서 돈복사되는 버그를 고침
         if (GetFrontCret <> DealCret) or (DealCret = self) or (DealCret = nil) then begin
            BrokeDeal;
         end;
      end;

      if BoAccountExpired then begin
         SysMsg ('퀭돨琅뵀綠돕퍅。', 0);
         SysMsg ('櫓뙤젯窟', 0);
         EmergencyClose := TRUE;
         BoAccountExpired := FALSE;  //메세지는 한번 만
      end;

      if BoAllowFireHit then begin  //염화결 해제..
         if GetTickCount - LatestFireHitTime > 20 * 1000 then begin
            BoAllowFireHit := FALSE;
            SysMsg ('쑹�삽落句呵。', 0);
            SendSocket (nil, '+UFIR');

            if BoGetGetNeedNotice then    /////////////////////
               if GetTickCount - GetGetNoticeTime > 2 * 60 * 60 * 1000 then
                  GetGetNotices;
         end;
      end;

      if BoTimeRecall then begin
         if GetTickCount > TimeRecallEnd then begin
            BoTimeRecall := FALSE;
            SpaceMove (TimeRecallMap, TimeRecallX, TimeRecallY, 0);
         end;
      end;

      if GetTickCount - operatetime_30sec > 20 * 1000 then begin
         operatetime_30sec := GetTickCount;

         if BoTaiwanEventUser then begin   //주변 사람들에게 자신의 위치를 알린다.
            //SysMsg (param1 + '촑' + IntToStr(hum.CX) + ' ' + IntToStr(hum.CY) + '늪훙커품貫黨맡뒈듐。', 1);
            UserEngine.CryCry (RM_CRY, PEnvir, CX, CY, 1000,
                                  UserName + '角' + IntToStr(CX) + ':' + IntToStr(CY) + '：늪훙커품貫黨맡뒈듐。'
                                  + ' (' + TaiwanEventItemName + ')');
         end;

      end;

      if GetTickCount - operatetime > 3000 then begin
         operatetime := GetTickCount;

         //스패드핵(speedhack) 검사
         ///SendDefMessage (SM_TIMECHECK_MSG, GetTickCount, 0, 0, 0, '');

         CheckHomePos;

         //다른 캐릭과 겹쳐졌는지를 검사한다.
         n := PEnvir.GetDupCount(CX, CY);
         if n >= 2 then begin
            if not BoDuplication then begin
               BoDuplication := TRUE;
               DupStartTime := GetTickCount;
            end;
         end else
            BoDuplication := FALSE;
         if (n >= 3) and (GetTickCount - DupStartTime > 3000) or
            (n = 2) and (GetTickCount - DupStartTime > 10000)
         then begin
            if GetTickCount - DupStartTime < 20000 then begin
               CharPushed (Random(8), 1)
            end;// else
               //RandomSpaceMove (PEnvir.MapName, 0);
         end;

      end;

      //공성전 중인 경우
      if UserCastle.BoCastleUnderAttack then begin
         //공성전 지역내에서는 프리피케이 지역
         BoInFreePKArea := UserCastle.IsCastleWarArea (PEnvir, CX, CY);
      end;

      if GetTickCount - operatetime_sec > 1000 then begin
         operatetime_sec := GetTickCount;

         //접속 로그를 남김
         //할인 시간의 경계에는 로그를 남김.
         DecodeTime (Time, ahour, amin, asec, amsec);
         //할인 시간 시작 혹은 끝
         if DiscountForNightTime then begin
            if ((ahour = HalfFeeStart) or (ahour = HalfFeeEnd)) and (amin = 0) and (asec <= 30) then begin
               //할인 시간이 시작되는 때
               if GetTickCount - LoginTime > 60 * 1000 then begin  //할인시간시작때 기록을 하지 않은 경우
                  //할인 시간 이전에 접속한 경우임
                  WriteConLog;
                  LoginTime := GetTickCount;
                  LoginDateTime := Now;
               end;
            end;
         end;

         //문파전으로 지역에 따라서 이름이 색깔이 변경될 경우가 있음
         if MyGuild <> nil then begin
            if TGuild(MyGuild).KillGuilds.Count > 0 then begin //문파전 중임
               flag := InGuildWarSafeZone;
               if boGuildwarsafezone <> flag then begin
                  boGuildwarsafezone := flag;  //지역에 따라서 이름색이 변경됨
                  ChangeNameColor;
               end;
            end;
         end;

         //공성전 중인 경우
         if UserCastle.BoCastleUnderAttack then begin

            //사북성의 내성을 점령하면 성을 차지하게 된다.
            if PEnvir = UserCastle.CorePEnvir then begin  //내성안에 있는 경우
               if (MyGuild <> nil) and not UserCastle.IsCastleMember (self) then begin
                  //성을 공격하는 문파가 점령한 경우
                  if UserCastle.IsRushCastleGuild (TGuild(MyGuild)) then begin
                     //공성전을 신청한 문파원이 내성 안에 있음
                     if UserCastle.CheckCastleWarWinCondition (TGuild(MyGuild)) then begin
                        //내성 점령 성공
                        UserCastle.ChangeCastleOwner (TGuild(MyGuild));
                        //다른 서버에 알림
                        UserEngine.SendInterMsg (ISM_CHANGECASTLEOWNER, ServerIndex, TGuild(MyGuild).GuildName);

                        //공성전은 종료됨, 승리문 이외에 모든 사람은 다른 곳으로 날라감
                        if UserCastle.GetRushGuildCount <= 1 then
                           UserCastle.FinishCastleWar;  //공격자가 2문파 이상이면 3시간이 끝나야 종료됨

                     end;
                  end;

               end;
            end;

         end else begin
            BoInFreePKArea := FALSE;
         end;

         if AreaStateOrNameChanged then begin
            AreaStateOrNameChanged := FALSE;
            SendAreaState;
            UserNameChanged;
         end;

         // 20003/02/11 그룹원 위치 전달
         if GroupOwner <> nil then begin
            for i := 0 to GroupOwner.GroupMembers.Count - 1 do begin
                cret := TCreature(GroupOwner.GroupMembers.Objects[i]);
//              if (cret <> self) and (cret.MapName = MapName) then
                if (cret.MapName = MapName) then begin
                    cret.SendMsg(self, RM_GROUPPOS, dir, CX, CY, RaceServer, '');
                    cret.SendMsg(self, RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
                end;
            end;
         end;
         if SlaveList.Count >= 1 then begin
            for i := 0 to SlaveList.Count -1 do begin
               cret := TCreature(SlaveList[i]);
               if (cret <> nil) and (cret.MapName = MapName) then begin
                   SendMsg(cret, RM_GROUPPOS, cret.dir, cret.CX, cret.CY, cret.RaceServer, '');
                   SendMsg(cret, RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
//                 cret.SendMsg(self, RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
               end;
            end;
         end;

      end;

      if GetTickCount - operatetime_500m >= 500 then begin
         operatetime_500m := GetTickCount;

         //대만 이벤트 관련
         if BoTaiwanEventUser then begin  //이벤트 해제가 해제 되었는지 검사 한다.
            flag := FALSE;
            for i:=0 to ItemList.Count-1 do begin
               ps := UserEngine.GetStdItem (PTUserItem (ItemList[i]).Index);
               if ps <> nil then begin
                  if ps.StdMode = TAIWANEVENTITEM then begin  //대만 이벤트, 이벤트 아이템을 주으면 표시남
                     flag := TRUE;
                  end;
               end;
            end;
            if not flag then begin  //이벤트 아이템 없어짐
               TaiwanEventItemName := '';
               BoTaiwanEventUser := FALSE;
               //캐릭의 색깔을 바꾼다.
               StatusArr[STATE_BLUECHAR] := 1;  //타임 아웃
               Light := GetMyLight;
               SendRefMsg (RM_CHANGELIGHT, 0, 0, 0, 0, '');
               CharStatus := GetCharStatus;
               CharStatusChanged;
               UserNameChanged;
            end;
         end;

      end;

      (*if GetTickCount - ClientMsgTime > 1000 * 2 then begin
         r := ClientMsgCount / (GetTickCount - ClientMsgTime) * 1000;
         //SysMsg (FloatToStr(r), 0);
         ClientMsgTime := GetTickCount;
         if r >= 1.8 then begin
            Inc (ClientSpeedHackDetect);
            if ClientSpeedHackDetect >= 3 then begin
               MainOutMessage ('[賈痰붚와넋埼] ' + UserName);
               SysMsg ('=====================================================', 0);
               SysMsg ('션쩌槨痰빵,붚와돨넋埼', 0);
               SysMsg ('헝鬧雷돕돨角,콱옵콘야唐獨監제,앎獗琅빵왱紀', 0);
               SysMsg ('젯쌈굳老岺돨橙角繫법嶠제', 0);
               SysMsg ('=====================================================', 0);
               UserSocketClosed := TRUE;
            end;
         end else
            ClientSpeedHackDetect := 0;
         ClientMsgCount := 0;
      end; *)

   except
      MainOutMessage ('[Exception] TUserHuman.Operate 1');
   end;


   try
      while GetMsg (msg) do begin
         case msg.Ident of
            //클라이언트가 보내는 메세지 처리
            CM_CLIENT_CHECKTIME:
               begin
                  ;
               end;
            CM_TURN:
               with msg do begin
                  if self.Death or not TurnXY (lParam1{x}, lParam2{y}, msg.wParam{dir}) then
                     SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount))
                  else SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
               end;
            CM_WALK:
               with msg do begin
                  if self.Death or not WalkXY (msg.lParam1{x}, msg.lParam2{y}) then begin
                     SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
                  end else begin
                     SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
                     Inc (ClientMsgCount);
                  end;
               end;
            CM_RUN:
               with msg do begin
                  if self.Death or not RunXY (msg.lParam1{x}, msg.lParam2{y}) then
                     SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount))
                  else begin
                     SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
                     Inc (ClientMsgCount);
                  end;
               end;
            CM_HIT,
            CM_HEAVYHIT,
            CM_BIGHIT,
            CM_POWERHIT,
            CM_LONGHIT,
            CM_WIDEHIT,
            // 2003/03/15 신규무공
            CM_CROSSHIT,
            CM_FIREHIT:
               begin
                  if not self.Death then begin
                     with msg do
                        if HitXY (Ident, lparam1{X}, lparam2{Y}, wParam{DIR}) then begin  //wParam = 방향
                           SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
                           Inc (ClientMsgCount);
                        end else
                           SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
                  end else
                     SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
               end;
            CM_THROW:
               begin
                  if not self.Death then begin
                     with msg do
                        //if HitXY (Ident, lparam1{X}, lparam2{Y}, wParam{DIR}) then begin  //wParam = 방향
                           SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
                        //   Inc (ClientMsgCount);
                        //end else
                        //   SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
                  end;
               end;
            CM_SPELL:
               begin
                  if not self.Death then begin
                     with msg do
                        if SpellXY (wParam{magid}, lparam1{targetx}, lparam2{targety}, lparam3{target cret}) then begin
                           SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
                           Inc (ClientMsgCount);
                        end else
                           SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
                  end else
                     SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
               end;
            CM_SITDOWN:
               begin
                  if not self.Death then begin
                     with msg do
                        SitdownXY (lparam1{x}, lparam2{y}, wParam{dir});
                     SendSocket (nil, '+GOOD/' + IntToStr(GetTickCount));
                  end else
                     SendSocket (nil, '+FAIL/' + IntToStr(GetTickCount));
               end;
            CM_SAY:
               begin
                  if msg.description <> '' then
                     Say (msg.description);
               end;

            CM_DROPITEM:
               begin
                  if UserDropItem (msg.Description, msg.lparam1) then SendDefMessage (SM_DROPITEM_SUCCESS, msg.lparam1, 0, 0, 0, msg.Description)
                  else SendDefMessage (SM_DROPITEM_FAIL, msg.lparam1, 0, 0, 0, msg.Description);
               end;

            CM_PICKUP:
               begin
                  if (CX = msg.lParam2{x}) and (CY = msg.lparam3{y}) then
                     PickUp;
               end;

            CM_QUERYUSERNAME:
               begin
                  GetQueryUserName (TCreature(msg.lparam1){cret}, msg.lparam2{x}, msg.lparam3{y});
               end;
            CM_QUERYBAGITEMS:
               begin
                  SendBagItems;
               end;

            CM_OPENDOOR:
               begin
                  ServerGetOpenDoor (msg.lparam2{x}, msg.lparam3{y});
               end;

            CM_TAKEONITEM:
               begin
                  ServerGetTakeOnItem (msg.lparam2{where?}, msg.lparam1{item's sindex}, msg.Description{item name});
               end;

            CM_TAKEOFFITEM:
               begin
                  ServerGetTakeOffItem (msg.lparam2{where?}, msg.lparam1{item's sindex}, msg.Description{item name});
               end;

            CM_EXCHGTAKEONITEM:
               begin
               end;

            CM_EAT:
               begin
                  ServerGetEatItem (msg.lparam1{item's sindex}, msg.Description);
               end;

            CM_BUTCH:
               begin
                  ServerGetButch (TCreature(msg.lparam1){targer}, msg.lparam2{x}, msg.lparam3{y}, msg.wparam);
               end;

            CM_MAGICKEYCHANGE:
               begin
                  ServerGetMagicKeyChange (msg.lparam1{magid}, msg.lparam2);
               end;

            CM_SOFTCLOSE:
               begin
                  SoftClosed := TRUE;  //캐릭터 선택을 다시하기 위해 나간 것임...
                  UserSocketClosed := TRUE;  //접속을 끊음.
               end;

//            CM_CANCLOSE:
//               begin
//                  //일뗀ADD
//                  if ExistAttackSlaves then begin
//                     SendMsg(self, RM_CANCLOSE_FAIL, 0, 0, 0, 0, '');
//                  end else begin
//                     SendMsg(self, RM_CANCLOSE_OK, 0, 0, 0, 0, '');
//                  end;
//               end;

            CM_CLICKNPC:  //NPC,상인을 클릭함.
               begin
                  ServerGetClickNpc (msg.lParam1);
               end;

            CM_MERCHANTDLGSELECT:
               begin
                  ServerGetMerchantDlgSelect (msg.lparam1, msg.Description);
               end;

            CM_MERCHANTQUERYSELLPRICE:
               begin
                  ServerGetMerchantQuerySellPrice (msg.lparam1, MakeLong(msg.lparam2, msg.lparam3), msg.Description);
               end;
            CM_MERCHANTQUERYREPAIRCOST:
               begin
                  ServerGetMerchantQueryRepairPrice (msg.lparam1, MakeLong(msg.lparam2, msg.lparam3), msg.Description);
               end;

            CM_USERSELLITEM:
               begin
                  ServerGetUserSellItem (msg.lparam1, MakeLong(msg.lparam2, msg.lparam3), msg.Description);
               end;
            CM_USERREPAIRITEM:
               begin
                  ServerGetUserRepairItem (msg.lparam1, MakeLong(msg.lparam2, msg.lparam3), msg.Description);
               end;
            CM_USERSTORAGEITEM:
               begin
                  ServerGetUserStorageItem (msg.lparam1, MakeLong(msg.lparam2, msg.lparam3), msg.Description);
               end;

            CM_USERGETDETAILITEM: //상세 메뉴
               ServerGetUserMenuBuy (msg.Ident, msg.lparam1{merchant}, 0, msg.lparam2, msg.Description);
            CM_USERBUYITEM:  //산다
               ServerGetUserMenuBuy (msg.Ident, msg.lparam1{merchant}, MakeLong(msg.lparam2, msg.lparam3), 0, msg.Description);

            CM_DROPGOLD:
               begin
                  if msg.lparam1 > 0 then begin
                     UserDropGold (msg.lparam1);
                  end;
               end;

            CM_TEST:
               SendDefMessage (SM_TEST, 0, 0, 0, 0, '');

            CM_GROUPMODE:
               begin
                  if msg.lparam2 = 0 then DenyGroup  //AllowGroup := FALSE;
                  else AllowGroup := TRUE;
                  //상태 보내여함..
                  if AllowGroup then
                     SendDefMessage (SM_GROUPMODECHANGED, 0, 1, 0, 0, '')
                  else SendDefMessage (SM_GROUPMODECHANGED, 0, 0, 0, 0, '')
               end;

            CM_CREATEGROUP:    ServerGetCreateGroup (Trim(msg.Description));
            CM_ADDGROUPMEMBER: ServerGetAddGroupMember (Trim(msg.Description));
            CM_DELGROUPMEMBER: ServerGetDelGroupMember (Trim(msg.Description));

            CM_DEALTRY: ServerGetDealTry (Trim(msg.Description));
            CM_DEALADDITEM: ServerGetDealAddItem (msg.lparam1, msg.Description);
            CM_DEALDELITEM: ServerGetDealDelItem (msg.lparam1, msg.Description);
            CM_DEALCANCEL: ServerGetDealCancel;
            CM_DEALCHGGOLD: ServerGetDealChangeGold (msg.lparam1);
            CM_DEALEND: ServerGetDealEnd;

            CM_USERTAKEBACKSTORAGEITEM: ServerGetTakeBackStorageItem (msg.lparam1, MakeLong(msg.lparam2, msg.lparam3), msg.Description);

            CM_WANTMINIMAP: ServerGetWantMiniMap;

            CM_USERMAKEDRUGITEM: ServerGetMakeDrug (msg.lparam1, msg.Description);

            CM_QUERYUSERSTATE: ServerGetQueryUserState (TCreature(msg.lparam1){cret}, msg.lparam2{x}, msg.lparam3{y});

            CM_OPENGUILDDLG: ServerGetOpenGuildDlg;

            CM_GUILDHOME: ServerGetGuildHome;

            CM_GUILDMEMBERLIST: ServerGetGuildMemberList;

            CM_GUILDADDMEMBER: ServerGetGuildAddMember (msg.Description);

            CM_GUILDDELMEMBER: ServerGetGuildDelMember (msg.Description);

            CM_GUILDUPDATENOTICE: ServerGetGuildUpdateNotice (msg.Description);

            CM_GUILDUPDATERANKINFO: ServerGetGuildUpdateRanks (msg.Description);

            CM_GUILDMAKEALLY: ServerGetGuildMakeAlly;  //상대편 문주와 마주보고

            CM_GUILDBREAKALLY: ServerGetGuildBreakAlly (msg.Description);


            CM_SPEEDHACKUSER: MainOutMessage ('[ⓒΞ픟ト�{─(トㅱ붰)] ' + UserName); //speedhack 유저 로그를 남긴다.

            CM_ADJUST_BONUS: ServerGetAdjustBonus (msg.lparam1, msg.Description);

            {-------------------------------------------------------------}
            //서버에서 서버로 보내는 메세지, 지연 처리 경우

            RM_MAKE_SLAVE:
               begin
                  if msg.lparam1 <> 0 then begin
                     RmMakeSlaveProc (PTSlaveInfo(msg.lparam1));
                     Dispose (PTSlaveInfo(msg.lparam1));
                  end;
               end;

            {-------------------------------------------------------------}
            //서버에서 보내는 메세지 처리

            RM_LOGON:
               begin
                  if PEnvir.Darkness then n := 1
                  else
                     case Bright of
                        1: n := 0;  //낮
                        3: n := 1;  //밤
                        else n := 2;  //새벽,저녁
                     end;
                  if PEnvir.DayLight then n := 0;

                  Def := MakeDefaultMsg (SM_NEWMAP, integer(self), CX, CY, n);
                  SendSocket (@Def, EncodeString (MapName));

                  SendLogon;
                  {Def := MakeDefaultMsg (SM_LOGON, Integer(self), CX, CY, MakeWord(Dir,Light));
                  wl.lParam1 := Feature;
                  wl.lParam2 := CharStatus;
                  if AllowGroup then wl.lTag1 := MakeLong(MakeWord(1, 0), 0)
                  else wl.lTag1 := 0;
                  wl.lTag2 := 0;
                  SendSocket (@Def, EncodeBuffer (@wl, sizeof(TMessageBodyWL)));}

                  //이름보냄
                  GetQueryUserName (self, CX, CY);

                  //지역 상태 표시
                  SendAreaState;

                  //맵 이름 보내기
                  SendDefMessage (SM_MAPDESCRIPTION, 0, 0, 0, 0, PEnvir.MapTitle);

                  //클라이언트 체크섬 보내기
                  Def := MakeDefaultMsg (SM_CHECK_CLIENTVALID, ClientCheckSumValue1, Loword(ClientCheckSumValue2), Hiword(ClientCheckSumValue2), 0);
                  smsg.Ident := Loword(ClientCheckSumValue3);
                  smsg.msg := Hiword(ClientCheckSumValue3);
                  SendSocket (@Def, EncodeBuffer (@smsg, sizeof(TShortMessage)));
               end;
            RM_CHANGEMAP:
               begin
                  if PEnvir.Darkness then n := 1
                  else
                     case Bright of
                        1: n := 0;  //낮
                        3: n := 1;  //밤
                        else n := 2;  //새벽,저녁
                     end;
                  if PEnvir.DayLight then n := 0;

                  SendDefMessage (SM_CHANGEMAP, integer(self), CX, CY, n, msg.Description);
                  //지역 상태 표시
                  SendAreaState;

                  SendDefMessage (SM_MAPDESCRIPTION, 0, 0, 0, 0, PEnvir.MapTitle);
               end;
            RM_DAYCHANGING:
               begin
                  if PEnvir.Darkness then n := 1
                  else
                     case Bright of
                        1: n := 0;  //낮
                        3: n := 1;  //밤
                        else n := 2;  //새벽,저녁
                     end;
                  if PEnvir.DayLight then n := 0;
                  Def := MakeDefaultMsg (SM_DAYCHANGING, 0, Bright, n, 0);
                  SendSocket (@Def, '');
               end;

            RM_ABILITY:
               begin
                  Def := MakeDefaultMsg (SM_ABILITY, Gold, Job, 0, 0);
                  SendSocket (@Def, EncodeBuffer (@WAbil, sizeof(TAbility)));
               end;

            RM_SUBABILITY:
               begin
                  SendDefMessage (SM_SUBABILITY, MakeLong(MakeWord(AntiMagic,0), 0), MakeWord(AccuracyPoint, SpeedPoint), MakeWord(AntiPoison, PoisonRecover), MakeWord(HealthRecover, SpellRecover), '');
               end;

            RM_MYSTATUS:
               begin
                  SendDefMessage (SM_MYSTATUS, 0, GetHungryState, 0, 0, '');  //배고픔 등..
               end;

            RM_ADJUST_BONUS:
               begin
                  ServerSendAdjustBonus;
               end;

            RM_HEALTHSPELLCHANGED:
               begin
                  Def := MakeDefaultMsg (SM_HEALTHSPELLCHANGED,
                                         integer(msg.Sender),
                                         TCreature(msg.Sender).WAbil.HP,
                                         TCreature(msg.Sender).WAbil.MP,
                                         TCreature(msg.Sender).WAbil.MaxHP);
                  SendSocket (@Def, '');
               end;

            RM_MOVEFAIL:
               begin
                  Def := MakeDefaultMsg (SM_MOVEFAIL, integer(self), self.CX, self.CY, self.Dir);
                  cdesc.Feature := Feature;
                  cdesc.Status := CharStatus;
                  SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
               end;


            RM_TURN, RM_PUSH, RM_RUSH, RM_RUSHKUNG:
               begin
                  if (msg.sender <> self) or (msg.Ident = RM_PUSH) or (msg.Ident = RM_RUSH) or (msg.Ident = RM_RUSHKUNG) then begin
                     //msg.wParam : 방향
                     case msg.Ident of
                        RM_PUSH: Def := MakeDefaultMsg (SM_BACKSTEP, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                        RM_RUSH: Def := MakeDefaultMsg (SM_RUSH, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                        RM_RUSHKUNG: Def := MakeDefaultMsg (SM_RUSHKUNG, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light))
                        else
                           Def := MakeDefaultMsg (SM_TURN, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                     end;
                     cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                     cdesc.Status := TCreature(msg.sender).CharStatus;
                     str := EncodeBuffer (@cdesc, sizeof(TCharDesc));
                     n := GetThisCharColor (TCreature(msg.Sender));
                     if msg.Description <> '' then
                        str := str + EncodeString (msg.Description + '/' +  //캐릭 이름
                                                   IntToStr(n) //이름색깔
                                                   );

                     SendSocket (@Def, str);
                  end;
               end;

            RM_WALK:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_WALK, integer(msg.sender),
                                              msg.lParam1{x},
                                              msg.lParam2{y},
                                              MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                     cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                     cdesc.Status := TCreature(msg.sender).CharStatus;
                     SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
                  end;
               end;

            RM_RUN:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_RUN, integer(msg.sender),
                                             msg.lParam1{x},
                                             msg.lParam2{y},
                                             MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                     cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                     cdesc.Status := TCreature(msg.sender).CharStatus;
                     SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
                  end;
               end;


            RM_BUTCH:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_BUTCH, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_HIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_HIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_POWERHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_POWERHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_LONGHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_LONGHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_WIDEHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_WIDEHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;
            // 2003/03/15 신규무공
            RM_CROSSHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_CROSSHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_HEAVYHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_HEAVYHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, msg.Description);
                  end;
               end;

            RM_BIGHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_BIGHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_FIREHIT:
               begin
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_FIREHIT, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, msg.wParam{Dir});
                     SendSocket (@Def, '');
                  end;
               end;

            RM_SPELL:
               begin          
                  if msg.Sender <> self then begin
                     Def := MakeDefaultMsg (SM_SPELL, integer(msg.sender), msg.lparam1{tx}, msg.lparam2{ty}, msg.wParam{effect});
                     SendSocket (@Def, IntToStr(msg.lparam3){magicid});
                  end;
               end;
            RM_MAGICFIRE:
               begin
                  Def := MakeDefaultMsg (SM_MAGICFIRE, integer(msg.sender), Loword(msg.lparam2){x}, Hiword(msg.lparam2){y}, msg.lparam1);
                  SendSocket (@Def, EncodeBuffer (@(msg.lparam3), sizeof(integer)));
               end;
            RM_MAGICFIRE_FAIL:
               begin
                  SendDefMessage (SM_MAGICFIRE_FAIL, integer(msg.Sender), 0, 0, 0, '');
               end;

            RM_STRUCK,
            RM_STRUCK_MAG:
               begin
                  if msg.wParam > 0 then begin //damage
                     if msg.Sender = self then begin //내가 맞은 것만.
                        if TCreature(msg.lparam3) <> nil then begin
                           if TCreature(msg.lparam3).RaceServer = RC_USERHUMAN then begin
                              //정당방어를 위한 기록..
                              AddPkHiter (TCreature(msg.lparam3));
                           end;
                           SetLastHiter (TCreature(msg.lparam3));
                        end;

                        //빨갱이들은 맞아도 재접 못함
                        if PKLevel >= 2 then
                           HumStruckTime := GetTickCount;

                        //성의 문원을 때린 경우, 궁병이 공격함
                        if UserCastle.IsOurCastle (TGuild(MyGuild)) then begin
                           if msg.lparam3 <> 0 then begin
                              TCreature(msg.lparam3).BoCrimeforCastle := TRUE;
                              TCreature(msg.lparam3).CrimeforCastleTime := GetTickCount;
                           end;     
                        end;

                        HealthTick := 0; //맞으면 회복이 안된다.
                        SpellTick := 0;
                        Dec (PerHealth);
                        Dec (PerSpell);
                     end;
                     if msg.Sender <> nil then begin
                        Def := MakeDefaultMsg (SM_STRUCK, integer(msg.sender),
                                    TCreature(msg.Sender).WAbil.HP,
                                    TCreature(msg.Sender).WAbil.MaxHP,
                                    msg.wparam);
                        wl.lParam1 := TCreature(msg.Sender).GetRelFeature (self);
                        wl.lParam2 := TCreature(msg.Sender).CharStatus;
                        wl.lTag1 := msg.lparam3;  //때린놈
                     end;
                     if msg.Ident = RM_STRUCK_MAG then
                        wl.lTag2 := 1     //마법으로 맞는 사운드 효과
                     else wl.lTag2 := 0;
                     SendSocket (@Def, EncodeBuffer (@wl, sizeof(TMessageBodyWL)));
                  end;
               end;

            RM_DEATH:
               begin
                  if msg.lparam3 = 1 then
                     Def := MakeDefaultMsg (SM_NOWDEATH, integer(msg.sender), msg.lparam1{x}, msg.lparam2{y}, msg.wparam{Dir})
                  else Def := MakeDefaultMsg (SM_DEATH, integer(msg.sender), msg.lparam1{x}, msg.lparam2{y}, msg.wparam{Dir});
                  cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                  cdesc.Status := TCreature(msg.sender).CharStatus;
                  SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
               end;
            RM_SKELETON:
               begin
                  Def := MakeDefaultMsg (SM_SKELETON, integer(msg.sender), msg.lparam1{x}, msg.lparam2{y}, msg.wparam{Dir});
                  cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                  cdesc.Status := TCreature(msg.sender).CharStatus;
                  SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
               end;
            RM_ALIVE:
               begin
                  Def := MakeDefaultMsg (SM_ALIVE, integer(msg.sender), msg.lparam1{x}, msg.lparam2{y}, msg.wparam{Dir});
                  cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                  cdesc.Status := TCreature(msg.sender).CharStatus;
                  SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
               end;
            RM_CHANGEFACE:
               begin
                  //msg.lparam1 변신전
                  //msg.lparam2 변신후
                  if (msg.lparam1 <> 0) and (msg.lparam2 <> 0) then begin
                     Def := MakeDefaultMsg (SM_CHANGEFACE, msg.lparam1, Loword(msg.lparam2), Hiword(msg.lparam2), 0);
                     cdesc.Feature := TCreature(msg.lparam2).GetRelFeature (self);
                     cdesc.Status := TCreature(msg.lparam2).CharStatus;
                     SendSocket (@Def, EncodeBuffer (@cdesc, sizeof(TCharDesc)));
                  end;
               end;

            RM_RECONNECT:
               begin
                  SoftClosed := TRUE;  //재접을 위해서 접속종료함.
                  SendDefMessage (SM_RECONNECT, 0, 0, 0, 0, msg.Description);
               end;

            RM_SPACEMOVE_SHOW,
            RM_SPACEMOVE_SHOW2:
               begin
                  //msg.wParam : 방향
                  if msg.Ident = RM_SPACEMOVE_SHOW then
                     Def := MakeDefaultMsg (SM_SPACEMOVE_SHOW, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light))
                  else
                     Def := MakeDefaultMsg (SM_SPACEMOVE_SHOW2, integer(msg.sender), msg.lParam1{x}, msg.lParam2{y}, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                  cdesc.Feature := TCreature(msg.sender).GetRelFeature (self);
                  cdesc.Status := TCreature(msg.sender).CharStatus;
                  str := EncodeBuffer (@cdesc, sizeof(TCharDesc));
                  n := GetThisCharColor (TCreature(msg.Sender));
                  if msg.Description <> '' then str := str + EncodeString (msg.Description + '/' + IntToStr(n)); //캐릭 이름 + 이름색깔
                  SendSocket (@Def, str);
               end;

            RM_SPACEMOVE_HIDE,
            RM_SPACEMOVE_HIDE2:
               begin
                  if msg.Ident = RM_SPACEMOVE_HIDE then
                     Def := MakeDefaultMsg (SM_SPACEMOVE_HIDE, integer(msg.sender), 0, 0, 0)
                  else
                     Def := MakeDefaultMsg (SM_SPACEMOVE_HIDE2, integer(msg.sender), 0, 0, 0);
                  SendSocket (@Def, '');
               end;

            RM_DISAPPEAR:
               begin
                  Def := MakeDefaultMsg (SM_DISAPPEAR, integer(msg.sender), 0, 0, 0);
                  SendSocket (@Def, '');
               end;

            RM_DIGUP:
               begin
                  Def := MakeDefaultMsg (SM_DIGUP, integer(msg.sender), msg.lparam1, msg.lparam2, MakeWord(msg.wParam{dir}, TCreature(msg.sender).Light));
                  wl.lParam1 := TCreature(msg.Sender).GetRelFeature (self);
                  wl.lParam2 := TCreature(msg.Sender).CharStatus;
                  wl.lTag1 := msg.lparam3;  //이벤트
                  wl.lTag1 := 0;
                  str := EncodeBuffer (@wl, sizeof(TMessageBodyWL));
                  SendSocket (@Def, str);
               end;
            RM_DIGDOWN:
               begin
                  Def := MakeDefaultMsg (SM_DIGDOWN, integer(msg.sender), msg.lparam1, msg.lparam2, 0);
                  SendSocket (@Def, '');
               end;
            RM_SHOWEVENT:
               begin
                  smsg.Ident := Hiword(msg.lParam2); //EventParam
                  smsg.msg := 0;
                  Def := MakeDefaultMsg (SM_SHOWEVENT, integer(msg.lparam1), msg.wParam, Loword(msg.lParam2), msg.lParam3);
                  str := EncodeBuffer (@smsg, sizeof(TShortMessage));
                  SendSocket (@Def, str);
               end;
            RM_HIDEEVENT:
               begin
                  SendDefMessage (SM_HIDEEVENT, integer(msg.lparam1), msg.wParam, msg.lParam2, msg.lParam3, '');
               end;

            RM_FLYAXE:
               begin
                  if msg.lparam3 <> 0 then begin
                     mbw.Param1 := TCreature(msg.lparam3).CX;
                     mbw.Param2 := TCreature(msg.lparam3).CY;
                     mbw.Tag1 := Loword (msg.lparam3);
                     mbw.Tag2 := Hiword (msg.lparam3);
                     Def := MakeDefaultMsg (SM_FLYAXE, integer(msg.sender), msg.lparam1, msg.lparam2, msg.wParam{Dir});
                     str := EncodeBuffer (@mbw, sizeof(TMessageBodyW));
                     SendSocket (@Def, str);
                  end;
               end;

            RM_LIGHTING:
               begin
                  if msg.lparam3 <> 0 then begin
                     wl.lParam1 := TCreature(msg.lparam3).CX;
                     wl.lParam2 := TCreature(msg.lparam3).CY;
                  end;
                  wl.lTag1 := msg.lparam3;
                  wl.lTag2 := msg.wparam; //마법 번호
                  Def := MakeDefaultMsg (SM_LIGHTING,
                                             integer(msg.Sender),
                                             msg.lparam1,
                                             msg.lparam2,
                                             TCreature(msg.Sender).Dir);
                  str := EncodeBuffer (@wl, sizeof(TMessageBodyWL));
                  SendSocket (@Def, str);
               end;

            RM_NORMALEFFECT:
               begin
                  SendDefMessage (SM_NORMALEFFECT,
                                       integer (msg.Sender),  //recog
                                       msg.lparam1, //xx
                                       msg.lparam2, //yy
                                       msg.lparam3, //효과 종류
                                       '');
               end;

            RM_OPENHEALTH:
               begin
                  SendDefMessage (SM_OPENHEALTH,
                                       integer(msg.Sender),
                                       TCreature(msg.Sender).WAbil.HP,
                                       TCreature(msg.Sender).WAbil.MaxHP,
                                       0, '');
               end;

            RM_CLOSEHEALTH:
               begin
                  SendDefMessage (SM_CLOSEHEALTH, integer(msg.Sender), 0, 0, 0, '');
               end;
            RM_INSTANCEHEALGUAGE:
               begin
                  SendDefMessage (SM_INSTANCEHEALGUAGE, integer(msg.Sender),
                                       TCreature(msg.Sender).WAbil.HP,
                                       TCreature(msg.Sender).WAbil.MaxHP,
                                       0, '');
               end;

            RM_BREAKWEAPON:
               begin
                  SendDefMessage (SM_BREAKWEAPON, integer(msg.Sender), 0, 0, 0, '');
               end;
            // 2003/03/04 탐기
            RM_GROUPPOS:    // gadget
               begin
                  Def := MakeDefaultMsg(SM_GROUPPOS, integer(msg.sender),
                                          msg.lParam1 {x},
                                          msg.lParam2 {y},
                                          msg.lParam3 );
                  SendSocket(@Def, '');
               end;
            RM_CHANGENAMECOLOR:
               begin
                  //색변경..
                  //나와의 관계에 따라서 색이 다르게 보인다.
                  n := GetThisCharColor (TCreature(msg.Sender));
                  SendDefMessage (SM_CHANGENAMECOLOR, integer(msg.Sender), n, 0, 0, '');
               end;
            RM_USERNAME:  //서버에서 가제적으로 이름을 보내려고 할때
               begin
                  Def := MakeDefaultMsg (SM_USERNAME, integer(msg.Sender), GetThisCharColor (TCreature(msg.Sender)), 0, 0);
                  SendSocket (@Def, EncodeString(msg.Description));
               end;

            RM_WINEXP:
               begin
                  Def := MakeDefaultMsg (SM_WINEXP, Abil.Exp, msg.lparam1{얻은경험치}, 0, 0);
                  SendSocket (@Def, '');
               end;

            RM_LEVELUP:
               begin
                  Def := MakeDefaultMsg (SM_LEVELUP, Abil.Exp, Abil.Level, 0, 0);
                  SendSocket (@Def, '');
                  Def := MakeDefaultMsg (SM_ABILITY, Gold, Job, Gold, 0);
                  SendSocket (@Def, EncodeBuffer (@WAbil, sizeof(TAbility)));
                  SendDefMessage (SM_SUBABILITY, MakeLong(MakeWord(AntiMagic,0), 0), MakeWord(AccuracyPoint, SpeedPoint), MakeWord(AntiPoison, PoisonRecover), MakeWord(HealthRecover, SpellRecover), '');
               end;

            RM_HEAR,
            RM_CRY,
            RM_WHISPER,
            RM_SYSMESSAGE,
            RM_SYSMESSAGE2,
            RM_SYSMSG_BLUE,
            RM_GROUPMESSAGE,
            RM_GUILDMESSAGE,
            RM_MERCHANTSAY:
               begin
                  case msg.Ident of
                     RM_HEAR:       Def := MakeDefaultMsg (SM_HEAR, integer(msg.sender), MakeWord(0, 255), 0, 1);
                     RM_CRY:        Def := MakeDefaultMsg (SM_HEAR, integer(msg.sender), MakeWord(0, 151), 0, 1);
                     RM_WHISPER:    Def := MakeDefaultMsg (SM_WHISPER, integer(msg.sender), MakeWord(252, 255), 0, 1);
                     RM_SYSMESSAGE: Def := MakeDefaultMsg (SM_SYSMESSAGE, integer(msg.sender), MakeWord(255, 56), 0, 1);
                     RM_SYSMESSAGE2:  Def := MakeDefaultMsg (SM_SYSMESSAGE, integer(msg.sender), MakeWord(219, 255), 0, 1);
                     RM_SYSMSG_BLUE:  Def := MakeDefaultMsg (SM_SYSMESSAGE, integer(msg.sender), MakeWord(255, 252), 0, 1);
                     RM_GROUPMESSAGE: Def := MakeDefaultMsg (SM_SYSMESSAGE, integer(msg.sender), MakeWord(196, 255), 0, 1);
                     RM_GUILDMESSAGE: Def := MakeDefaultMsg (SM_GUILDMESSAGE, integer(msg.sender), MakeWord(212, 255), 0, 1);
                     RM_MERCHANTSAY:  Def := MakeDefaultMsg (SM_MERCHANTSAY, integer(msg.sender), 0, 0, 1);
                  end;
                  str := EncodeString (msg.description);
                  SendSocket (@Def, str);
               end;
            RM_MERCHANTDLGCLOSE:
               begin
                  SendDefMessage (SM_MERCHANTDLGCLOSE, msg.lparam1, 0, 0, 0, '');
               end;
            RM_SENDGOODSLIST:
               begin
                  SendDefMessage (SM_SENDGOODSLIST, msg.lparam1{merchant id}, msg.lparam2{count}, 0, 0, msg.Description);
               end;
            RM_SENDUSERSELL:
               begin
                  SendDefMessage (SM_SENDUSERSELL, msg.lparam1{merchant id}, msg.lparam2{count}, 0, 0, '');
               end;
            RM_SENDUSERREPAIR,
            RM_SENDUSERSPECIALREPAIR:
               begin
                  SendDefMessage (SM_SENDUSERREPAIR, msg.lparam1{merchant id}, msg.lparam2{count}, 0, 0, '');
               end;
            RM_SENDUSERSTORAGEITEM:
               begin
                  SendDefMessage (SM_SENDUSERSTORAGEITEM, msg.lparam1{merchant id}, msg.lparam2{count}, 0, 0, '');
               end;
            RM_SENDUSERSTORAGEITEMLIST:
               begin
                  ServerSendStorageItemList (msg.lparam1);
               end;
            RM_SENDUSERMAKEDRUGITEMLIST:
               begin
                  SendDefMessage (SM_SENDUSERMAKEDRUGITEMLIST, msg.lparam1{merchant id}, msg.lparam2{count}, 0, 0, msg.Description);
               end;
            RM_SENDBUYPRICE:
               begin
                  SendDefMessage (SM_SENDBUYPRICE, msg.lparam1{buy price}, 0, 0, 0, '');
               end;
            RM_USERSELLITEM_OK:
                  SendDefMessage (SM_USERSELLITEM_OK, msg.lparam1{chg gold}, 0, 0, 0, '');
            RM_USERSELLITEM_FAIL:
                  SendDefMessage (SM_USERSELLITEM_FAIL, msg.lparam1, 0, 0, 0, '');
            RM_BUYITEM_SUCCESS:
                  SendDefMessage (SM_BUYITEM_SUCCESS, msg.lparam1{chg gold}, Loword(msg.lparam2), Hiword(msg.lparam2), 0, '');
            RM_BUYITEM_FAIL:
                  SendDefMessage (SM_BUYITEM_FAIL, msg.lparam1{error code}, 0, 0, 0, '');
            RM_MAKEDRUG_SUCCESS:
                  SendDefMessage (SM_MAKEDRUG_SUCCESS, msg.lparam1{chg gold}, 0, 0, 0, '');
            RM_MAKEDRUG_FAIL:
                  SendDefMessage (SM_MAKEDRUG_FAIL, msg.lparam1{chg gold}, 0, 0, 0, '');
            RM_SENDDETAILGOODSLIST:
                  SendDefMessage (SM_SENDDETAILGOODSLIST, msg.lparam1{merchant id}, msg.lparam2{count}, msg.lparam3{menuindex}, 0, msg.Description);

            RM_USERREPAIRITEM_OK:
                  SendDefMessage (SM_USERREPAIRITEM_OK, msg.lparam1{cost}, msg.lparam2{dura}, msg.lparam3{maxdura}, 0, '');
            RM_USERREPAIRITEM_FAIL:
                  SendDefMessage (SM_USERREPAIRITEM_FAIL, msg.lparam1{cost}, 0, 0, 0, '');
            RM_SENDREPAIRCOST:
                  SendDefMessage (SM_SENDREPAIRCOST, msg.lparam1{cost}, 0, 0, 0, '');

            RM_ITEMSHOW:
               begin
                  SendDefMessage (SM_ITEMSHOW, msg.lparam1{pointer}, msg.lparam2{x}, msg.lparam3{y}, msg.wParam{looks}, msg.Description);
               end;
            RM_ITEMHIDE:
               begin
                  SendDefMessage (SM_ITEMHIDE, msg.lparam1{pointer}, msg.lparam2{x}, msg.lparam3{y}, 0, '');
               end;
            RM_DELITEMS:
               begin
                  if msg.lparam1 <> 0 then begin
                     SendDelItems (TStringList (msg.lparam1));
                     TStringList (msg.lparam1).Free; //여기서 Free해야 함...
                  end;
               end;
//            //일뗀ADD
//            RM_CANCLOSE_OK:   SendDefMessage (SM_CANCLOSE_OK, msg.lparam1, 0, 0, 0, msg.Description);
//            RM_CANCLOSE_FAIL: SendDefMessage (SM_CANCLOSE_FAIL, msg.lparam1, 0, 0, 0, msg.Description);

            RM_OPENDOOR_OK:
               begin
                  SendDefMessage (SM_OPENDOOR_OK, 0, msg.lparam1, msg.lparam2, 0, '');
               end;
            RM_CLOSEDOOR:
               begin
                  SendDefMessage (SM_CLOSEDOOR, 0, msg.lparam1, msg.lparam2, 0, '');
               end;

            RM_SENDUSEITEMS:
               begin
                  SendUseItems;
               end;

            RM_SENDMYMAGIC:
               begin
                  SendMyMagics;
               end;

            RM_WEIGHTCHANGED:
               begin
                  SendDefMessage (SM_WEIGHTCHANGED,
                                  WAbil.Weight,
                                  WAbil.WearWeight,
                                  WAbil.HandWeight,
                                  (((WAbil.Weight + WAbil.WearWeight + WAbil.HandWeight) xor $3A5F) xor $1F35) xor $aa21, '');
               end;
            RM_GOLDCHANGED:
               begin
                  SendDefMessage (SM_GOLDCHANGED, Gold, 0, 0, 0, '');
               end;
            RM_FEATURECHANGED:
               begin
                  SendDefMessage (SM_FEATURECHANGED, integer(msg.sender), Loword(msg.lParam1), Hiword(msg.lparam1), 0, '');
               end;
            RM_CHARSTATUSCHANGED:
               begin
                  SendDefMessage (SM_CHARSTATUSCHANGED, integer(msg.sender), Loword(msg.lParam1), Hiword(msg.lparam1), msg.wparam, '');
               end;

            RM_CLEAROBJECTS:
               begin
                  SendDefMessage (SM_CLEAROBJECTS, 0, 0, 0, 0, '');
               end;

            RM_MAGIC_LVEXP:
               begin
                  SendDefMessage (SM_MAGIC_LVEXP, msg.lparam1, msg.lparam2{lv}, Loword(msg.lparam3), Hiword(msg.lparam3), '');
               end;
            RM_SOUND:
               begin
               end;
            RM_DURACHANGE:
               begin
                  SendDefMessage (SM_DURACHANGE, msg.lparam1, msg.wparam, Loword(msg.lparam2), Hiword(msg.lparam2), '');
               end;
            //RM_ITEMDURACHANGE:
            //   begin
            //      SendDefMessage (SM_ITEMDURACHANGE, msg.lparam1, msg.lparam2{dura}, msg.lparam3{duramax}, 0, '');
            //   end;
            RM_CHANGELIGHT:
               begin
                  SendDefMessage (SM_CHANGELIGHT, integer(msg.Sender), TCreature(msg.sender).Light, 0, 0, '');
               end;
            RM_LAMPCHANGEDURA:
               begin
                  SendDefMessage (SM_LAMPCHANGEDURA, msg.lparam1, 0, 0, 0, '');
               end;

            RM_GROUPCANCEL:
               begin
                  SendDefMessage (SM_GROUPCANCEL, 0, 0, 0, 0, '');
               end;

            RM_CHANGEGUILDNAME:
               begin
                  SendChangeGuildName;
               end;

            RM_BUILDGUILD_OK: SendDefMessage (SM_BUILDGUILD_OK, 0, 0, 0, 0, '');

            RM_BUILDGUILD_FAIL: SendDefMessage (SM_BUILDGUILD_FAIL, msg.lparam1, 0, 0, 0, '');

            RM_DONATE_OK:  SendDefMessage (SM_DONATE_OK, msg.lparam1, 0, 0, 0, '');

            RM_DONATE_FAIL:  SendDefMessage (SM_DONATE_FAIL, msg.lparam1, 0, 0, 0, '');

            RM_MENU_OK: SendDefMessage (SM_MENU_OK, msg.lparam1, 0, 0, 0, msg.Description);

            RM_NEXTTIME_PASSWORD: SendDefMessage (SM_NEXTTIME_PASSWORD, 0, 0, 0, 0, '');

            RM_DOSTARTUPQUEST: DoStartupQuestNow;

            RM_PLAYDICE:
               begin
                  wl.lParam1 := msg.lparam1;
                  wl.lParam2 := msg.lparam2;
                  wl.lTag1 := msg.lparam3;
                  Def := MakeDefaultMsg (SM_PLAYDICE, integer(msg.sender), msg.wparam, 0, 0);

                  SendSocket (@Def, EncodeBuffer (@wl, sizeof(TMessageBodyWL))
                                    + EncodeString (msg.Description));

               end;

            else
               inherited RunMsg (msg);
         end;
      end;

      if EmergencyClose or UserRequestClose or UserSocketClosed then begin

         if not BoChangeServer then //접속종료 되면 떨어지는이벤트 아이템을 떨군다.
            DropEventItems;

         if BoChangeServer then begin
            MapName := ChangeMapName;
            CX := ChangeCX;
            CY := ChangeCY;

            //테스트
            //MakeDefaultMsg (SM_SYSMESSAGE, integer(self), MakeWord(255, 56), 0, 1);
            //SendSocket (@Def, EncodeString ('change server 1'));
         end;
         MakeGhost;

         if UserRequestClose then SendDefMessage (SM_OUTOFCONNECTION, 0, 0, 0, 0, '');

         if not SoftClosed and UserSocketClosed then begin //캐릭터 선택으로 빠진것이 아니면
            //다른 서버에 알린다.
            FrmIDSoc.SendUserClose (UserId, Certification);
         end;

         exit;
      end;

   except
      MainOutMessage ('[Exception] Operate 2 #' + UserName +
                                           ' Ident' + IntToStr(msg.Ident) +
                                           ' Sender' + IntToStr(integer(msg.Sender)) +
                                           ' wP ' + IntToStr(msg.wParam) +
                                           ' lP1 ' + IntToStr(msg.lParam1) +
                                           ' lP2 ' + IntToStr(msg.lParam2) +
                                           ' lP3 ' + IntToStr(msg.lParam3));
   end;

   inherited Run;
end;


{-------------------- 公告通知 ---------------------}

{ SendLoginNotice - 发送登录公告
  功能: 发送登录公告给客户端 }
procedure TUserHuman.SendLoginNotice;
var
   i: integer;
   strlist: TStringList;
   data: string;
begin
   strlist := TStringList.Create;
   NoticeMan.GetNoticList ('Notice', strlist);
   data := '';
   for i:=0 to strlist.Count-1 do begin
      data := data + strlist[i]+' '#27;
   end;
   strlist.Free;
   SendDefMessage (SM_SENDNOTICE, 0, 0, 0, 0, data);
end;

{ RunNotice - 运行公告
  功能: 处理公告相关逻辑 }
procedure TUserHuman.RunNotice;
var
   msg: TMessageInfo;
begin
   if EmergencyClose or UserRequestClose or UserSocketClosed then begin
      if UserRequestClose then SendDefMessage (SM_OUTOFCONNECTION, 0, 0, 0, 0, '');
      MakeGhost;
      //BoGhost := TRUE;
      //GhostTime := GetTickCount;
      exit;
   end;
   try
      //로그인 전에 공지사항을 보낸다.
      if not BoSendNotice then begin
         SendLoginNotice;
         BoSendNotice := TRUE;
      end else begin
         while GetMsg (msg) do begin
            case msg.Ident of
               CM_LOGINNOTICEOK: ServerGetNoticeOk;
            end;
         end;
      end;
   except
      MainOutMessage ('[Exception] TUserHuman.RunNotice');
   end;
end;

{ GetGetNotices - 获取公告
  功能: 获取需要的公告 }
procedure TUserHuman.GetGetNotices;
begin
   if BoGetGetNeedNotice then
      GetGetNotices;
end;

{ ServerGetNoticeOk - 公告确认
  功能: 处理玩家确认公告请求 }
procedure TUserHuman.ServerGetNoticeOk;
begin
   LoginSign := TRUE;
end;


{ GetStartX - 获取起始X坐标
  功能: 获取玩家起始X坐标 }
function  TUserHuman.GetStartX: integer;
begin
   Result := HomeX - 2 + Random(3);
end;

{ GetStartY - 获取起始Y坐标
  功能: 获取玩家起始Y坐标 }
function  TUserHuman.GetStartY: integer;
begin
   Result := HomeY - 2 + Random(3);
end;

{ CheckHomePos - 检查出生点
  功能: 检查并设置玩家的出生点 }
procedure TUserHuman.CheckHomePos;
var
   i: integer;
   flag: Boolean;
begin
   flag := FALSE;
   for i:=0 to StartPoints.Count-1 do begin
      if PEnvir.MapName = StartPoints[i] then begin
         if (Abs(CX - Loword(integer(StartPoints.Objects[i]))) < 50) and
            (Abs(CY - Hiword(integer(StartPoints.Objects[i]))) < 50)
         then begin
            HomeMap := StartPoints[i];
            HomeX := Loword(integer(StartPoints.Objects[i]));
            HomeY := Hiword(integer(StartPoints.Objects[i]));
            flag := TRUE;
         end;
      end;
   end;
   if PKLevel >= 2 then begin  //빨갱이는 빨갱이 마을로
      HomeMap := BADMANHOMEMAP;
      HomeX := BADMANSTARTX;
      HomeY := BADMANSTARTY;
   end;
end;


{-------------------- 客户端消息处理 ---------------------}

{ GetQueryUserName - 查询用户名称
  功能: 查询指定生物的名称 }
procedure TUserHuman.GetQueryUserName (target: TCreature; x, y: integer);
var
   uname: string;
   tagcolor: integer;
begin
   if CretInNearXY (target, x, y) then begin
      tagcolor := GetThisCharColor (target);
      Def := MakeDefaultMsg (SM_USERNAME, Integer(target), tagcolor, 0, 0);
      uname := target.GetUserName;
      SendSocket (@Def, EncodeString (uname));
   end else
      SendDefMessage (SM_GHOST, integer(target), x, y, 0, '');
end;

{ ServerSendAdjustBonus - 发送调整奖励点
  功能: 发送奖励点调整信息给客户端 }
procedure TUserHuman.ServerSendAdjustBonus;
var
   str: string;
   na: TNakedAbility;
begin
   Def := MakeDefaultMsg (SM_ADJUST_BONUS, Integer(BonusPoint), 0, 0, 0);
   str := '';
   na := BonusAbil;
   case Job of
      0: str := EncodeBuffer(@WarriorBonus, sizeof(TNakedAbility)) + '/' +  EncodeBuffer(@CurBonusAbil, sizeof(TNakedAbility)) + '/' + EncodeBuffer(@na, sizeof(TNakedAbility));
      1: str := EncodeBuffer(@WizzardBonus, sizeof(TNakedAbility)) + '/' + EncodeBuffer(@CurBonusAbil, sizeof(TNakedAbility)) + '/' + EncodeBuffer(@na, sizeof(TNakedAbility));
      2: str := EncodeBuffer(@PriestBonus, sizeof(TNakedAbility))  + '/' + EncodeBuffer(@CurBonusAbil, sizeof(TNakedAbility)) + '/' + EncodeBuffer(@na, sizeof(TNakedAbility));
   end;
   SendSocket (@Def, str);
end;

{ ServerGetOpenDoor - 处理开门
  功能: 处理玩家开门请求 }
procedure TUserHuman.ServerGetOpenDoor (dx, dy: integer);
var
   pd: PTDoorInfo;
begin
   if PEnvir = UserCastle.CastlePEnvir then begin
      pd := PEnvir.FindDoor (dx, dy);
      if UserCastle.CoreCastlePDoorCore = pd.pCore then begin  //사북성의 내성문
         if RaceServer = RC_USERHUMAN then
            if not UserCastle.CanEnteranceCoreCastle (CX, CY, TUserHuman(self)) then  //
               exit;  //들어갈 수 없음.
      end;
   end;

   UserEngine.OpenDoor (PEnvir, dx, dy);
end;

{ ServerGetTakeOnItem - 处理穿戴物品
  功能: 处理玩家穿戴物品请求 }
procedure TUserHuman.ServerGetTakeOnItem (where: byte; svindex: integer; itmname: string);
var
   i, bagindex, ecount: integer;
   ps, ps2: PTStdItem;
   std: TStdItem;
   targpu, pu: PTUserItem;
label
   finish;
begin
   ps := nil;
   targpu := nil;
   bagindex := -1;
   for i:=0 to Itemlist.Count-1 do begin
      if PTUserItem(ItemList[i]).MakeIndex = svindex then begin
         ps := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
         if ps <> nil then
            if CompareText (ps.Name, itmname) = 0 then begin
               bagindex := i;
               targpu := PTUserItem(ItemList[i]);
               break;
            end;
      end;
   end;

   ecount := 0;
   if (ps <> nil) and (targpu <> nil) then begin
      if IsTakeOnAvailable (where, ps) then begin //착용할 수 있는 바른 아이템인가?
         std := ps^;
         ItemMan.GetUpgradeStdItem (targpu^, std); //무기의 업그래이드된 능력치를 얻어온다.
         if CanTakeOn (where, @std) then begin //내가 능력이 되는가?

            pu := nil;
            if UseItems[where].Index > 0 then begin //이미 착용하고 있음.
               //벗지 못하는 아이템이 아닌경우 (미지수로 벗을 수 있음)
               ps2 := UserEngine.GetStdItem (UseItems[where].Index);
               // 2003/03/15 아이템 인벤토리 확장
               if ps2.StdMode in [15,19,20,21,22,23,24,26,52,53,54] then begin
                  if not BoNextTimeFreeCurseItem and (UseItems[where].Desc[7] <> 0) then begin
                     //벗을 수 없는 아이템
                     SysMsg ('퀭돨嶠포轟랬菌뇜', 0);
                     ecount := -4;
                     goto finish;
                  end;
               end;
               if not BoNextTimeFreeCurseItem and (ps2.ItemDesc and IDC_UNABLETAKEOFF <> 0) then begin
                  //벗을 수 없는 아이템
                  SysMsg ('퀭돨嶠포轟랬菌뇜', 0);
                  ecount := -4;
                  goto finish;
               end;
               //절대로 벗지 못하는 아이템
               if ps2.ItemDesc and IDC_NEVERTAKEOFF <> 0 then begin
                  SysMsg ('퀭돨嶠포轟랬菌뇜', 0);
                  ecount := -4;
                  goto finish;
               end;
               new (pu);
               pu^ := UseItems[where];
            end;

            //미지의 속성을 가지고 있는 아이템인 경우 한번 착용하면 풀림
            // 2003/03/15 아이템 인벤토리 확장
            if ps.StdMode in [15,19,20,21,22,23,24,26,52,53,54] then begin
               if targpu.Desc[8] <> 0 then
                  targpu.Desc[8] := 0; //미지속성 풀림;
            end;

            UseItems[where] := targpu^;  //
            DelItemIndex (bagindex);  //DelItem (svindex, itmname);
            if pu <> nil then begin
               AddItem (pu); //가방에 추가되는 아이템 보낸다.(있으면)
               SendAddItem (pu^);
            end;
            RecalcAbilitys;     //능력치 재조정 한다.
            SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
            SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
            SendDefMessage (SM_TAKEON_OK, Feature, 0, 0, 0, ''); //착용 성공 보낸다. 변경된 모습을 보낸다.
            FeatureChanged;
            ecount := 1;
         end else
            ecount := -1;
      end else
         ecount := -2;
   end;
   finish:
   if ecount <= 0 then
      SendDefMessage (SM_TAKEON_FAIL, ecount, 0, 0, 0, '');
end;

{ ServerGetTakeOffItem - 处理脱下物品
  功能: 处理玩家脱下物品请求 }
procedure TUserHuman.ServerGetTakeOffItem (where: byte; svindex: integer; itmname: string);
var
   ecount: integer;
   ps: PTStdItem;
   pu: PTUserItem;
label
   finish;
begin
   ecount := 0;
   // 2003/03/15 아이템 인벤토리 확장
   if (not BoDealing) and (where in [0..12]) then begin  //교환중에는 아이템을 못 벗는다. 8->12
      if UseItems[where].Index > 0 then begin //착용하고 있어야 벗을 수 있음.
         if UseItems[where].MakeIndex = svindex then begin
            ps := UserEngine.GetStdItem (UseItems[where].Index);

            //벗지 못하는 아이템이 아닌경우
            // 2003/03/15 아이템 인벤토리 확장
            if ps.StdMode in [15,19,20,21,22,23,24,26,52,53,54] then begin
               if not BoNextTimeFreeCurseItem and (UseItems[where].Desc[7] <> 0) then begin
                  //벗을 수 없는 아이템
                  SysMsg ('퀭돨嶠포轟랬菌뇜', 0);
                  ecount := -4;
                  goto finish;
               end;
            end;
            if not BoNextTimeFreeCurseItem and (ps.ItemDesc and IDC_UNABLETAKEOFF <> 0) then begin
               //벗을 수 없는 아이템
               SysMsg ('퀭돨嶠포轟랬菌뇜', 0);
               ecount := -4;
               goto finish;
            end;
            //절대로 벗지 못하는 아이템
            if ps.ItemDesc and IDC_NEVERTAKEOFF <> 0 then begin
               SysMsg ('퀭돨嶠포轟랬菌뇜', 0);
               ecount := -4;
               goto finish;
            end;

            if CompareText (ps.Name, itmname) = 0 then begin
               new (pu);
               pu^ := UseItems[where];
               if AddItem (pu) then begin //가방에 추가되는 아이템 보낸다.
                  UseItems[where].Index := 0; //지움..
                  SendDefMessage (SM_TAKEOFF_OK, Feature, 0, 0, 0, '');
                  SendAddItem (pu^);
                  RecalcAbilitys;     //능력치 재조정 한다.
                  SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
                  SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
                  FeatureChanged;
               end else begin
                  Dispose (pu);
                  ecount := -3;
               end;
            end;
         end;
      end else
         ecount := -2;
   end else
      ecount := -1;

   finish:
   if ecount <= 0 then
      SendDefMessage (SM_TAKEOFF_FAIL, ecount, 0, 0, 0, '');
end;

{ ServerGetEatItem - 处理使用物品
  功能: 处理玩家使用物品请求 }
procedure TUserHuman.ServerGetEatItem (svindex: integer; itmname: string);
   function UnbindPotionUnit (itmname: string; count: integer): Boolean;
   var
      i: integer;
      hum: TUserHuman;
      pui: PTUserItem;
   begin
      Result := FALSE;
      for i:=0 to count-1 do begin
         new (pui);
         if UserEngine.CopyToUserItemFromName (itmname, pui^) then begin
            ItemList.Add (pui);
            if RaceServer = RC_USERHUMAN then begin
               hum := TUserHuman (self);
               hum.SendAddItem (pui^);
            end;
         end else
            Dispose (pui);
      end;
      Result := TRUE;
   end;
var
   i: integer;
   flag: Boolean;
   ps: PTStdItem;
   ui: TUserItem;
begin
   flag := FALSE;
   if not Death then begin
      for i:=0 to Itemlist.Count-1 do begin
         if PTUserItem(ItemList[i]).MakeIndex = svindex then begin
            //if UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index) = itmname then begin
               ps := UserEngine.GetStdItem (PTUserItem(Itemlist[i]).Index);
               ui := PTUserItem(Itemlist[i])^;
               case ps.StdMode of
                  0,1,2,3: //시약, 고기류, 음식, 스크롤
                     if EatItem (ps^, PTUserItem(ItemList[i])) then begin
                        Dispose (PTUserItem(ItemList[i]));
                        ItemList.Delete (i);
                        flag := TRUE;
                     end;
                  4: //책
                     if ReadBook (ps^) then begin
                        Dispose (PTUserItem(ItemList[i]));
                        ItemList.Delete (i);
                        flag := TRUE;
                        //어검술
                        if PLongHitSkill <> nil then
                           if not BoAllowLongHit then begin
                              SetAllowLongHit (TRUE);
                              SendSocket (nil, '+LNG');  //원거리 공격을 하게 한다.
                           end;
                        //반월검법
                        if PWideHitSkill <> nil then
                           if not BoAllowWideHit then begin
                              SetAllowWideHit (TRUE);
                              SendSocket (nil, '+WID');
                           end;
                        // 2003/03/15 신규무공
                        // 광풍참
                        if PCrossHitSkill <> nil then
                           if not BoAllowCrossHit then begin
                              SetAllowCrossHit (TRUE);
                              SendSocket (nil, '+CRS');
                           end;
                     end;
                  31:
                     begin
                        if ItemList.Count + 6 - 1 <= MAXBAGITEM then begin
                           Dispose (PTUserItem(ItemList[i]));
                           ItemList.Delete (i);
                           UnbindPotionUnit (GetUnbindItemName (ps.Shape), 6);
                           flag := TRUE;
                        end;
                     end;
               end;
               break;
            //end;
         end;
      end;
   end;
   if flag then begin
      WeightChanged;
      SendDefMessage (SM_EAT_OK, 0, 0, 0, 0, '');
      //물건을 사용하여 없어짐
      AddUserLog ('11'#9 + //사용_ +
                  MapName + ''#9 +
                  IntToStr(CX) + ''#9 +
                  IntToStr(CY) + ''#9 +
                  UserName + ''#9 +
                  UserEngine.GetStdItemName (ui.Index) + ''#9 +
                  IntToStr(ui.MakeIndex) + ''#9 +
                  IntToStr(BoolToInt(RaceServer = RC_USERHUMAN)) + ''#9 +
                  '0');
   end else SendDefMessage (SM_EAT_FAIL, 0, 0, 0, 0, '');
end;

{ ServerGetButch - 处理屠孰
  功能: 处理玩家屠孰动物请求 }
procedure TUserHuman.ServerGetButch (animal: TCreature; x, y, ndir: integer);
var
   n, m: integer;
begin
   if (abs(x-CX) <= 2) and (abs(y-CY) <= 2) then begin  //바로 옆칸만 썰 수 있음
      if PEnvir.IsValidCreature (x, y, 2, animal) then begin  //
         if (animal.Death) and (not animal.BoSkeleton) and (animal.BoAnimal) then begin
            //자신의 도축 기술에 따라서 도축 포인트가 다르게 적용된다.
            //기술이 없는 경우, 5-20 사이이며, 고기의 질도 10-20씩 떨어진다.
            n := 5 + Random(16);
            m := 100 + Random(201);
            animal.BodyLeathery := animal.BodyLeathery - n;
            animal.MeatQuality := animal.MeatQuality - m;   //칼질을 할 수록 고기질은 조금씩 떨어짐
            if animal.MeatQuality < 0 then animal.MeatQuality := 0;
            if animal.BodyLeathery <= 0 then begin
               if (animal.RaceServer >= RC_ANIMAL) and (animal.RaceServer < RC_MONSTER) then begin  //사슴같이 고기를주는 것만, 해골로 변함
                  animal.BoSkeleton := TRUE;
                  animal.ApplyMeatQuality;
                  animal.SendRefMsg (RM_SKELETON, animal.Dir, animal.CX, animal.CY, 0, '')
               end;
               if not TakeCretBagItems (animal) then
                  SysMsg ('청唐삿돤훨부땜鮫。', 0);
               animal.BodyLeathery := 50; //메세지가 연속으로 나오는 것을 막음.
            end;
            DeathTime := GetTickCount;  //도축하고 있는도중에 고기는 사라지지 않음.
         end;
      end;
      Dir := ndir;
   end;
   SendRefMsg (RM_BUTCH, Dir, CX, CY, 0, '');
end;

{ ServerGetMagicKeyChange - 处理魔法快捷键变更
  功能: 处理玩家修改魔法快捷键请求 }
procedure TUserHuman.ServerGetMagicKeyChange (magid, key: integer);
var
   i: integer;
begin
   for i:=0 to MagicList.Count-1 do begin
      if PTUserMagic(MagicList[i]).pDef.MagicId = magid then begin
         PTUserMagic(MagicList[i]).Key := char(key);
         break;
      end;
   end;
end;

{ ServerGetClickNpc - 处理点击NPC
  功能: 处理玩家点击NPC请求 }
procedure TUserHuman.ServerGetClickNpc (clickid: integer);
var
   npc: TCreature;
begin
   if BoDealing then exit;  //교환중에는 npc를 클릭할 수 없다.

   //NPC등, 상인들 검사
   npc := UserEngine.GetMerchant (clickid);
   if npc = nil then npc := UserEngine.GetNpc (clickid);
   if npc <> nil then begin
      if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
         TNormNpc(npc).UserCall (self);
      end;
   end;
end;

{ ServerGetMerchantDlgSelect - 处理商人对话框选择
  功能: 处理玩家在商人对话框中的选择 }
procedure TUserHuman.ServerGetMerchantDlgSelect (npcid: integer; clickstr: string);
var
   npc: TNormNpc;
begin
   npc := TNormNpc (UserEngine.GetMerchant (npcid));
   if npc = nil then npc := TNormNpc (UserEngine.GetNpc (npcid));
   if npc <> nil then begin
      //npc.BoInvisible => 맵 퀘스트인 경우
      if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) or (npc.BoInvisible) then begin


         TNormNpc(npc).UserSelect (self, clickstr);

      end;
   end;
end;

{ ServerGetMerchantQuerySellPrice - 查询出售价格
  功能: 查询物品的出售价格 }
procedure TUserHuman.ServerGetMerchantQuerySellPrice (npcid, itemindex: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
   pu: PTuserItem;
begin
   pu := nil;
   //내 가방의 아이템에서 itemindex의 아이템을 찾는다.
   for i:=0 to ItemList.Count-1 do begin
      if PTUserItem(ItemList[i]).MakeIndex = itemindex then begin
         if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), itemname) = 0 then begin
            pu := PTUserItem(ItemList[i]);
            break;
         end;
      end;
   end;

   if pu <> nil then begin
      npc := UserEngine.GetMerchant (npcid);
      if npc <> nil then begin
         if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
            TMerchant(npc).QueryPrice (self, pu^);
         end;
      end;
   end;
end;

{ ServerGetMerchantQueryRepairPrice - 查询修理价格
  功能: 查询物品的修理价格 }
procedure TUserHuman.ServerGetMerchantQueryRepairPrice (npcid, itemindex: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
   pu: PTuserItem;
begin
   pu := nil;
   //내 가방의 아이템에서 itemindex의 아이템을 찾는다.
   for i:=0 to ItemList.Count-1 do begin
      if PTUserItem(ItemList[i]).MakeIndex = itemindex then begin
         if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), itemname) = 0 then begin
            pu := PTUserItem(ItemList[i]);
            break;
         end;
      end;
   end;

   if pu <> nil then begin
      npc := UserEngine.GetMerchant (npcid);
      if npc <> nil then begin
         if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
            TMerchant(npc).QueryRepairCost (self, pu^);
         end;
      end;
   end;
end;

{ ServerGetUserSellItem - 处理出售物品
  功能: 处理玩家出售物品请求 }
procedure TUserHuman.ServerGetUserSellItem (npcid, itemindex: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
   pu: PTuserItem;
   pstd: PTStdItem;
begin
   pu := nil;
   //내 가방의 아이템에서 itemindex의 아이템을 찾는다.
   for i:=0 to ItemList.Count-1 do begin
      if PTUserItem(ItemList[i]).MakeIndex = itemindex then begin
         if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), itemname) = 0 then begin
            pu := PTUserItem(ItemList[i]);
            npc := UserEngine.GetMerchant (npcid);
            pstd := UserEngine.GetStdItem (pu.Index);
            if (npc <> nil) and (pu <> nil) and (pstd <> nil) then begin
               if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin

                  if pstd.StdMode <> TAIWANEVENTITEM then begin //대만 이벤트용 아이템은 팔 수 없다

                     if TMerchant(npc).UserSellItem (self, pu^) then begin
                        //판매한 아이템을 없앤다.
                        Dispose (PTUserItem(ItemList[i]));
                        ItemList.Delete (i);
                        WeightChanged;
                     end else
                        SendMsg (self, RM_USERSELLITEM_FAIL, 0, 0, 0, 0, '');
                  end;
               end;
            end;
            break;
         end;
      end;
   end;
end;

{ ServerGetUserRepairItem - 处理修理物品
  功能: 处理玩家修理物品请求 }
procedure TUserHuman.ServerGetUserRepairItem (npcid, itemindex: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
   pu: PTuserItem;
begin
   pu := nil;
   //내 가방의 아이템에서 itemindex의 아이템을 찾는다.
   for i:=0 to ItemList.Count-1 do begin
      if PTUserItem(ItemList[i]).MakeIndex = itemindex then begin
         if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), itemname) = 0 then begin
            pu := PTUserItem(ItemList[i]);
            npc := UserEngine.GetMerchant (npcid);
            if (npc <> nil) and (pu <> nil) then begin
               if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
                  //수리한다.
                  if TMerchant(npc).UserRepairItem (self, pu) then begin
                     ;
                  end;
               end;
            end;
            break;
         end;
      end;
   end;
end;

{ ServerSendStorageItemList - 发送仓库物品列表
  功能: 发送仓库物品列表给客户端 }
procedure TUserHuman.ServerSendStorageItemList (npcid: integer);
var
   i: integer;
   data: string;
   pu: PTUserItem;
   ps: PTStdItem;
   std: TStdItem;
   citem: TClientItem;
begin
   data := '';
   for i:=0 to SaveItems.Count-1 do begin
      pu := PTUserItem (SaveItems[i]);
      ps := UserEngine.GetStdItem (pu.Index);
      if ps <> nil then begin
         std := ps^;
         ItemMan.GetUpgradeStdItem (pu^, std);
         citem.S := std;
         citem.Dura := pu.Dura;
         citem.DuraMax := pu.DuraMax;
         citem.MakeIndex := pu.MakeIndex;
         data := data + EncodeBuffer (@citem, sizeof(TClientItem)) + '/';
      end;
   end;
   Def := MakeDefaultMsg (SM_SAVEITEMLIST, npcid, 0, 0, SaveItems.Count{수량});
   SendSocket (@Def, data);
end;

{ ServerGetUserStorageItem - 处理存储物品
  功能: 处理玩家存储物品到仓库请求 }
procedure TUserHuman.ServerGetUserStorageItem (npcid, itemindex: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
   pu: PTuserItem;
   pstd: PTStdItem;
   flag: Boolean;
begin
   pu := nil;
   //내 가방의 아이템에서 itemindex의 아이템을 찾는다.
   flag := FALSE;
   if pos(' ', itemname) >= 0 then
      GetValidStr3 (itemname, itemname, [' ']);
   if ApprovalMode <> 1 then begin //체험모드는 물건을 못 맡긴다. 맡기다
      for i:=0 to ItemList.Count-1 do begin
         if PTUserItem(ItemList[i]).MakeIndex = itemindex then begin
            if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), itemname) = 0 then begin
               pu := PTUserItem(ItemList[i]);
               npc := UserEngine.GetMerchant (npcid);
               pstd := UserEngine.GetStdItem (pu.Index);
               if (npc <> nil) and (pu <> nil) and (pstd <> nil) then begin
                  if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin

                     if pstd.StdMode <> TAIWANEVENTITEM then begin  //대만 이벤트용 아이템, 맡길수 없다

                        //보관한다.
                        if SaveItems.Count < MAXSAVELIMIT-1 then begin
                           SaveItems.Add (pu);  //보관
                           ItemList.Delete (i);
                           WeightChanged;
                           SendDefMessage (SM_STORAGE_OK, 0, 0, 0, 0, '');
                           //로그남김
                           AddUserLog ('1'#9 + //보관_ +
                                       MapName + ''#9 +
                                       IntToStr(CX) + ''#9 +
                                       IntToStr(CY) + ''#9 +
                                       UserName + ''#9 +
                                       UserEngine.GetStdItemName (pu.Index) + ''#9 +
                                       IntToStr(pu.MakeIndex) + ''#9 +
                                       '1'#9 +
                                       '0');
                        end else //더이상 보관 못함
                           SendDefMessage (SM_STORAGE_FULL, 0, 0, 0, 0, '');
                        flag := TRUE;
                     end;
                  end;
               end;
               break;
            end;
         end;
      end;
   end else
      SysMsg ('竟駱친駕櫓퀭꼇콘賈痰꾑욋륩蛟。', 0);
   if not flag then
      SendDefMessage (SM_STORAGE_FAIL, 0, 0, 0, 0, '');
end;

{ ServerGetTakeBackStorageItem - 处理取回仓库物品
  功能: 处理玩家从仓库取回物品请求 }
procedure TUserHuman.ServerGetTakeBackStorageItem (npcid, itemserverindex: integer; iname: string);
var
   I: INTEGER;
   flag: Boolean;
   pu: PTUserItem;
   npc: TCreature;
begin
   flag := FALSE;
   if ApprovalMode <> 1 then begin //체험모드는 물건을 못 찾는다.
      for i:=0 to SaveItems.Count-1 do begin
         if PTUserItem(SaveItems[i]).MakeIndex = itemserverindex then begin
            if CompareText (UserEngine.GetStdItemName (PTUserItem(SaveItems[i]).Index), iname) = 0 then begin
               pu := PTUserItem(SaveItems[i]);
               npc := UserEngine.GetMerchant (npcid);
               if (npc <> nil) and (pu <> nil) then begin
                  if IsAddWeightAvailable (UserEngine.GetStdItemWeight(pu.Index)) then begin
                     if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
                        //맡긴물건을 찾는다.
                        if AddItem (pu) then begin  //가방으로 옮기고
                           SendAddItem (pu^);  //클라이언트에 보냄
                           SaveItems.Delete (i);
                           SendDefMessage (SM_TAKEBACKSTORAGEITEM_OK, itemserverindex, 0, 0, 0, '');
                           //로그남김
                           AddUserLog ('0'#9 + //찾기_ +
                                       MapName + ''#9 +
                                       IntToStr(CX) + ''#9 +
                                       IntToStr(CY) + ''#9 +
                                       UserName + ''#9 +
                                       UserEngine.GetStdItemName (pu.Index) + ''#9 +
                                       IntToStr(pu.MakeIndex) + ''#9 +
                                       '1'#9 +
                                       '0');
                        end else
                           SendDefMessage (SM_TAKEBACKSTORAGEITEM_FULLBAG, 0, 0, 0, 0, ''); //가방 꽉 찼음
                        flag := TRUE;
                     end;
                  end else
                     SysMsg ('轟랬赳던뫘뜩땜鮫。', 0);
               end;
               break;
            end;
         end;
      end;
   end else
      SysMsg ('竟駱친駕櫓퀭꼇콘賈痰꾑욋륩蛟。', 0);
   if not flag then
      SendDefMessage (SM_TAKEBACKSTORAGEITEM_FAIL, 0, 0, 0, 0, ''); //가방 꽉 찼음
end;

{ ServerGetUserMenuBuy - 处理购买物品
  功能: 处理玩家购买物品请求 }
procedure TUserHuman.ServerGetUserMenuBuy (msg, npcid, MakeIndex, menuindex: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
begin
   if BoDealing then exit;  //교환중에는 물건을 살 수 없다.
   npc := UserEngine.GetMerchant (npcid);
   if npc <> nil then begin
      if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
         if msg = CM_USERBUYITEM then begin
            TMerchant(npc).UserBuyItem (self, itemname, MakeIndex);  //상세아이템 or 시약류..
         end;
         if msg = CM_USERGETDETAILITEM then begin
            TMerchant(npc).UserWantDetailItems (self, itemname, menuindex);
         end;
      end;
   end;
end;

{ ServerGetMakeDrug - 处理制作药品
  功能: 处理玩家制作药品请求 }
procedure TUserHuman.ServerGetMakeDrug (npcid: integer; itemname: string);
var
   i: integer;
   npc: TCreature;
begin
   npc := UserEngine.GetMerchant (npcid);
   if npc <> nil then begin
      if (npc.PEnvir = PEnvir) and (abs(npc.CX-CX) <= 15) and (abs(npc.CY-CY) <= 15) then begin
         TMerchant(npc).UserMakeNewItem (self, itemname);
      end;
   end;
end;

{ RefreshGroupMembers - 刷新组队成员
  功能: 组队成员变更时刷新组队列表 }
procedure TUserHuman.RefreshGroupMembers;
var
   i: integer;
   data: string;
   cret: TCreature;
   hum: TUserHuman;
begin
   data := '';
   for i:=0 to GroupMembers.Count-1 do begin
      cret := TCreature (GroupMembers.Objects[i]);
      data := data + cret.UserName + '/';
   end;
   for i:=0 to GroupMembers.Count-1 do begin
      hum := TUserHuman (GroupMembers.Objects[i]);
      hum.SendDefMessage (SM_GROUPMEMBERS, 0, 0, 0, 0, data);
   end;
end;

{ ServerGetCreateGroup - 创建组队
  功能: 处理玩家创建组队请求 }
procedure TUserHuman.ServerGetCreateGroup (withwho: string);
var
   who: TUserHuman;
begin
   who := UserEngine.GetUserHuman (withwho);
   if GroupOwner <> nil then begin
      SendDefMessage (SM_CREATEGROUP_FAIL, -1, 0, 0, 0, '');
      exit;
   end;
   if (who = nil) or (who = self) then begin
      SendDefMessage (SM_CREATEGROUP_FAIL, -2, 0, 0, 0, '');
      exit;
   end;
   if (self.LoginSign = FALSE) or (who.LoginSign = FALSE) then begin
      SendDefMessage (SM_CREATEGROUP_FAIL, -2, 0, 0, 0, '');
      exit;
   end;
   if who.GroupOwner <> nil then begin
      SendDefMessage (SM_CREATEGROUP_FAIL, -3, 0, 0, 0, '');
      exit;
   end;
   if not who.AllowGroup then begin
      SendDefMessage (SM_CREATEGROUP_FAIL, -4, 0, 0, 0, '');
      exit;
   end;
   GroupMembers.Clear;
   GroupMembers.AddObject (UserName, self);
   GroupMembers.AddObject (withwho, who);
   EnterGroup (self);
   who.EnterGroup (self);
   AllowGroup := TRUE;

   SendDefMessage (SM_CREATEGROUP_OK, 0, 0, 0, 0, '');
   RefreshGroupMembers;
end;

{ ServerGetAddGroupMember - 添加组队成员
  功能: 处理玩家添加组队成员请求 }
procedure TUserHuman.ServerGetAddGroupMember (withwho: string);
var
   who: TUserHuman;
   i: integer;
begin
   who := UserEngine.GetUserHuman (withwho);
   if GroupOwner <> self then begin
      SendDefMessage (SM_GROUPADDMEM_FAIL, -1, 0, 0, 0, '');
      exit;
   end;
   if GroupMembers.Count >= GROUPMAX then begin
      SendDefMessage (SM_GROUPADDMEM_FAIL, -5, 0, 0, 0, ''); //full
      exit;
   end;

   if (who = nil) or (who = self) then begin
      SendDefMessage (SM_GROUPADDMEM_FAIL, -2, 0, 0, 0, '');
      exit;
   end;

   if (self.LoginSign = FALSE) or (who.LoginSign = FALSE) then begin
      SendDefMessage (SM_GROUPADDMEM_FAIL, -2, 0, 0, 0, '');
      exit;
   end;

   // 2003/05/02 그룹 중복 버그 패치
   for i := 0 to GroupMembers.Count - 1 do begin // 버그패치
      // PDS -- Nil Check
      if (GroupMembers.Objects[i] = nil ) then
      begin
        svMain.MainOutMessage('ERROR: GROUP MEMBER IS NIL');
      end
      else
      begin
          if CompareText(TCreature(GroupMembers.Objects[i]).UserName, who.UserName) = 0 then begin
             SendDefMessage(SM_GROUPADDMEM_FAIL, -3, 0, 0, 0, ''); //이미 있음
             exit;
          end;
      end;
   end;

   if (who.GroupOwner <> nil) or (who.LoginSign = FALSE) then begin
      SendDefMessage (SM_GROUPADDMEM_FAIL, -3, 0, 0, 0, ''); //이미 있음
      exit;
   end;
   if not who.AllowGroup then begin
      SendDefMessage (SM_GROUPADDMEM_FAIL, -4, 0, 0, 0, '');
      exit;
   end;
   GroupMembers.AddObject (withwho, who);
   who.EnterGroup (self);
   SendDefMessage (SM_GROUPADDMEM_OK, 0, 0, 0, 0, '');
   RefreshGroupMembers;
end;

{ ServerGetDelGroupMember - 删除组队成员
  功能: 处理玩家删除组队成员请求 }
procedure TUserHuman.ServerGetDelGroupMember (withwho: string);
var
   i: integer;
   who: TUserHuman;
begin
   who := UserEngine.GetUserHuman (withwho);
   if GroupOwner <> self then begin
      SendDefMessage (SM_GROUPDELMEM_FAIL, -1, 0, 0, 0, '');
      exit;
   end;
   if who = nil then begin
      SendDefMessage (SM_GROUPDELMEM_FAIL, -2, 0, 0, 0, '');
      exit;
   end;
   if not IsGroupMember (who) then begin
      SendDefMessage (SM_GROUPDELMEM_FAIL, -3, 0, 0, 0, '');
      exit;
   end;
   DelGroupMember (who);
   SendDefMessage (SM_GROUPDELMEM_OK, 0, 0, 0, 0, withwho);
end;

{ ServerGetDealTry - 处理交易请求
  功能: 处理玩家发起交易请求 }
procedure TUserHuman.ServerGetDealTry (withwho: string);
var
   cret: TCreature;
begin
   if BoDealing then exit; //이미 거래중
   cret := GetFrontCret;
   if (cret <> nil) and (cret <> self) then begin //앞에 누가 있어야하고
      if (cret.GetFrontCret = self) and (not cret.BoDealing) then begin //마주보고 있어야하고, 이미 거래중이면 안됨.
         if cret.RaceServer = RC_USERHUMAN then begin
            if cret.BoExchangeAvailable then begin
               //교환 다이얼로그 보낸다.
               cret.SysMsg (UserName + '  뵨콱역迦슥弄죄。', 1);
               SysMsg (cret.UserName + '  뵨콱역迦슥弄죄。', 1);
               StartDeal (TUserHuman(cret));
               TUserHuman(cret).StartDeal(self);
            end else
               SysMsg ('뚤렘앳없뵨퀭슥弄。', 1);
         end;
      end else
         SendDefMessage (SM_DEALTRY_FAIL, 0, 0, 0, 0, '');
   end else
      SendDefMessage (SM_DEALTRY_FAIL, 0, 0, 0, 0, '');
end;

{ ResetDeal - 重置交易
  功能: 重置交易状态 }
procedure TUserHuman.ResetDeal;
var
   i: integer;
begin
   if DealList.Count > 0 then begin
      for i:=DealList.Count-1 downto 0 do begin
         ItemList.Add (DealList[i]); //그래로 위치 이동
      end;
      DealList.Clear;
   end;
   Gold := Gold + DealGold;
   DealGold := 0;
   BoDealSelect := FALSE;
end;

{ StartDeal - 开始交易
  功能: 开始与指定玩家交易 }
procedure TUserHuman.StartDeal (who: TUserHuman);
begin
   BoDealing := TRUE;
   DealCret := who;
   ResetDeal;
   SendDefMessage (SM_DEALMENU, 0, 0, 0, 0, who.UserName);
   DealItemChangeTime := GetTickCount; //
end;

{ BrokeDeal - 取消交易
  功能: 取消当前交易 }
procedure TUserHuman.BrokeDeal;
begin
   if BoDealing then begin
      BoDealing := FALSE;
      SendDefMessage (SM_DEALCANCEL, 0, 0, 0, 0, '');
      if DealCret <> nil then begin
         TUserHuman(DealCret).DealCret := nil;
         if DealCret <> nil then
            TUserHuman(DealCret).BrokeDeal;
      end;
      DealCret := nil;
      ResetDeal;
      SysMsg ('슥弄혤句', 1);
      DealItemChangeTime := GetTickCount; //
   end;
end;

{ ServerGetDealCancel - 处理取消交易
  功能: 处理玩家取消交易请求 }
procedure TUserHuman.ServerGetDealCancel;
begin
   BrokeDeal;
end;

{ AddDealItem - 添加交易物品
  功能: 添加物品到交易列表 }
procedure TUserHuman.AddDealItem (uitem: TUserItem);
var
   citem: TClientItem;
   ps: PTStdItem;
   std: TStdItem;
begin
   SendDefMessage (SM_DEALADDITEM_OK, 0, 0, 0, 0, '');
   if DealCret <> nil then begin
      ps := UserEngine.GetStdItem (uitem.Index);
      if ps <> nil then begin
         std := ps^;
         ItemMan.GetUpgradeStdItem (uitem, std);
         citem.S := std;
         citem.MakeIndex := uitem.MakeIndex;
         citem.Dura := uitem.Dura;
         citem.DuraMax := uitem.DuraMax;
      end;
      Def := MakeDefaultMsg (SM_DEALREMOTEADDITEM, integer(self), 0, 0, 1);
      TUserHuman(DealCret).SendSocket (@Def, EncodeBuffer (@citem, sizeof(TClientItem)));
      TUserHuman(DealCret).DealItemChangeTime := GetTickCount;
      DealItemChangeTime := GetTickCount;
   end;
end;

{ DelDealItem - 删除交易物品
  功能: 从交易列表删除物品 }
procedure TUserHuman.DelDealItem (uitem: TUserItem);
var
   citem: TClientItem;
   ps: PTStdItem;
begin
   SendDefMessage (SM_DEALDELITEM_OK, 0, 0, 0, 0, '');
   if DealCret <> nil then begin
      ps := UserEngine.GetStdItem (uitem.Index);
      if ps <> nil then begin
         citem.S := ps^;
         citem.MakeIndex := uitem.MakeIndex;
         citem.Dura := uitem.Dura;
         citem.DuraMax := uitem.DuraMax;
      end;
      Def := MakeDefaultMsg (SM_DEALREMOTEDELITEM, integer(self), 0, 0, 1);
      TUserHuman(DealCret).SendSocket (@Def, EncodeBuffer (@citem, sizeof(TClientItem)));
      TUserHuman(DealCret).DealItemChangeTime := GetTickCount;
      DealItemChangeTime := GetTickCount;
   end;
end;

{ IsReservedMakingSlave - 是否有预约召唤兽
  功能: 检查是否有预约的召唤兽需要创建 }
function  TUserHuman.IsReservedMakingSlave: Boolean;
var
   i: integer;
	pmsg: PTMessageInfoPtr;
begin
   Result := FALSE;
   try
      csObjMsgLock.Enter;
      // 2003/06/12 슬레이브 패치
      if PrevServerSlaves.Count > 0 then   //소환 해야할 부하가 있음
         Result := TRUE;
      {
      for i:=0 to MsgList.Count-1 do begin
         pmsg := MsgList[i];
         if pmsg.Ident = RM_MAKE_SLAVE then begin
            Result := TRUE;
            break;
         end;
      end;
      }
   finally
      csObjMsgLock.Leave;
   end;
end;

{ ServerGetDealAddItem - 处理添加交易物品
  功能: 处理玩家添加交易物品请求 }
procedure TUserHuman.ServerGetDealAddItem (iidx: integer; iname: string);
var
   i: integer;
   flag: Boolean;
   pstd: PTStdItem;
begin
   //교환 상대가 앞에 있는지, 없으면 거래 취소
   if (DealCret <> nil) then begin
      if pos(' ', iname) >= 0 then
         GetValidStr3 (iname, iname, [' ']);
      flag := FALSE;
      if not DealCret.BoDealSelect then
         for i:=0 to ItemList.Count-1 do begin
            pstd := UserEngine.GetStdItem (PTUserItem(ItemList[i]).Index);
            if pstd <> nil then begin   //교환이 안되는 이벤트 아이템은 제외
               if pstd.StdMode <> TAIWANEVENTITEM then begin
                  if PTUserItem(ItemList[i]).MakeIndex = iidx then
                     if CompareText (UserEngine.GetStdItemName (PTUserItem(ItemList[i]).Index), iname) = 0 then begin
                        if DealList.Count < MAXDEALITEM then begin
                           DealList.Add (ItemList[i]);
                           AddDealItem (PTUserItem(ItemList[i])^);
                           ItemList.Delete (i);
                           flag := TRUE;
                           break;
                        end;
                     end;
               end;
            end;
         end;
      if not flag then
         SendDefMessage (SM_DEALADDITEM_FAIL, 0, 0, 0, 0, '');
   end;
end;

{ ServerGetDealDelItem - 处理删除交易物品
  功能: 处理玩家删除交易物品请求 }
procedure TUserHuman.ServerGetDealDelItem (iidx: integer; iname: string);
var
   i: integer;
   flag: Boolean;
begin
   //교환 상대가 앞에 있는지, 없으면 거래 취소
   if (DealCret <> nil) then begin
      if pos(' ', iname) >= 0 then
         GetValidStr3 (iname, iname, [' ']);
      flag := FALSE;
      if not DealCret.BoDealSelect then
         for i:=0 to DealList.Count-1 do begin
            if PTUserItem(DealList[i]).MakeIndex = iidx then
               if CompareText (UserEngine.GetStdItemName (PTUserItem(DealList[i]).Index), iname) = 0 then begin
                  ItemList.Add (DealList[i]);
                  DelDealItem (PTUserItem(DealList[i])^);
                  DealList.Delete (i);
                  flag := TRUE;
                  break;
               end;
         end;
      if not flag then
         SendDefMessage (SM_DEALDELITEM_FAIL, 0, 0, 0, 0, '');
   end;
end;

{ ServerGetDealChangeGold - 处理交易金币变更
  功能: 处理玩家修改交易金币请求 }
procedure TUserHuman.ServerGetDealChangeGold (dgold: integer);
var
   flag: Boolean;
begin
   if dgold < 0 then begin
      SendDefMessage (SM_DEALCHGGOLD_FAIL, DealGold, Loword(Gold), Hiword(Gold), 0, '');
      exit;
   end;
   flag := FALSE;
   if (GetFrontCret = DealCret) and (DealCret <> nil) then
      if not DealCret.BoDealSelect then begin //상배방이 선택 완료
         if self.Gold + DealGold >= dgold then begin
            self.Gold := (self.Gold + DealGold) - dgold;
            DealGold := dgold;
            SendDefMessage (SM_DEALCHGGOLD_OK, DealGold, Loword(Gold), Hiword(Gold), 0, '');
            if DealCret <> nil then begin
               TUserHuman(DealCret).SendDefMessage (SM_DEALREMOTECHGGOLD, DealGold, 0, 0, 0, '');
               TUserHuman(DealCret).DealItemChangeTime := GetTickCount;
            end;
            flag := TRUE;
            DealItemChangeTime := GetTickCount;
         end;
       end;
   if not flag then
      SendDefMessage (SM_DEALCHGGOLD_FAIL, DealGold, Loword(Gold), Hiword(Gold), 0, '');
end;

{ ServerGetDealEnd - 处理交易结束
  功能: 处理玩家确认交易请求 }
procedure TUserHuman.ServerGetDealEnd;
var
   i: integer;
   pu: PTUserItem;
   ps: PTStdItem;
   flag: Boolean;
begin
   BoDealSelect := TRUE; //교환 버튼을 누름
   if DealCret <> nil then begin
      if (GetTickCount - DealItemChangeTime < 1000) or (GetTickCount - DealCret.DealItemChangeTime < 1000) then begin
         //거래 직전 1초이전에 물건의 이동이 있었음.
         SysMsg ('법豆돨객죄냥슥객큐', 0);
         BrokeDeal;  //거래가 취소
         exit;
      end;
      if DealCret.BoDealSelect then begin //둘다 누름, 교환 시작..
         flag := TRUE;
         //내가 교환품을 받을 만큼 가방에 룸이 있는지 검사..
         if MAXBAGITEM - Itemlist.Count < TUserHuman(DealCret).DealList.Count then flag := FALSE;
         if AvailableGold - Gold < TUserHuman(DealCret).DealGold then flag := FALSE;
         //상대가 교환품을 받을 만큼 가방에 룸이 있는지 검사..
         if MAXBAGITEM - TUserHuman(DealCret).Itemlist.Count < DealList.Count then flag := FALSE;
         if TUserHuman(DealCret).AvailableGold - TUserHuman(DealCret).Gold < DealGold then flag := FALSE;

         if flag then begin
            //내 교환품을 상대에게 줌.
            for i:=0 to DealList.Count-1 do begin
               pu := PTUserItem (DealList[i]);
               TUserHuman(DealCret).AddItem (pu);
               TUserHuman(DealCret).SendAddItem (pu^);
               ps := UserEngine.GetStdItem (pu.Index);
               if ps <> nil then begin
                  //로그남김
                  if not IsCheapStuff (ps.StdMode) then
                     AddUserLog ('8'#9 + //교환_ +
                                 MapName + ''#9 +
                                 IntToStr(CX) + ''#9 +
                                 IntToStr(CY) + ''#9 +
                                 UserName + ''#9 +
                                 UserEngine.GetStdItemName (pu.Index) + ''#9 +
                                 IntToStr(pu.MakeIndex) + ''#9 +
                                 '1'#9 +
                                 DealCret.UserName);
               end;
            end;
            if DealGold > 0 then begin
               DealCret.Gold := DealCret.Gold + DealGold;
               DealCret.GoldChanged;
               //로그남김
               AddUserLog ('8'#9 + //교환_ +
                           MapName + ''#9 +
                           IntToStr(CX) + ''#9 +
                           IntToStr(CY) + ''#9 +
                           UserName + ''#9 +
                           '쏜귑'#9 +
                           IntToStr(DealGold) + ''#9 +
                           '1'#9 +
                           DealCret.UserName);
            end;
            //상대의 교환품을 갖는다.
            for i:=0 to DealCret.DealList.Count-1 do begin
               pu := PTUserItem (DealCret.DealList[i]);
               AddItem (pu);
               SendAddItem (pu^);
               ps := UserEngine.GetStdItem (pu.Index);
               if ps <> nil then begin
                  //로그남김
                  if not IsCheapStuff (ps.StdMode) then
                     AddUserLog ('8'#9 + //교환_ +
                                 DealCret.MapName + ''#9 +
                                 IntToStr(DealCret.CX) + ''#9 +
                                 IntToStr(DealCret.CY) + ''#9 +
                                 DealCret.UserName + ''#9 +
                                 UserEngine.GetStdItemName (pu.Index) + ''#9 +
                                 IntToStr(pu.MakeIndex) + ''#9 +
                                 '1'#9 +
                                 UserName);
               end;
            end;
            if DealCret.DealGold > 0 then begin
               Gold := Gold + DealCret.DealGold;
               GoldChanged;
               //로그남김
               AddUserLog ('8'#9 + //교환_ +
                           DealCret.MapName + ''#9 +
                           IntToStr(DealCret.CX) + ''#9 +
                           IntToStr(DealCret.CY) + ''#9 +
                           DealCret.UserName + ''#9 +
                           '쏜귑'#9 +
                           IntToStr(DealCret.DealGold) + ''#9 +
                           '1'#9 +
                           UserName);
            end;


            with TUserHuman(DealCret) do begin
               SendDefMessage (SM_DEALSUCCESS, 0, 0, 0, 0, '');
               SysMsg ('슥弄냥묘', 1);
               DealCret := nil;
               BoDealing := FALSE;
               DealList.Clear;
               DealGold := 0;
            end;
            SendDefMessage (SM_DEALSUCCESS, 0, 0, 0, 0, '');
            SysMsg ('슥弄냥묘', 1);
            DealCret := nil;
            BoDealing := FALSE;
            DealList.Clear;
            DealGold := 0;
         end else begin
            BrokeDeal;  //거래가 취소
         end;
      end else begin
         SysMsg ('헝횻뚤렘객苟냥슥객큐', 1);
         DealCret.SysMsg ('뚤렘疼늴狼헹콱횅훰슥弄，객苟［슥弄］객큐횅땍', 1);
      end;
   end;
end;

{ ServerGetWantMiniMap - 获取小地图
  功能: 发送小地图信息给客户端 }
procedure TUserHuman.ServerGetWantMiniMap;
var
   i, mini: integer;
begin
   mini := PEnvir.MiniMap;

   if mini > 0 then
      SendDefMessage (SM_READMINIMAP_OK, 0, mini, 0, 0, '')
   else
      SendDefMessage (SM_READMINIMAP_FAIL, 0, 0, 0, 0, '');
end;

{ SendChangeGuildName - 发送行会名称变更
  功能: 发送行会名称变更消息 }
procedure TUserHuman.SendChangeGuildName;
begin
   if MyGuild <> nil then begin
      SendDefMessage (SM_CHANGEGUILDNAME, 0, 0, 0, 0, TGuild(MyGuild).GuildName + '/' + GuildRankName);
   end else
      SendDefMessage (SM_CHANGEGUILDNAME, 0, 0, 0, 0, '');
end;

{ ServerGetQueryUserState - 查询用户状态
  功能: 查询指定玩家的状态信息 }
procedure TUserHuman.ServerGetQueryUserState (who: TCreature; xx, yy: integer);
var
   i: integer;
   ustate: TUserStateInfo;
   ps: PTStdItem;
   std: TStdItem;
   citem: TClientItem;
begin
   if CretInNearXY (who, xx, yy) then begin
      FillChar (ustate, sizeof(TUserStateInfo), #0);
      ustate.Feature := who.GetRelFeature (self);
      ustate.UserName := who.UserName;
      ustate.NameColor := GetThisCharColor (who);
      if who.MyGuild <> nil then
         ustate.GuildName := TGuild(who.MyGuild).GuildName;
      ustate.GuildRankName := who.GuildRankName;
      // 2003/03/15 아이템 인벤토리 확장
      for i:=0 to 12 do begin    // 8->12
         if who.UseItems[i].Index > 0 then begin
            ps := UserEngine.GetStdItem (who.UseItems[i].Index);
            if ps <> nil then begin
               std := ps^;
               ItemMan.GetUpgradeStdItem (who.UseItems[i], std);
               Move (std, citem.S, sizeof(TStdItem));
               citem.MakeIndex := who.UseItems[i].MakeIndex;
               citem.Dura := who.UseItems[i].Dura;
               citem.DuraMax := who.UseItems[i].DuraMax;
               ustate.UseItems[i] := citem;
            end;
         end;
      end;

      Def := MakeDefaultMsg (SM_SENDUSERSTATE, 0, 0, 0, 1);
      SendSocket (@Def, EncodeBuffer (@ustate, sizeof(TUserStateInfo)));

   end;
end;

{ ServerGetOpenGuildDlg - 打开行会对话框
  功能: 发送行会信息并打开行会对话框 }
procedure TUserHuman.ServerGetOpenGuildDlg;
var
   i: integer;
   data: string;
begin
   if MyGuild <> nil then begin
      data := TGuild(MyGuild).GuildName + #13;
      data := data + ' '#13; //문파깃말 파일 이름
      if GuildRank = 1 then data := data + '1'#13  //문주
      else data := data + '0'#13;  //일반

      //NoticeList
      data := data + '<Notice>'#13;
      for i:=0 to TGuild(MyGuild).NoticeList.Count-1 do begin
         if Length(data) > 5000 then break;
         data := data + TGuild(MyGuild).NoticeList[i] + #13;
      end;
      //KillGuilds
      data := data + '<KillGuilds>'#13;
      for i:=0 to TGuild(MyGuild).KillGuilds.Count-1 do begin
         if Length(data) > 5000 then break;
         data := data + TGuild(MyGuild).KillGuilds[i] + #13;
      end;
      //AllyGuilds
      data := data + '<AllyGuilds>'#13;
      for i:=0 to TGuild(MyGuild).AllyGuilds.Count-1 do begin
         if Length(data) > 5000 then break;
         data := data + TGuild(MyGuild).AllyGuilds[i] + #13;
      end;

      Def := MakeDefaultMsg (SM_OPENGUILDDLG, 0, 0, 0, 1);
      SendSocket (@Def, EncodeString (data));
   end else
      SendDefMessage (SM_OPENGUILDDLG_FAIL, 0, 0, 0, 0, '');
end;

{ ServerGetGuildHome - 获取行会主页
  功能: 获取行会主页信息 }
procedure TUserHuman.ServerGetGuildHome;
begin
   ServerGetOpenGuildDlg;
end;

{ ServerGetGuildMemberList - 获取行会成员列表
  功能: 发送行会成员列表 }
procedure TUserHuman.ServerGetGuildMemberList;
var
   i, k: integer;
   data: string;
   pgrank: PTGuildRank;
begin
   if MyGuild <> nil then begin
      data := '';
      for i:=0 to TGuild(MyGuild).MemberList.Count-1 do begin
         pgrank := TGuild(MyGuild).MemberList[i];
         data := data + '#' + IntToStr(pgrank.Rank) + '/*' + pgrank.RankName + '/';
         for k:=0 to pgrank.MemList.Count-1 do begin
            if Length(data) > 5000 then break;
            data := data + pgrank.MemList[k] + '/'
         end;
      end;

      Def := MakeDefaultMsg (SM_SENDGUILDMEMBERLIST, 0, 0, 0, 1);
      SendSocket (@Def, EncodeString (data));
   end;
end;

{ ServerGetGuildAddMember - 添加行会成员
  功能: 添加新成员到行会 }
procedure TUserHuman.ServerGetGuildAddMember (who: string);
var
   error: integer;
   hum: TUserHuman;
begin
   error := 1; //문주만 사용가능
   if IsGuildMaster then begin  //문주만 가능
      hum := UserEngine.GetUserHuman (who);
      if hum <> nil then begin
         if hum.GetFrontCret = self then begin
            if hum.AllowEnterGuild then begin
               if not TGuild(MyGuild).IsMember (who) then begin
                  if (hum.MyGuild = nil) and  //가입문파 없을 때
                     (TGuild(MyGuild).MemberList.Count < MAXGUILDMEMBER)  //인원 제한
                  then begin
                     TGuild(MyGuild).AddMember (hum);
                     UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);

                     //새 맵버를 문파에 가입시킴
                     hum.MyGuild := MyGuild;
                     hum.GuildRankName := TGuild (MyGuild).MemberLogin (hum, hum.GuildRank);
                     //hum.SendMsg (self, RM_CHANGEGUILDNAME, 0, 0, 0, 0, '');
                     error := 0;
                  end else
                     error := 4;  //다른 문파에 가입되어 있음.
               end else
                  error := 3; //이미 가입되어 있음.
            end else begin
               error := 5; //상대방이 문파 가입을 허용안함.
               hum.SysMsg ('퀭앳없속흙契삔。 [豚冀츱즈槨:@속흙쳔탰] ', 0);
            end;
         end else
            error := 2;
      end else
         error := 2; //접속하고 마주보고 있어야 함.
   end;
   if error = 0 then SendDefMessage (SM_GUILDADDMEMBER_OK, 0, 0, 0, 0, '')
   else SendDefMessage (SM_GUILDADDMEMBER_FAIL, error, 0, 0, 0, '');
end;

{ ServerGetGuildDelMember - 删除行会成员
  功能: 从行会中删除成员 }
procedure TUserHuman.ServerGetGuildDelMember (who: string);
var
   error: integer;
   hum: TUserHuman;
   gname: string;
begin
   error := 1; //문주만 사용가능
   if IsGuildMaster then begin  //문주만 가능
      if TGuild(MyGuild).IsMember (who) then begin
         if self.UserName <> who then begin
            if TGuild(MyGuild).DelMember (who) then begin
               hum := UserEngine.GetUserHuman (who);
               if hum <> nil then begin
                  hum.MyGuild := nil;
                  hum.GuildRankChanged (0, '');
               end;
               UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
               error := 0
            end else error := 4; //할 수 없음.
         end else begin
            error := 3; //문주 본인은 탈퇴 안됨.
            //문주본인이 탈퇴하려면 문원이 아무도 없는상태에서 자신을 빼면됨, 문파도 깨짐
            gname := TGuild(MyGuild).GuildName;
            if TGuild(MyGuild).DelGuildMaster (who) then begin
               GuildMan.DelGuild (gname);  //문파가 사라진다.
               UserEngine.SendInterMsg (ISM_DELGUILD, ServerIndex, gname);
               //UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
               MyGuild := nil;
               GuildRankChanged (0, '');
               SysMsg ('"' + gname + '"契삔綠쒔굳혤句죄。', 0);
               error := 0;
            end;
         end;
      end else
         error := 2; //문원이 아님
   end;
   if error = 0 then SendDefMessage (SM_GUILDDELMEMBER_OK, 0, 0, 0, 0, '')
   else SendDefMessage (SM_GUILDDELMEMBER_FAIL, error, 0, 0, 0, '');
end;

{ ServerGetGuildUpdateNotice - 更新行会公告
  功能: 更新行会公告内容 }
procedure TUserHuman.ServerGetGuildUpdateNotice (body: string);
var
   data: string;
begin
   if MyGuild = nil then exit;
   if GuildRank <> 1 then exit; //문파의 문주만 변경 가능

   TGuild(MyGuild).NoticeList.Clear;

   while TRUE do begin
      if body = '' then break;
      body := GetValidStr3 (body, data, [#13]);
      TGuild(MyGuild).NoticeList.Add (data);
   end;
   TGuild(MyGuild).SaveGuild;
   UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);

   ServerGetOpenGuildDlg;
end;

{ ServerGetGuildUpdateRanks - 更新行会职位
  功能: 更新行会职位信息 }
procedure TUserHuman.ServerGetGuildUpdateRanks (body: string);
var
   error: integer;
begin
   if MyGuild = nil then exit;
   if GuildRank <> 1 then exit; //문파의 문주만 변경 가능

   error := TGuild(MyGuild).UpdateGuildRankStr (body);
   if error = 0 then begin
      UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
      ServerGetGuildMemberList;
   end else if error <= -2 then
      SendDefMessage (SM_GUILDRANKUPDATE_FAIL, error, 0, 0, 0, '');
   //-1: 이전과 같음.. 처리하지 않는다.
end;

{ ServerGetGuildMakeAlly - 行会结盟
  功能: 与其他行会结成联盟 }
procedure TUserHuman.ServerGetGuildMakeAlly;
var
   error: integer;
   hum: TUserHuman;
begin
   error := -1; //문주만 사용가능
   hum := TUserHuman(GetFrontCret);
   if hum <> nil then begin
      if hum.RaceServer = RC_USERHUMAN then begin
         if hum.GetFrontCret = self then begin  //얼굴을 마주보고 있는지
            if TGuild(hum.MyGuild).AllowAllyGuild then begin
               if IsGuildMaster and hum.IsGuildMaster then begin  //문주만 가능
                  if TGuild(MyGuild).CanAlly(TGuild(hum.MyGuild)) and TGuild(hum.MyGuild).CanAlly(TGuild(MyGuild)) then begin
                     //동맹 조건 충족
                     TGuild(MyGuild).MakeAllyGuild (TGuild(hum.MyGuild));
                     TGuild(hum.MyGuild).MakeAllyGuild (TGuild(MyGuild));
                     TGuild(MyGuild).GuildMsg (TGuild(hum.MyGuild).GuildName + '契삔綠쒔뵨퀭돨契삔써촉供냥。');
                     TGuild(hum.MyGuild).GuildMsg (TGuild(MyGuild).GuildName + '契삔綠쒔뵨퀭돨契삔써촉供냥。');

                     TGuild(MyGuild).MemberNameChanged;
                     TGuild(hum.MyGuild).MemberNameChanged;

                     //다른 서버에 적용
                     UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
                     UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(hum.MyGuild).GuildName);
                     error := 0;
                  end else
                     error := -2;  //동맹 실패
               end else
                  error := -3;  //문주끼리 마주보고 해야 한다.
            end else
               error := -4;  //상대가 동맹을 허용하지 않고 있음.
         end;
      end;
   end;
   if error = 0 then  //성공
      SendDefMessage (SM_GUILDMAKEALLY_OK, 0, 0, 0, 0, '')
   else SendDefMessage (SM_GUILDMAKEALLY_FAIL, error, 0, 0, 0, '');
end;

{ ServerGetGuildBreakAlly - 解除行会联盟
  功能: 解除与指定行会的联盟 }
procedure TUserHuman.ServerGetGuildBreakAlly (gname: string);
var
   aguild: TGuild;
   error: integer;
begin
   error := -1;
   if IsGuildMaster then begin
      aguild := GuildMan.GetGuild (gname);
      if aguild <> nil then begin
         if TGuild(MyGuild).IsAllyGuild (aguild) then begin
            TGuild(MyGuild).BreakAlly (aguild);
            aguild.BreakAlly (TGuild(MyGuild));
            TGuild(MyGuild).GuildMsg (aguild.GuildName + '契삔綠쒔뵨퀭돨契삔썩뇜써촉供냥。');
            aguild.GuildMsg (TGuild(MyGuild).GuildName + '契삔썩뇜죄宅퀭돨契삔돨써촉。');

            TGuild(MyGuild).MemberNameChanged;
            aguild.MemberNameChanged;

            //다른 서버에 적용
            UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, TGuild(MyGuild).GuildName);
            UserEngine.SendInterMsg (ISM_RELOADGUILD, ServerIndex, aguild.GuildName);
            error := 0;
         end else error := -2;  //동맹중 아님
      end else error := -3; //그런 문파 없음
   end;
   if error = 0 then  //성공
      SendDefMessage (SM_GUILDBREAKALLY_OK, 0, 0, 0, 0, '')
   else SendDefMessage (SM_GUILDBREAKALLY_FAIL, error, 0, 0, 0, '');
end;

{ ServerGetAdjustBonus - 调整奖励点
  功能: 处理玩家调整奖励点请求 }
procedure TUserHuman.ServerGetAdjustBonus (remainbonus: integer; bodystr: string);
   function CalcLoHi (abil, point: word): word;
   var
      i, lo, hi: integer;
   begin
      lo := Lobyte(abil);
      hi := Hibyte(abil);
      for i:=1 to point do begin
         if lo + 1 < hi then Inc (lo)
         else Inc (hi);
      end;
      Result := MakeWord(lo, hi);
   end;
var
   cha: TNakedAbility;
   sum: integer;
   ptk: PTNakedAbility;
begin
{$IFDEF FOR_ABIL_POINT}
//4/16일부터 적용
   if (remainbonus >= 0) and (remainbonus < BonusPoint) then begin
      DecodeBuffer (bodystr, @cha, sizeof(TNakedAbility));
      //검증...
      sum := cha.DC + cha.MC + cha.SC + cha.AC + cha.MAC + cha.HP + cha.MP + cha.Hit + cha.Speed;
      ptk := nil;
      case Job of
         0: ptk := @WarriorBonus;
         1: ptk := @WizzardBonus;
         2: ptk := @PriestBonus;
      end;
      if (ptk <> nil) and (sum = (BonusPoint - remainbonus)) then begin
         BonusPoint := remainbonus;
         CurBonusAbil.DC := CurBonusAbil.DC + cha.DC;
         CurBonusAbil.MC := CurBonusAbil.MC + cha.MC;
         CurBonusAbil.SC := CurBonusAbil.SC + cha.SC;
         CurBonusAbil.AC := CurBonusAbil.AC + cha.AC;
         CurBonusAbil.MAC := CurBonusAbil.MAC + cha.MAC;
         CurBonusAbil.HP := CurBonusAbil.HP + cha.HP;
         CurBonusAbil.MP := CurBonusAbil.MP + cha.MP;
         CurBonusAbil.Hit := CurBonusAbil.Hit + cha.Hit;
         CurBonusAbil.Speed := CurBonusAbil.Speed + cha.Speed;

         BonusAbil.DC := CalcLoHi (BonusAbil.DC, CurBonusAbil.DC div ptk.DC);
         CurBonusAbil.DC := CurBonusAbil.DC mod ptk.DC;

         BonusAbil.MC := CalcLoHi (BonusAbil.MC, CurBonusAbil.MC div ptk.MC);
         CurBonusAbil.MC := CurBonusAbil.MC mod ptk.MC;

         BonusAbil.SC := CalcLoHi (BonusAbil.SC, CurBonusAbil.SC div ptk.SC);
         CurBonusAbil.SC := CurBonusAbil.SC mod ptk.SC;

         BonusAbil.AC := MakeWord(0, Hibyte(BonusAbil.AC) + CurBonusAbil.AC div ptk.AC);   //CalcLoHi (BonusAbil.AC, CurBonusAbil.AC div ptk.AC);
         CurBonusAbil.AC := CurBonusAbil.AC mod ptk.AC;

         BonusAbil.MAC := MakeWord(0, Hibyte(BonusAbil.MAC) + CurBonusAbil.MAC div ptk.MAC);//CalcLoHi (BonusAbil.MAC, CurBonusAbil.MAC div ptk.MAC);
         CurBonusAbil.MAC := CurBonusAbil.MAC mod ptk.MAC;

         BonusAbil.HP := BonusAbil.HP + CurBonusAbil.HP div ptk.HP;
         CurBonusAbil.HP := CurBonusAbil.HP mod ptk.HP;

         BonusAbil.MP := BonusAbil.MP + CurBonusAbil.MP div ptk.MP;
         CurBonusAbil.MP := CurBonusAbil.MP mod ptk.MP;

         BonusAbil.Hit := BonusAbil.Hit + CurBonusAbil.Hit div ptk.Hit;
         CurBonusAbil.Hit := CurBonusAbil.Hit mod ptk.Hit;

         BonusAbil.Speed := BonusAbil.Speed + CurBonusAbil.Speed div ptk.Speed;
         CurBonusAbil.Speed := CurBonusAbil.Speed mod ptk.Speed;

         RecalcLevelAbilitys;
         RecalcAbilitys;
         SendMsg (self, RM_ABILITY, 0, 0, 0, 0, '');
         SendMsg (self, RM_SUBABILITY, 0, 0, 0, 0, '');
      end;
      ServerSendAdjustBonus;  //보너스 포인트를 다시 보내준다.
   end;
{$ENDIF}
end;


{ RmMakeSlaveProc - 创建召唤兽
  功能: 根据召唤兽信息创建召唤兽
  参数:
    pslave - 召唤兽信息指针 }
procedure TUserHuman.RmMakeSlaveProc (pslave: PTSlaveInfo);
var
   cret: TCreature;   // 生物
   maxcount: integer; // 最大数量
begin
   // 道士最多1个，其他职业最多5个
   if Job = 2 then
      maxcount := 1
   else maxcount := 5;
   cret := MakeSlave (pslave.SlaveName,
                      pslave.SlaveMakeLevel,
                      maxcount,
                      pslave.RemainRoyalty);
   if cret <> nil then begin
      cret.SlaveExp := pslave.SlaveExp;
      cret.SlaveExpLevel := pslave.SlaveExpLevel;
      cret.WAbil.HP := pslave.HP;
      cret.WAbil.MP := pslave.MP;
      // 根据等级调整移动和攻击速度
      if cret.NextWalkTime > 1500-(pslave.SlaveMakeLevel*200)  then cret.NextWalkTime := 1500-(pslave.SlaveMakeLevel*200);
      if cret.NextHitTime > 2000-(pslave.SlaveMakeLevel*200) then cret.NextHitTime := 2000-(pslave.SlaveMakeLevel*200);

      cret.RecalcAbilitys;

   end;
end;

end.
