{ ============================================================================
  单元名称: Grobal2
  功能描述: 传奇2游戏全局常量、数据类型和通用函数定义单元
  
  主要功能:
  - 定义游戏版本和运行模式常量
  - 定义客户端与服务器之间通信的消息头和消息体结构
  - 定义游戏中的各种数据记录类型（物品、角色、技能、地图等）
  - 定义服务器消息(SM_*)、客户端消息(CM_*)、内部消息(RM_*)等协议常量
  - 定义游戏机制相关常量（种族、方向、装备槽位、任务等）
  - 提供角色外观特征的编码/解码工具函数
  
  注意事项:
  - 此单元为游戏核心公共单元，被客户端和服务器端共同引用
  - 修改此文件需要同时考虑对客户端和服务器端的影响
  - 包含条件编译指令 MIR2EI 用于区分不同游戏版本
  
  作者: Mir2开发团队
  创建日期: 2001
  修改日期: 2003/06/12
============================================================================ }
unit Grobal2;

interface

uses
	Windows, SysUtils, Classes, Hutil32;  // Windows API、系统工具、类库、自定义工具库
{ ============================================================================
  版本控制常量
  用途: 控制游戏的运行模式和界面版本
============================================================================ }
const
  { 游戏运行模式常量 }
  Public_Test = 0;     // 测试版本模式
  Public_Release = 1;  // 正式发布模式
  Public_Free = 2;     // 免费版本模式

  Public_Ver = Public_Free;  // 当前版本设置为免费版

  Var_Free = 0;  // 自由版本标识

  { 界面版本常量 }
  Var_Default = 0;   // 默认界面版本
  Var_Mir2 = 1;      // 传奇2界面版本

  Var_Interface = Var_Mir2;   // 当前使用的界面版本

{ ============================================================================
  数据类型定义
  用途: 定义游戏中使用的各种数据结构
============================================================================ }
type
   { TMsgHeader - 消息头记录
     功能: 用于网关(Gate)与服务器之间的通信
     用途: 封装消息的元信息，包含连接标识和数据长度 }
   TMsgHeader = record
      Code:          integer;  // 消息识别码，固定值 $aa55aa55，用于验证消息有效性
      SNumber:       integer;  // Socket连接编号
      UserGateIndex: word;     // 用户在网关中的索引位置
      Ident:         word;     // 消息标识符
      UserListIndex: word;     // 用户在服务器用户列表中的索引
      temp:          word;     // 临时保留字段
      length:        integer;  // 消息体(body)的二进制数据长度
   end;
   PTMsgHeader = ^TMsgHeader;  // TMsgHeader的指针类型

   { TDefaultMessage - 默认消息记录
     功能: 标准消息格式，用于客户端和服务器之间的通信
     用途: 传递游戏操作指令和数据
     大小: 12字节 }
   TDefaultMessage = record
      Recog:   integer;       // 识别码/角色ID，4字节
      Ident:   word;          // 消息标识符，2字节
      Param:   word;          // 参数，2字节
      Tag:     word;          // 标签/附加信息，2字节
      Series:  word;          // 序列号，2字节
   end;
   PTDefaultMessage = ^TDefaultMessage;  // TDefaultMessage的指针类型

   { TChrMsg - 角色消息记录
     功能: 客户端使用的角色信息消息
     用途: 传递角色的位置、状态、外观等信息 }
   TChrMsg = record
      ident:   integer;   // 消息标识符
      x:       integer;   // X坐标
      y:       integer;   // Y坐标
      dir:     integer;   // 方向 (0-7，表示8个方向)
      feature: integer;   // 外观特征码
      state:   integer;   // 状态
      saying:  string;    // 说话内容
      sound:   integer;   // 音效编号
   end;
   PTChrMsg = ^TChrMsg;  // TChrMsg的指针类型

   { TMessageInfo - 消息信息记录
     功能: 服务器端使用的消息结构
     用途: 在服务器内部传递消息和对象引用 }
   TMessageInfo = record
      Ident	: word;        // 消息标识符
      wParam	: word;        // 字参数
      lParam1	: Longint;     // 长整型参数1
      lParam2 : Longint;     // 长整型参数2
      lParam3 : Longint;     // 长整型参数3
      sender	: TObject;     // 发送者对象引用
      target  : TObject;     // 目标对象引用
      description : string;  // 描述信息
   end;
   PTMessageInfo = ^TMessageInfo;  // TMessageInfo的指针类型

   { TMessageInfoPtr - 消息信息指针记录
     功能: 带延迟投递功能的消息结构
     用途: 用于需要定时投递的消息处理 }
   TMessageInfoPtr = record
      Ident	: word;           // 消息标识符
      wParam	: word;           // 字参数
      lParam1	: Longint;        // 长整型参数1
      lParam2 : Longint;        // 长整型参数2
      lParam3 : Longint;        // 长整型参数3
      sender	: TObject;        // 发送者对象引用
      //target  : TObject;      // 目标对象引用(已注释)
      deliverytime: longword;   // 消息投递时间(到达时间)
      descptr : PChar;          // 描述信息指针
   end;
   PTMessageInfoPtr = ^TMessageInfoPtr;  // TMessageInfoPtr的指针类型

   { TShortMessage - 短消息记录
     功能: 简化的消息格式
     用途: 用于简单的消息传递 }
   TShortMessage = record
      Ident    : word;  // 消息标识符
      msg      : word;  // 消息内容
   end;

   { TMessageBodyW - 消息体(字型)记录
     功能: Word类型参数的消息体
     用途: 传递4个Word类型的参数 }
   TMessageBodyW = record
     Param1    : word;  // 参数1
     Param2    : word;  // 参数2
     Tag1      : word;  // 标签1
     Tag2      : word;  // 标签2
   end;

   { TMessageBodyWL - 消息体(长整型)记录
     功能: Longint类型参数的消息体
     用途: 传递4个Longint类型的参数 }
   TMessageBodyWL = record
     lParam1   : longint;  // 长整型参数1
     lParam2   : longint;  // 长整型参数2
     lTag1     : longint;  // 长整型标签1
     lTag2     : longint;  // 长整型标签2
   end;

   { TCharDesc - 角色描述记录
     功能: SM_WALK消息中使用的角色移动信息
     用途: 描述角色的外观特征和状态 }
   TCharDesc = record
     Feature : integer;   // 外观特征码
     Status  : integer;   // 角色状态
   end;

   { TPowerClass - 能力等级记录
     功能: 定义能力值的范围
     用途: 存储最小值、常规值和最大值 }
   TPowerClass = record
      Min   : byte;   // 最小值
      Ever  : byte;   // 常规值/基础值
      Max   : byte;   // 最大值
      dummy : byte;   // 占位字段，用于对齐
   end;

   { TNakedAbility - 裸装能力值记录
     功能: 角色不穿装备时的基础能力值
     用途: 作为能力值计算的基准 }
   TNakedAbility = record
      DC          : word;   // 物理攻击力 (Damage Class)
      MC          : word;   // 魔法攻击力 (Magic Class)
      SC          : word;   // 道术攻击力 (Spirit Class)
      AC          : word;   // 物理防御力 (Armor Class)
      MAC         : word;   // 魔法防御力 (Magic Armor Class)
      HP          : word;   // 生命值 (Health Point)
      MP          : word;   // 魔法值 (Magic Point)
      Hit         : word;   // 命中率
      Speed       : word;   // 速度/敏捷
      Reserved    : word;   // 保留字段
   end;
   PTNakedAbility = ^TNakedAbility;  // TNakedAbility的指针类型

   { TChgAttr - 属性变更记录
     功能: 记录属性变更信息
     用途: 用于物品属性修改时传递变更数据 }
   TChgAttr = record
      attr         : byte;  // 变更的属性标识: 1=AC 2=MAC 3=DC 4=MC 5=SC
      min          : byte;  // DC/MC/SC的最小值，AC/MAC时为MakeWord(min,max)的值
      max          : byte;  // DC/MC/SC的最大值
   end;

{ ============================================================================
  条件编译区域: MIR2EI 版本
  说明: MIR2EI是传奇2的扩展版本，包含一些额外的功能和修改
============================================================================ }
{$ifdef MIR2EI}

   { TStdItem - 标准物品记录 (MIR2EI版本)
     功能: 定义游戏中标准物品的基本属性
     用途: 作为物品数据库的基础结构，存储物品模板信息 }
   TStdItem = record
      Name		    : string[30];   // 物品名称，最大30字符 (如: 天下第一剑)
      StdMode      : byte;         // 物品标准模式/类型
      Shape 	   : byte;         // 物品形态编号 (如: 铁剑)
      CharLooks    : byte;         // 角色外观显示类型
      Weight       : byte;         // 物品重量
      AniCount     : byte;         // 动画帧数，大于1表示有动画效果 (也用于其他用途)
      SpecialPwr   : shortint;     // 特殊能力值:
                                   //   正值(1~10): 对生物攻击加成强度
                                   //   负值(-50~-1): 对亡灵能力增强
                                   //   负值(-100~-51): 对亡灵能力减弱
      ItemDesc     : byte;         // 物品描述标志:
                                   //   $01 IDC_UNIDENTIFIED: 未鉴定(仅客户端使用)
                                   //   $02 IDC_UNABLETAKEOFF: 无法脱下，可用未知数
                                   //   $04 IDC_NEVERTAKEOFF: 永远无法脱下，不可用未知数
                                   //   $08 IDC_DIEANDBREAK: 死亡时损坏属性
                                   //   $10 IDC_NEVERLOSE: 死亡时不掉落
      Looks        : word;         // 物品图片编号
      DuraMax      : word;         // 最大耐久度
      AC           : word;         // 物理防御力
      MACType      : byte;         // 魔法防御类型
      MAC          : word;         // 魔法防御力
      DC           : word;         // 物理攻击力
      MCType       : byte;         // 魔法攻击类型
      MC           : word;         // 术士魔法力
      AtomDCType   : byte;         // 元素物理攻击类型
      AtomDC       : word;         // 元素物理攻击力
//      SCType       : byte;
//      SC           : word;       // 道士精神力 (已注释)
      Need         : byte;         // 装备需求类型: 0=等级, 1=DC, 2=MC, 3=SC
      NeedLevel    : byte;         // 需求等级值 (1..60)
      Price        : integer;      // 物品价格
      FuncType     : byte;         // 功能类型
      Throw        : byte;         // 特殊标志:
                                   //   1: 死亡时不掉落
                                   //   2: 计数型物品
      Reserved     : array[0..11] of byte;  // 保留字段
   end;
   PTStdItem = ^TStdItem;  // TStdItem的指针类型

   { TStdItemPack - 标准物品打包记录 (MIR2EI版本)
     功能: TStdItem的紧凑打包版本
     用途: 用于网络传输和文件存储，减少内存占用 }
    TStdItemPack = packed record
        Name	    : array[0..29] of char;  // 物品名称，固定30字符
        StdMode     : byte;                  // 物品标准模式/类型
        Shape       : byte;                  // 物品形态编号 (如: 铁剑)
        Weight      : byte;                  // 物品重量
        AniCount    : byte;                  // 动画帧数，大于1表示有动画效果
        SpecialPwr  : shortint;              // 特殊能力值:
                                             //   正值(1~10): 对生物攻击加成强度
                                             //   负值(-50~-1): 对亡灵能力增强
                                             //   负值(-100~-51): 对亡灵能力减弱
        ItemDesc     : byte;                 // 物品描述标志:
                                             //   $01 IDC_UNIDENTIFIED: 未鉴定
                                             //   $02 IDC_UNABLETAKEOFF: 无法脱下，可用未知数
                                             //   $04 IDC_NEVERTAKEOFF: 永远无法脱下
                                             //   $08 IDC_DIEANDBREAK: 死亡时损坏
                                             //   $10 IDC_NEVERLOSE: 死亡时不掉落
        Looks        : word;                 // 物品图片编号
        DuraMax      : word;                 // 最大耐久度
        AC           : word;                 // 物理防御力
        MACType      : byte;                 // 魔法防御类型
        MAC          : word;                 // 魔法防御力
        DC           : word;                 // 物理攻击力
        MCType       : byte;                 // 魔法攻击类型
        MC           : word;                 // 术士魔法力
        AtomDCType   : byte;                 // 元素物理攻击类型
        AtomDC       : word;                 // 元素物理攻击力
        Need         : byte;                 // 装备需求类型: 0=等级, 1=DC, 2=MC, 3=SC
        NeedLevel    : byte;                 // 需求等级值 (1..60)
        Price        : integer;              // 物品价格
        FuncType     : byte;                 // 功能类型
        Throw        : byte;                 // 特殊标志: 1=死亡不掉落, 2=计数型物品
    end;
   PTStdItemPack = ^TStdItemPack;  // TStdItemPack的指针类型

   { TUserItem - 用户物品记录 (MIR2EI版本)
     功能: 存储玩家拥有的具体物品实例信息
     用途: 与TStdItem配合使用，TStdItem是模板，TUserItem是实例 }
   TUserItem = packed record
      MakeIndex  : integer;   // 物品制造索引，服务器生成时分配，可重复
      Index      : word;      // 标准物品索引，0=无物品，从1开始
      Dura       : word;      // 当前耐久度
      DuraMax    : word;      // 变更后的最大耐久度
      Desc       : array[0..13] of byte;  // 物品描述数组:
           // [0..7]: 物品升级状态
           // [10]: 升级鉴定状态:
           //   0: 与升级无关
           //   1: 攻击力升级未鉴定
           //   2: 魔力(自然系)升级未鉴定
           //   3: 道力升级未鉴定(Mir2) / 魔力(灵魂系)升级未鉴定(Mir3)
           //   5: 攻击速度升级未鉴定
           //   9: 失败，已折叠
           // [11]: MAC_TYPE (魔防类型)
           // [12]: MC_TYPE (魔攻类型)
      ColorR     : byte;      // 颜色R分量
      ColorG     : byte;      // 颜色G分量
      ColorB     : byte;      // 颜色B分量
      Prefix     : array [0..12] of char;  // 物品前缀名称
   end;
   PTUserItem = ^TUserItem;  // TUserItem的指针类型

   { TAbility - 角色能力值记录 (MIR2EI版本)
     功能: 存储角色的各项能力属性
     用途: 角色面板显示和战斗计算的基础数据 }
   TAbility = packed record
      Level       : byte;     // 角色等级
//      reserved1   : byte;   // 保留字段(已注释)
      AC          : word;     // 物理防御力 (Armor Class)

//      MAC         : word;   // 魔法防御力(已注释)
      DC          : word;     // 物理攻击力 (Damage Class)，使用MakeWord(min,max)格式

//      MC          : word;   // 魔法攻击力(已注释)
//      SC          : word;   // 道术攻击力(已注释)

      HP          : word;     // 当前生命值 (Health Point)
      MP          : word;     // 当前魔法值 (Magic Point)

      MaxHP       : word;     // 最大生命值
      MaxMP       : word;     // 最大魔法值

//      ExpCount    : byte;   // 未使用，已删除
//      ExpMaxCount : byte;   // 未使用，已删除

      Exp         : longword;  // 当前经验值
      MaxExp      : longword;  // 当前等级最大经验值

      Weight      : word;      // 当前负重
      MaxWeight   : word;      // 最大负重能力

      WearWeight    : byte;    // 当前穿戴重量
      MaxWearWeight : byte;    // 最大穿戴重量(不含武器)，超过时移动速度降低2-3倍
      HandWeight    : byte;    // 当前手持重量
      MaxHandWeight : byte;    // 最大手持武器重量，超过时攻击速度降低2-3倍

      // MIR2EI扩展字段(已注释，包含声望、采矿、耕作、钓鱼等生活技能)
{      FameLevel      : byte;  // 声望等级
      MiningLevel    : byte;  // 采矿等级
      FramingLevel   : byte;  // 耕作等级
      FishingLevel   : byte;  // 钓鱼等级

      FameExp        : integer;     // 声望经验值
      FameMaxExp     : integer;     // 声望最大经验值
      MiningExp      : integer;     // 采矿经验值
      MiningMaxExp   : integer;     // 采矿最大经验值
      FramingExp     : integer;     // 耕作经验值
      FramingMaxExp  : integer;     // 耕作最大经验值
      FishingExp     : integer;     // 钓鱼经验值
      FishingMaxExp  : integer;     // 钓鱼最大经验值 }

      // 元素系统属性数组
      ATOM_DC        : array [0.._MAX_ATOM_] of word;  // 元素物理攻击力数组
      ATOM_MC        : array [0.._MAX_ATOM_] of word;  // 元素魔法攻击力数组
                                                       // 元素索引: 0=火, 1=冰, 2=雷, 3=风, 4=神圣, 5=黑暗, 6=幻影
      ATOM_MAC       : array [0.._MAX_ATOM_] of word;  // 元素魔法防御力数组
   end;

   { TAddAbility - 附加能力值记录 (MIR2EI版本)
     功能: 记录装备穿戴后增加的能力值
     用途: 计算装备对角色属性的加成效果 }
   TAddAbility = record
      HP          : word;     // 生命值加成
      MP          : word;     // 魔法值加成
      HIT         : word;     // 命中率加成
      SPEED       : word;     // 速度加成
      AC          : word;     // 物理防御加成
//      MAC         : word;   // 魔法防御加成(已注释)
      DC          : word;     // 物理攻击加成
//      MC          : word;   // 魔法攻击加成(已注释)
//      SC          : word;   // 道术攻击加成(已注释)
      AntiPoison  : word;     // 抗毒能力(百分比%)
      PoisonRecover : word;   // 毒素恢复速度(百分比%)
      HealthRecover : word;   // 生命恢复速度(百分比%)
      SpellRecover : word;    // 魔法恢复速度(百分比%)
      AntiMagic   : word;     // 魔法闪避率(百分比%)
      Luck        : byte;     // 幸运值
      UnLuck      : byte;     // 诅咒值/不幸值
      WeaponStrong : byte;    // 武器强度
      UndeadPower : byte;     // 对亡灵攻击力
      HitSpeed    : shortint; // 攻击速度加成
      // 元素系统加成
      ATOM_DC        : array [0.._MAX_ATOM_] of word;  // 元素物理攻击加成
      ATOM_MC        : array [0.._MAX_ATOM_] of word;  // 元素魔法攻击加成
      ATOM_MAC       : array [0.._MAX_ATOM_] of word;  // 元素魔法防御加成
   end;

{ ============================================================================
  条件编译区域: 标准传奇2版本
  说明: 非MIR2EI版本使用的数据结构
============================================================================ }
{$else}

   { TStdItem - 标准物品记录 (传奇2标准版本)
     功能: 定义游戏中标准物品的基本属性
     用途: 作为物品数据库的基础结构，存储物品模板信息 }
   TStdItem = record
  	   Name		    : string[14];   // 物品名称，最大14字符 (如: 天下第一剑)
      StdMode      : byte;         // 物品标准模式/类型
      Shape 	    : byte;         // 物品形态编号 (如: 铁剑)
      Weight       : byte;         // 物品重量
      AniCount     : byte;         // 动画帧数，大于1表示有动画效果 (也用于其他用途)
      SpecialPwr   : shortint;     // 特殊能力值:
                                   //   正值(1~10): 对生物攻击加成强度
                                   //   负值(-50~-1): 对亡灵能力增强
                                   //   负值(-100~-51): 对亡灵能力减弱
      ItemDesc     : byte;         // 物品描述标志:
                                   //   $01 IDC_UNIDENTIFIED: 未鉴定(仅客户端使用)
                                   //   $02 IDC_UNABLETAKEOFF: 无法脱下，可用未知数
                                   //   $04 IDC_NEVERTAKEOFF: 永远无法脱下，不可用未知数
                                   //   $08 IDC_DIEANDBREAK: 死亡时损坏属性
                                   //   $10 IDC_NEVERLOSE: 死亡时不掉落
      Looks        : word;         // 物品图片编号
      DuraMax      : word;         // 最大耐久度
      AC           : word;         // 物理防御力
      MAC          : word;         // 魔法防御力
      DC           : word;         // 物理攻击力
      MC           : word;         // 术士魔法力
      SC           : word;         // 道士精神力
      Need         : byte;         // 装备需求类型: 0=等级, 1=DC, 2=MC, 3=SC
      NeedLevel    : byte;         // 需求等级值 (1..60)
      Price        : integer;      // 物品价格
      // 2003/03/15 新增字段
      Stock        : integer;      // 库存数量
      AtkSpd       : byte;         // 攻击速度
      Agility      : byte;         // 敏捷度
      Accurate     : byte;         // 准确度
      MgAvoid      : byte;         // 魔法闪避
      Strong       : byte;         // 强度
      Undead       : byte;         // 对亡灵攻击
      HpAdd        : integer;      // 附加生命值
      MpAdd        : integer;      // 附加魔法值
      ExpAdd       : integer;      // 附加经验值
      EffType1     : byte;         // 效果类型1
      EffRate1     : byte;         // 效果触发概率1
      EffValue1    : byte;         // 效果数值1
      EffType2     : byte;         // 效果类型2
      EffRate2     : byte;         // 效果触发概率2
      EffValue2    : byte;         // 效果数值2
   end;
   PTStdItem = ^TStdItem;  // TStdItem的指针类型

   { TUserItem - 用户物品记录 (传奇2标准版本)
     功能: 存储玩家拥有的具体物品实例信息
     用途: 与TStdItem配合使用，TStdItem是模板，TUserItem是实例 }
   TUserItem = packed record
      MakeIndex  : integer;   // 物品制造索引，服务器生成时分配，可重复
      Index      : word;      // 标准物品索引，0=无物品，从1开始
      Dura       : word;      // 当前耐久度
      DuraMax    : word;      // 变更后的最大耐久度
      Desc       : array[0..13] of byte;  // 物品描述数组:
           // [0..7]: 物品升级状态
           // [10]: 升级鉴定状态:
           //   0: 与升级无关
           //   1: 攻击力升级未鉴定
           //   2: 魔力升级未鉴定
           //   3: 道力升级未鉴定
           //   5: 攻击速度升级未鉴定
           //   9: 失败，已折叠
      ColorR     : byte;      // 颜色R分量
      ColorG     : byte;      // 颜色G分量
      ColorB     : byte;      // 颜色B分量
      Prefix     : array [0..12] of char;  // 物品前缀名称
   end;
   PTUserItem = ^TUserItem;  // TUserItem的指针类型

   { TAbility - 角色能力值记录 (传奇2标准版本)
     功能: 存储角色的各项能力属性
     用途: 角色面板显示和战斗计算的基础数据 }
   TAbility = record
      Level       : byte;     // 角色等级
      reserved1   : byte;     // 保留字段
      AC          : word;     // 物理防御力 (Armor Class)
      MAC         : word;     // 魔法防御力 (Magic Armor Class)
      DC          : word;     // 物理攻击力 (Damage Class)，使用MakeWord(min,max)格式
      MC          : word;     // 魔法攻击力 (Magic Class)，使用MakeWord(min,max)格式
      SC          : word;     // 道术攻击力 (Spirit Class)，使用MakeWord(min,max)格式
      HP          : word;     // 当前生命值 (Health Point)
      MP          : word;     // 当前魔法值 (Magic Point)
      MaxHP       : word;     // 最大生命值
      MaxMP       : word;     // 最大魔法值
      ExpCount    : byte;     // 未使用
      ExpMaxCount : byte;     // 未使用
      Exp         : longword; // 当前经验值
      MaxExp      : longword; // 当前等级最大经验值
      Weight      : word;     // 当前负重
      MaxWeight   : word;     // 最大负重能力
      WearWeight    : byte;   // 当前穿戴重量
      MaxWearWeight : byte;   // 最大穿戴重量(不含武器)，超过时移动速度降低2-3倍
      HandWeight    : byte;   // 当前手持重量
      MaxHandWeight : byte;   // 最大手持武器重量，超过时攻击速度降低2-3倍
   end;

   { TAddAbility - 附加能力值记录 (传奇2标准版本)
     功能: 记录装备穿戴后增加的能力值
     用途: 计算装备对角色属性的加成效果 }
   TAddAbility = record
      HP          : word;     // 生命值加成
      MP          : word;     // 魔法值加成
      HIT         : word;     // 命中率加成
      SPEED       : word;     // 速度加成
      AC          : word;     // 物理防御加成
      MAC         : word;     // 魔法防御加成
      DC          : word;     // 物理攻击加成
      MC          : word;     // 魔法攻击加成
      SC          : word;     // 道术攻击加成
      AntiPoison  : word;     // 抗毒能力(百分比%)
      PoisonRecover : word;   // 毒素恢复速度(百分比%)
      HealthRecover : word;   // 生命恢复速度(百分比%)
      SpellRecover : word;    // 魔法恢复速度(百分比%)
      AntiMagic   : word;     // 魔法闪避率(百分比%)
      Luck        : byte;     // 幸运值
      UnLuck      : byte;     // 诅咒值/不幸值
      WeaponStrong : byte;    // 武器强度
      UndeadPower : byte;     // 对亡灵攻击力
      HitSpeed    : shortint; // 攻击速度加成
   end;

{$endif}  // 传奇2标准版本条件编译结束


   { TPricesInfo - 价格信息记录
     功能: 存储物品的价格信息
     用途: 商店系统中的物品定价 }
   TPricesInfo = record
      Index       : word;     // 标准物品索引
      SellPrice   : integer;  // 出售价格(基础价格)，购买价格为出售价格的一半
   end;
   PTPricesInfo = ^TPricesInfo;  // TPricesInfo的指针类型

   { TClientGoods - 客户端商品记录
     功能: 存储商店中显示的商品信息
     用途: 在客户端商店界面显示商品列表 }
   TClientGoods = record
      Name        : string[14];  // 商品名称
      SubMenu     : byte;        // 子菜单分类
      Price       : integer;     // 商品价格
      Stock       : integer;     // 库存数量，对于独立物品则为服务器索引
      //Dura        : word;      // 耐久度(已注释)
      //DuraMax     : word;      // 最大耐久度(已注释)
      Grade     : ShortInt;      // 商品品质/状态
   end;
   PTClientGoods = ^TClientGoods;  // TClientGoods的指针类型

   { TClientItem - 客户端物品记录
     功能: 客户端使用的物品显示格式
     用途: 整合标准物品信息和实例信息用于客户端显示 }
   TClientItem = record
      S            : TStdItem;  // 标准物品信息(变更的能力值会应用到这里)
      MakeIndex    : integer;   // 物品制造索引
      Dura         : word;      // 当前耐久度
      DuraMax      : word;      // 最大耐久度
   end;
   PTClientItem = ^TClientItem;  // TClientItem的指针类型

   { TUserStateInfo - 用户状态信息记录
     功能: 存储用户的外观和装备状态
     用途: 用于查看其他玩家的装备信息 }
   TUserStateInfo = record
      Feature     : integer;     // 外观特征码
      UserName    : string[14];  // 用户名称
      NameColor   : integer;     // 名称颜色
      GuildName   : string[14];  // 行会名称
      GuildRankName : string[14]; // 行会职位名称
      // 2003/03/15 装备栏扩展 8->12
      UseItems : array[0..12] of TClientItem;  // 穿戴装备数组
   end;
   PTUserStateInfo = ^TUserStateInfo;  // TUserStateInfo的指针类型

   { TDropItem - 掉落物品记录
     功能: 客户端使用，表示地图上掉落的物品
     用途: 显示地面上的物品和闪烁效果 }
   TDropItem = record
      Id          : integer;     // 物品唯一标识
      X           : word;        // X坐标
      Y           : word;        // Y坐标
      Looks       : word;        // 物品外观图片编号
      FlashTime   : longword;    // 上次闪烁时间
      BoFlash     : Boolean;     // 是否正在闪烁
      FlashStepTime : longword;  // 闪烁步进时间
      FlashStep   : integer;     // 当前闪烁步骤
      Name        : string[14];  // 物品名称
   end;
   PTDropItem = ^TDropItem;  // TDropItem的指针类型

   { TDefMagic - 魔法定义记录
     功能: 定义游戏中魔法技能的基本属性
     用途: 作为魔法技能数据库的基础结构 }
   TDefMagic = record
      MagicId: word;              // 魔法技能ID
      MagicName: string[12];      // 魔法名称(待扩展)
      EffectType: byte;           // 效果类型
      Effect: byte;               // 效果编号
      Spell: word;                // 魔法消耗
      MinPower: word;             // 最小威力
      NeedLevel: array[0..3] of byte;    // 各等级需求的角色等级
      MaxTrain: array[0..3] of integer;  // 各等级最大修炼值
      MaxTrainLevel: byte;        // 最大修炼等级
      Job: byte;                  // 适用职业: 0=战士, 1=术士, 2=道士, 99=所有职业
      DelayTime: integer;         // 施法后到下次可施法的冷却时间(毫秒)
      DefSpell: byte;             // 默认魔法消耗
      DefMinPower: byte;          // 默认最小威力
      MaxPower: word;             // 最大威力
      DefMaxPower: byte;          // 默认最大威力
      Desc: string[15];           // 魔法描述
   end;
   PTDefMagic = ^TDefMagic;  // TDefMagic的指针类型

   { TUserMagic - 用户魔法记录
     功能: 存储玩家已学会的魔法技能信息
     用途: 管理玩家的技能列表和修炼进度 }
   TUserMagic = record
      pDef        : PTDefMagic;  // 魔法定义指针，必须不为nil
      MagicId     : word;        // 魔法ID，必须唯一且不变，始终大于0
      Level       : byte;        // 当前技能等级
      Key         : char;        // 用户指定的快捷键
      CurTrain    : integer;     // 当前修炼值
   end;
   PTUserMagic = ^TUserMagic;  // TUserMagic的指针类型

   { TClientMagic - 客户端魔法记录
     功能: 客户端使用的魔法显示格式
     用途: 在客户端技能界面显示魔法信息 }
   TClientMagic = record
      Key: char;           // 快捷键
      Level: byte;         // 技能等级
      CurTrain: integer;   // 当前修炼值
      Def: TDefMagic;      // 魔法定义(完整复制)
   end;
   PTClientMagic = ^TClientMagic;  // TClientMagic的指针类型

   { TFriend - 好友记录 (2003/04/15 新增)
     功能: 存储好友信息
     用途: 好友系统的数据存储 }
   TFriend = record
      CharID: String;   // 角色ID
      Status: Byte;     // 好友状态
      Memo  : String;   // 备注信息
   end;
   PTFriend = ^TFriend;  // TFriend的指针类型

   { TMail - 邮件记录 (2003/04/15 新增)
     功能: 存储邮件/私信信息
     用途: 邮件系统的数据存储 }
   TMail = record
      Sender: String;   // 发送者
      Date  : String;   // 发送日期
      Mail  : String;   // 邮件内容
      Status: Byte;     // 邮件状态
   end;
   PTMail = ^TMail;  // TMail的指针类型

   { TSkillInfo - 技能信息记录
     功能: 存储技能的基本信息
     用途: 技能数据的简化存储格式 }
   TSkillInfo = record
      SkillIndex  : word;     // 技能索引
      Reserved    : word;     // 保留字段
      CurTrain    : integer;  // 当前修炼值
   end;
   PTSkillInfo = ^TSkillInfo;  // TSkillInfo的指针类型

   { TMapItem - 地图物品记录
     功能: 存储地图上掉落的物品完整信息
     用途: 服务器端管理地图上的掉落物品 }
   TMapItem = record
      useritem: TUserItem;  // 用户物品数据
      Name: string[14];     // 物品名称
      Looks: word;          // 外观图片编号
      AniCount: byte;       // 动画帧数
      Reserved: byte;       // 保留字段
      Count: integer;       // 物品数量
      Ownership: TObject;   // 拾取权归属者(可以拾取此物品的人)
      Droptime: longword;   // 物品掉落时间
      Droper: TObject;      // 掉落者(人物或怪物)
   end;
   PTMapItem = ^TMapItem;  // TMapItem的指针类型

   { TVisibleItemInfo - 可见物品信息记录
     功能: 存储地图上可见物品的显示信息
     用途: 用于客户端显示地图上的物品 }
   TVisibleItemInfo = record
      check: byte;        // 检查标志
      x: word;            // X坐标
      y: word;            // Y坐标
      Id: longint;        // 物品唯一ID
      Name: string[14];   // 物品名称
      looks: word;        // 外观图片编号
   end;
   PTVisibleItemInfo = ^TVisibleItemInfo;  // TVisibleItemInfo的指针类型

   { TVisibleActor - 可见角色记录
     功能: 存储视野内可见的角色引用
     用途: 管理玩家视野内的其他角色 }
   TVisibleActor = record
      check: byte;     // 检查标志
      cret: TObject;   // 角色对象引用
   end;
   PTVisibleActor = ^TVisibleActor;  // TVisibleActor的指针类型

   { TMapEventInfo - 地图事件信息记录
     功能: 存储地图上的事件信息
     用途: 管理地图触发事件，需要激活后才会触发 }
   TMapEventInfo = record
      check: byte;           // 检查标志
      X: integer;            // X坐标
      Y: integer;            // Y坐标
      EventObject: TObject;  // 事件对象引用 (TMapEvent)
   end;
   PTMapEventInfo = ^TMapEventInfo;  // TMapEventInfo的指针类型

   { TGateInfo - 传送门信息记录
     功能: 存储地图传送门的配置
     用途: 定义传送门的目标位置 }
   TGateInfo = record
      GateType: byte;        // 传送门类型
      EnterEnvir: TObject;   // 目标环境对象 (TEnvironment)
      EnterX: integer;       // 目标X坐标
      EnterY: integer;       // 目标Y坐标
   end;
   PTGateInfo = ^TGateInfo;  // TGateInfo的指针类型

   { TAThing - 地图物件记录
     功能: 存储地图上的通用物件信息
     用途: 地图格子上的对象管理 }
   TAThing = record
      Shape  	: byte;      // 物件形态
      AObject : TObject;   // 物件对象引用
      ATime   : longword;  // 时间戳
   end;
   PTAThing = ^TAThing;  // TAThing的指针类型

   { TMapInfo - 地图信息记录
     功能: 存储地图格子的属性信息
     用途: 地图碰撞检测和区域管理 }
   TMapInfo = record
      MoveAttr	: byte;    // 移动属性: 0=可移动, 1=不可移动, 2=不可移动且不可飞越
      Door     : Boolean;  // 是否有门(OBJList中包含门对象)
      Area     : byte;     // 区域类型(城镇、修炼场等)
      Reserved : byte;     // 保留字段(未使用)
      OBJList	: TList;   // 物件列表(TAThing对象列表)
   end;
   PTMapInfo = ^TMapInfo;  // TMapInfo的指针类型


   { TUserEntryInfo - 用户注册信息记录
     功能: 存储用户账号注册时的基本信息
     用途: 在登录前用于账号注册和验证 }
   TUserEntryInfo = record
      LoginId  : string[10];   // 登录ID
      Password : string[10];   // 密码
      UserName : string[20];   // 用户真实姓名(*)
      SSNo     : string[14];   // 身份证号码(*) 格式:721109-1476110
      Phone    : string[14];   // 家庭电话号码
      Quiz     : string[20];   // 密保问题(*)
      Answer   : string[12];   // 密保答案(*)
      EMail    : string[40];   // 电子邮箱
   end;

   { TUserEntryAddInfo - 用户注册附加信息记录
     功能: 存储用户账号的附加信息
     用途: 补充用户注册信息 }
   TUserEntryAddInfo = record
      //temp     : array[0..14] of byte;  // 临时字段(已注释)
      Quiz2    : string[20];   // 第二密保问题(*)
      Answer2  : string[12];   // 第二密保答案(*)
      Birthday : string[10];   // 生日(*) 格式:1972/11/09
      MobilePhone: string[13]; // 手机号码 格式:017-6227-1234
      Memo1: string[20];       // 备注1(*)
      Memo2: string[20];       // 备注2(*)
   end;

   { TUserCharacterInfo - 用户角色信息记录
     功能: 存储角色的基本信息
     用途: 进入游戏世界前传递给用户的角色信息 }
   TUserCharacterInfo = record
      Name		: string[14];  // 角色名称
      Sex		: byte;        // 性别
      Hair     : byte;        // 发型
      Job      : byte;        // 职业: 0=战士, 1=术士, 2=道士
      Level	   : byte;        // 等级
      Feature	: integer;     // 外观特征码
   end;
   PTUserCharacterInfo = ^TUserCharacterInfo;  // TUserCharacterInfo的指针类型

   { TLoadHuman - 加载角色请求记录
     功能: 存储加载角色时需要的验证信息
     用途: 从数据库加载角色数据时使用 }
   TLoadHuman = packed record
      UsrId: array [0..20] of char;     // 用户ID
      ChrName: array [0..19] of char;   // 角色名称 (13->19扩展)
      UsrAddr: array [0..14] of char;   // 用户地址/IP
      CertifyCode: integer;             // 验证码
   end;
   PTLoadHuman = ^TLoadHuman;  // TLoadHuman的指针类型

   { TMonsterInfo - 怪物信息记录
     功能: 定义游戏中怪物的属性
     用途: 作为怪物数据库的基础结构 }
   TMonsterInfo = record
      Name: string[14];     // 怪物名称
      Race: byte;           // 种族类型(服务器AI程序标识)
      RaceImg: byte;        // 种族图像(客户端帧识别)
      Appr: word;           // 外观图像编号
      Level: byte;          // 怪物等级
      LifeAttrib: byte;     // 生命属性
      CoolEye: byte;        // 洞察力(100%=必定看穿隐身，50%=50%概率看穿隐身)
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
      WalkSpeed: word;      // 移动速度
      WalkStep: word;       // 移动步数
      WalkWait: word;       // 移动等待时间
      AttackSpeed: word;    // 攻击速度
      ItemList: TList;      // 掉落物品列表
   end;
   PTMonsterInfo = ^TMonsterInfo;  // TMonsterInfo的指针类型

   { TZenInfo - 刷怪点信息记录
     功能: 定义怪物刷新点的配置
     用途: 服务器怪物刷新系统使用 }
   TZenInfo = record
      MapName:  string[14];   // 地图名称
      X: integer;             // 中心X坐标
      Y: integer;             // 中心Y坐标
      MonName: string[14];    // 怪物名称
      MonRace: integer;       // 怪物种族
      Area: integer;          // 刷新范围(+area/-area形成矩形区域)
      Count: integer;         // 刷新数量
      ZenTime: longword;      // 刷新间隔时间(毫秒)
      StartTime: longword;    // 开始时间
      Mons: TList;            // 已刷新的怪物列表
      SmallZenRate: integer;  // 小规模刷新概率
   end;
   PTZenInfo = ^TZenInfo;  // TZenInfo的指针类型

   { TMonItemInfo - 怪物掉落物品信息记录
     功能: 定义怪物掉落物品的概率配置
     用途: 计算怪物死亡时的物品掉落 }
   TMonItemInfo = record
      SelPoint: integer;      // 选择点数(用于概率计算)
      MaxPoint: integer;      // 最大点数
      ItemName: string[14];   // 物品名称
      Count: integer;         // 物品数量
   end;
   PTMonItemInfo = ^TMonItemInfo;  // TMonItemInfo的指针类型

   { TMarketProduct - 市场商品记录
     功能: 定义市场/商店的商品刷新配置
     用途: 商店系统的商品库存管理 }
   TMarketProduct = record
      GoodsName: string[14];  // 商品名称
      Count: integer;         // 商品数量
      ZenHour: integer;       // 刷新间隔(小时)
      ZenTime: longword;      // 最近刷新时间
   end;
   PTMarketProduct = ^TMarketProduct;  // TMarketProduct的指针类型

   { TQDDinfo - 任务日记信息记录
     功能: 存储任务日记的内容
     用途: 任务系统的日记显示 }
   TQDDinfo = record
      Index: integer;      // 日记索引
      Title: string;       // 日记标题
      SList: TStringList;  // 日记内容列表
   end;
   PTQDDinfo = ^TQDDinfo;  // TQDDinfo的指针类型


{ ============================================================================
  游戏配置常量
  用途: 定义游戏系统的各种限制和配置参数
============================================================================ }
const
   DEFBLOCKSIZE  = 16;  // 默认数据块大小

{ MIR2EI版本的容量配置 }
{$ifdef MIR2EI}
   MAXBAGITEM = 46;          // 背包最大物品数量
   MAXHORSEBAG = 30;         // 马匹背包最大物品数量
   MAXUSERMAGIC = 20;        // 玩家最大魔法技能数量
   MAXSAVEITEM = 50;         // 仓库最大存储物品数量
   MAXQUESTINDEXBYTE = 24;   // 任务索引字节数(EI版)
   MAXQUESTBYTE = 176;       // 任务数据字节数(EI版)

{ 传奇2标准版本的容量配置 }
{$else}
   MAXBAGITEM = 46;          // 背包最大物品数量
   MAXHORSEBAG = 30;         // 马匹背包最大物品数量
   MAXUSERMAGIC = 20;        // 玩家最大魔法技能数量
   MAXSAVEITEM = 50;         // 仓库最大存储物品数量
   MAXQUESTINDEXBYTE = 24;   // 任务索引字节数
   MAXQUESTBYTE = 176;       // 任务数据字节数
{$endif}

   { 客户端地图显示常量 }
   LOGICALMAPUNIT    = 40;   // 逻辑地图单元大小
   UNITX             = 48;   // 地图格子X方向像素宽度
   UNITY             = 32;   // 地图格子Y方向像素高度
   HALFX             = 24;   // X方向半格像素 (UNITX/2)
   HALFY             = 16;   // Y方向半格像素 (UNITY/2)

   { 地图对象类型常量 }
   OS_MOVINGOBJECT   = 1;    // 可移动对象(角色、怪物等)
   OS_ITEMOBJECT     = 2;    // 物品对象
   OS_EVENTOBJECT    = 3;    // 事件对象
   OS_GATEOBJECT     = 4;    // 传送门对象
   OS_SWITCHOBJECT   = 5;    // 开关对象
   OS_MAPEVENT       = 6;    // 地图事件
   OS_DOOR           = 7;    // 门对象
   OS_ROON           = 8;    // 符文对象


   { 中毒和状态效果常量 }
   POISON_DECHEALTH     = 0;    // 持续减少生命值(绿毒)
   POISON_DAMAGEARMOR   = 1;    // 降低防御(红毒)
   STATE_BLUECHAR       = 2;    // 蓝名状态
   //POISON_
   POISON_DONTMOVE      = 4;    // 麻痹/无法移动
   POISON_STONE         = 5;    // 石化状态
   STATE_TRANSPARENT    = 8;    // 透明/隐身状态
   STATE_DEFENCEUP      = 9;    // 物理防御提升状态
   STATE_MAGDEFENCEUP   = 10;   // 魔法防御提升状态
   STATE_BUBBLEDEFENCEUP = 11;  // 护盾防御提升状态

   { 临时能力提升常量 - 用于临时提升角色属性(持续一定时间) }
   EABIL_DCUP = 0;       // 物理攻击力提升
   EABIL_MCUP = 1;       // 魔法攻击力提升
   EABIL_SCUP = 2;       // 道术攻击力提升
   EABIL_HITSPEEDUP = 3; // 攻击速度提升
   EABIL_HPUP = 4;       // 生命值提升
   EABIL_MPUP = 5;       // 魔法值提升

   { ItemDesc物品描述标志常量 }
   IDC_UNIDENTIFIED     = $01;   // 未鉴定状态，能力未确认
   IDC_UNABLETAKEOFF    = $02;   // 无法脱下，使用未知数可解除
   IDC_NEVERTAKEOFF     = $04;   // 永远无法脱下
   IDC_DIEANDBREAK      = $08;   // 死亡时损坏
   IDC_NEVERLOSE        = $10;   // 死亡时不掉落

   { 怪物状态模式常量 }
   STATE_STONE_MODE     = $00000001;  // 石像怪物形态(处于石像状态)
   STATE_OPENHEATH      = $00000002;  // 生命值公开状态

   { 攻击模式常量 (Hit Attack Mode) }
   HAM_ALL              = 0;  // 全体攻击模式
   HAM_PEACE            = 1;  // 和平模式，只攻击怪物
   HAM_GROUP            = 2;  // 编组模式，攻击编组外的任何人
   HAM_GUILD            = 3;  // 行会模式，攻击行会外的任何人
   HAM_PKATTACK         = 4;  // PK模式，红名攻击白名
   HAM_MAXCOUNT         = 5;  // 攻击模式数量

   { 区域类型常量 }
   AREA_FIGHT        = $01;   // 战斗区域
   AREA_SAFE         = $02;   // 安全区域
   AREA_FREEPK       = $04;   // 自由PK区域

   { 攻击方式常量 (Hit Mode) }
   HM_HIT            = 0;     // 普通攻击
   HM_HEAVYHIT       = 1;     // 重击
   HM_BIGHIT         = 2;     // 大力击
   HM_POWERHIT       = 3;     // 烈火剑法
   HM_LONGHIT        = 4;     // 刺杀剑法
   HM_WIDEHIT        = 5;     // 半月弯刀
   // 2003/03/15 新增武功
   HM_CROSSHIT       = 6;     // 狂风斩(攻击周围8个方向)
   HM_FIREHIT        = 7;     // 烈火剑法(新版)

   {----------------------------}
   { ============================================================================
     服务器消息常量 (SM_* = Server Message)
     用途: 服务器发送给客户端的消息类型标识
     范围: 1 ~ 2000
   ============================================================================ }

   SM_TEST                 = 1;   // 测试消息

   { 流程控制命令 }
   SM_STOPACTIONS          = 2;   // 停止所有角色/魔法的动作(如进入其他地图时)

   { 动作相关命令 }
   SM_ACTION_MIN           = 5;   // 动作消息最小值
   SM_THROW                = 5;   // 投掷
   SM_RUSH                 = 6;   // 向前冲锋
   SM_RUSHKUNG             = 7;   // 冲锋失败
   SM_FIREHIT              = 8;   // 烈火剑法
   SM_BACKSTEP             = 9;   // 后退
   SM_TURN                 = 10;  // 转身
   SM_WALK                 = 11;  // 行走
   SM_SITDOWN              = 12;  // 坐下
   SM_RUN                  = 13;  // 跑步
   SM_HIT                  = 14;  // 普通攻击
   SM_HEAVYHIT             = 15;  // 重击
   SM_BIGHIT               = 16;  // 大力击
   SM_SPELL                = 17;  // 施法
   SM_POWERHIT             = 18;  // 烈火剑法
   SM_LONGHIT              = 19;  // 刺杀剑法
   SM_DIGUP                = 20;  // 从地下钻出
   SM_DIGDOWN              = 21;  // 钻入地下隐藏
   SM_FLYAXE               = 22;  // 飞斧
   SM_LIGHTING             = 23;  // 使用魔法
   SM_WIDEHIT              = 24;  // 半月弯刀
   SM_ACTION_MAX           = 25;  // 动作消息最大值
   // 2003/03/15 新增武功
   SM_CROSSHIT             = 35;  // 狂风斩，攻击周围8格

   SM_ACTION2_MIN          = 1000;  // 扩展动作消息最小值
   //SM_READYFIREHIT         = 1000;  // 烈火准备(仅客户端使用，已注释)
   SM_ACTION2_MAX          = 1099;  // 扩展动作消息最大值

   { 生死状态消息 }
   SM_DIE                  = 26;  // 消失/死亡
   SM_ALIVE                = 27;  // 复活
   SM_MOVEFAIL             = 28;  // 移动失败
   SM_HIDE                 = 29;  // 隐藏
   SM_DISAPPEAR            = 30;  // 消失
   SM_STRUCK               = 31;  // 被击中
   SM_DEATH                = 32;  // 死亡
   SM_SKELETON             = 33;  // 变为骷髅(尸体腐烂)
   SM_NOWDEATH             = 34;  // 立即死亡

   { 角色信息消息 }
   SM_HEAR                 = 40;  // 听到(聊天消息)
   SM_FEATURECHANGED       = 41;  // 外观改变
   SM_USERNAME             = 42;  // 用户名
   SM_WINEXP               = 44;  // 获得经验
   SM_LEVELUP              = 45;  // 升级
   SM_DAYCHANGING          = 46;  // 昼夜变化

   { 登录和地图消息 }
   SM_LOGON                = 50;  // 登录成功
   SM_NEWMAP               = 51;  // 进入新地图
   SM_ABILITY              = 52;  // 能力值更新
   SM_HEALTHSPELLCHANGED   = 53;  // 生命值/魔法值改变
   SM_MAPDESCRIPTION       = 54;  // 地图描述

   { 聊天消息 }
   SM_SYSMESSAGE           = 100;  // 系统消息
   SM_GROUPMESSAGE         = 101;  // 组队消息
   SM_CRY                  = 102;  // 喊话
   SM_WHISPER              = 103;  // 私聊
   SM_GUILDMESSAGE         = 104;  // 行会消息

   { 物品相关消息 }
   SM_ADDITEM              = 200;  // 获得新物品(Series=数量)
   SM_BAGITEMS             = 201;  // 背包所有物品
   SM_DELITEM              = 202;  // 物品消失(磨损等原因)
   SM_UPDATEITEM           = 203;  // 物品属性变化

   { 魔法相关消息 }
   SM_ADDMAGIC             = 210;  // 学会新魔法
   SM_SENDMYMAGIC          = 211;  // 发送我的魔法列表
   SM_DELMAGIC             = 212;  // 删除魔法

   { 版本和认证消息 }
   SM_VERSION_AVAILABLE    = 500;  // 版本可用
   SM_VERSION_FAIL         = 501;  // 版本检查失败
   SM_PASSWD_SUCCESS       = 502;  // 密码验证成功
   SM_PASSWD_FAIL          = 503;  // 密码验证失败
   SM_NEWID_SUCCESS        = 504;  // 新账号创建成功
   SM_NEWID_FAIL           = 505;  // 新账号创建失败
   SM_CHGPASSWD_SUCCESS    = 506;  // 修改密码成功
   SM_CHGPASSWD_FAIL       = 507;  // 修改密码失败

   { 角色管理消息 }
   SM_QUERYCHR             = 520;  // 查询角色列表
   SM_NEWCHR_SUCCESS       = 521;  // 创建角色成功
   SM_NEWCHR_FAIL          = 522;  // 创建角色失败
   SM_DELCHR_SUCCESS       = 523;  // 删除角色成功
   SM_DELCHR_FAIL          = 524;  // 删除角色失败
   SM_STARTPLAY            = 525;  // 开始游戏
   SM_STARTFAIL            = 526;  // 开始游戏失败
   SM_QUERYCHR_FAIL        = 527;  // 查询角色失败
   SM_OUTOFCONNECTION      = 528;  // 连接断开
   SM_PASSOK_SELECTSERVER  = 529;  // 密码正确，选择服务器
   SM_SELECTSERVER_OK      = 530;  // 选择服务器成功
   SM_NEEDUPDATE_ACCOUNT   = 531;  // 需要更新账号信息
   SM_UPDATEID_SUCCESS     = 532;  // 更新账号成功
   SM_UPDATEID_FAIL        = 533;  // 更新账号失败
   SM_PASSOK_WRONGSSN      = 534;  // 密码正确但身份证错误
   SM_NOT_IN_SERVICE       = 535;  // 服务不可用


   { 物品操作消息 }
   SM_DROPITEM_SUCCESS     = 600;  // 丢弃物品成功
   SM_DROPITEM_FAIL        = 601;  // 丢弃物品失败
   SM_ITEMSHOW             = 610;  // 物品显示
   SM_ITEMHIDE             = 611;  // 物品隐藏
   SM_OPENDOOR_OK          = 612;  // 开门成功
   SM_OPENDOOR_LOCK        = 613;  // 门被锁定
   SM_CLOSEDOOR            = 614;  // 关门

   { 装备操作消息 }
   SM_TAKEON_OK            = 615;  // 穿戴装备成功
   SM_TAKEON_FAIL          = 616;  // 穿戴装备失败
   SM_EXCHGTAKEON_OK       = 617;  // 交换装备成功(左右手)
   SM_EXCHGTAKEON_FAIL     = 618;  // 交换装备失败
   SM_TAKEOFF_OK           = 619;  // 脱下装备成功
   SM_TAKEOFF_FAIL         = 620;  // 脱下装备失败
   SM_SENDUSEITEMS         = 621;  // 发送所有穿戴装备
   SM_WEIGHTCHANGED        = 622;  // 负重改变

   { 地图和状态消息 }
   SM_CLEAROBJECTS         = 633;  // 清除对象
   SM_CHANGEMAP            = 634;  // 切换地图
   SM_EAT_OK               = 635;  // 使用物品成功
   SM_EAT_FAIL             = 636;  // 使用物品失败
   SM_BUTCH                = 637;  // 挖取尸体

   { 魔法和效果消息 }
   SM_MAGICFIRE            = 638;  // 魔法发射 (CM_SPELL -> SM_SPELL + SM_MAGICFIRE)
   SM_MAGICFIRE_FAIL       = 639;  // 魔法发射失败
   SM_MAGIC_LVEXP          = 640;  // 魔法等级经验
   SM_SOUND                = 641;  // 播放音效
   SM_DURACHANGE           = 642;  // 耐久度改变

   { NPC商店消息 }
   SM_MERCHANTSAY          = 643;  // NPC对话
   SM_MERCHANTDLGCLOSE     = 644;  // NPC对话关闭
   SM_SENDGOODSLIST        = 645;  // 发送商品列表
   SM_SENDUSERSELL         = 646;  // 发送用户出售信息
   SM_SENDBUYPRICE         = 647;  // 发送购买价格
   SM_USERSELLITEM_OK      = 648;  // 出售物品成功
   SM_USERSELLITEM_FAIL    = 649;  // 出售物品失败
   SM_BUYITEM_SUCCESS      = 650;  // 购买物品成功
   SM_BUYITEM_FAIL         = 651;  // 购买物品失败
   SM_SENDDETAILGOODSLIST  = 652;  // 发送详细商品列表
   SM_GOLDCHANGED          = 653;  // 金币改变
   SM_CHANGELIGHT          = 654;  // 光照改变
   SM_LAMPCHANGEDURA       = 655;  // 灯光耐久改变
   SM_CHANGENAMECOLOR      = 656;  // 名字颜色改变
   SM_CHARSTATUSCHANGED    = 657;  // 角色状态改变
   SM_SENDNOTICE           = 658;  // 发送公告

   { 组队消息 }
   SM_GROUPMODECHANGED     = 659;  // 组队模式改变
   SM_CREATEGROUP_OK       = 660;  // 创建队伍成功
   SM_CREATEGROUP_FAIL     = 661;  // 创建队伍失败
   SM_GROUPADDMEM_OK       = 662;  // 添加队员成功
   SM_GROUPDELMEM_OK       = 663;  // 移除队员成功
   SM_GROUPADDMEM_FAIL     = 664;  // 添加队员失败
   SM_GROUPDELMEM_FAIL     = 665;  // 移除队员失败
   SM_GROUPCANCEL          = 666;  // 队伍解散
   SM_GROUPMEMBERS         = 667;  // 队伍成员列表

   { 修理消息 }
   SM_SENDUSERREPAIR       = 668;  // 发送用户修理信息
   SM_USERREPAIRITEM_OK    = 669;  // 修理物品成功
   SM_USERREPAIRITEM_FAIL  = 670;  // 修理物品失败
   SM_SENDREPAIRCOST       = 671;  // 发送修理费用

   { 交易消息 }
   SM_DEALMENU             = 673;  // 交易菜单
   SM_DEALTRY_FAIL         = 674;  // 发起交易失败
   SM_DEALADDITEM_OK       = 675;  // 添加交易物品成功
   SM_DEALADDITEM_FAIL     = 676;  // 添加交易物品失败
   SM_DEALDELITEM_OK       = 677;  // 移除交易物品成功
   SM_DEALDELITEM_FAIL     = 678;  // 移除交易物品失败
   //SM_DEALREMOTEADDITEM_OK = 679;  // (已注释)
   //SM_DEALREMOTEDELITEM_OK = 680;  // (已注释)
   SM_DEALCANCEL           = 681;  // 交易取消
   SM_DEALREMOTEADDITEM    = 682;  // 对方添加交易物品
   SM_DEALREMOTEDELITEM    = 683;  // 对方移除交易物品
   SM_DEALCHGGOLD_OK       = 684;  // 修改交易金币成功
   SM_DEALCHGGOLD_FAIL     = 685;  // 修改交易金币失败
   SM_DEALREMOTECHGGOLD    = 686;  // 对方修改交易金币
   SM_DEALSUCCESS          = 687;  // 交易成功

   { 仓库消息 }
   SM_SENDUSERSTORAGEITEM  = 700;  // 发送用户仓库物品
   SM_STORAGE_OK           = 701;  // 存储成功
   SM_STORAGE_FULL         = 702;  // 仓库已满
   SM_STORAGE_FAIL         = 703;  // 存储失败
   SM_SAVEITEMLIST         = 704;  // 保存物品列表
   SM_TAKEBACKSTORAGEITEM_OK = 705;    // 取回仓库物品成功
   SM_TAKEBACKSTORAGEITEM_FAIL = 706;  // 取回仓库物品失败
   SM_TAKEBACKSTORAGEITEM_FULLBAG = 707;  // 背包已满无法取回
   SM_AREASTATE            = 708;  // 区域状态(安全/对练/普通)
   SM_DELITEMS             = 709;  // 删除多个物品
   SM_READMINIMAP_OK       = 710;  // 读取小地图成功
   SM_READMINIMAP_FAIL     = 711;  // 读取小地图失败

   { 制药消息 }
   SM_SENDUSERMAKEDRUGITEMLIST = 712;  // 发送制药材料列表
   SM_MAKEDRUG_SUCCESS     = 713;  // 制药成功
   SM_MAKEDRUG_FAIL        = 714;  // 制药失败
   SM_ALLOWPOWERHIT        = 715;  // 允许烈火攻击
   SM_NORMALEFFECT         = 716;  // 普通效果

   { 行会消息 }
   SM_CHANGEGUILDNAME      = 750;  // 行会名称或职位名称改变
   SM_SENDUSERSTATE        = 751;  // 发送用户状态
   SM_SUBABILITY           = 752;  // 辅助能力
   SM_OPENGUILDDLG         = 753;  // 打开行会对话框
   SM_OPENGUILDDLG_FAIL    = 754;  // 打开行会对话框失败
   SM_SENDGUILDHOME        = 755;  // 发送行会主页
   SM_SENDGUILDMEMBERLIST  = 756;  // 发送行会成员列表
   SM_GUILDADDMEMBER_OK    = 757;  // 添加行会成员成功
   SM_GUILDADDMEMBER_FAIL  = 758;  // 添加行会成员失败
   SM_GUILDDELMEMBER_OK    = 759;  // 移除行会成员成功
   SM_GUILDDELMEMBER_FAIL  = 760;  // 移除行会成员失败
   SM_GUILDRANKUPDATE_FAIL = 761;  // 更新行会职位失败
   SM_BUILDGUILD_OK        = 762;  // 创建行会成功
   SM_BUILDGUILD_FAIL      = 763;  // 创建行会失败
   SM_DONATE_FAIL          = 764;  // 捐献失败
   SM_DONATE_OK            = 765;  // 捐献成功
   SM_MYSTATUS             = 766;  // 我的状态
   SM_MENU_OK              = 767;  // 菜单确认(通过description传递消息)
   SM_GUILDMAKEALLY_OK     = 768;  // 建立行会联盟成功
   SM_GUILDMAKEALLY_FAIL   = 769;  // 建立行会联盟失败
   SM_GUILDBREAKALLY_OK    = 770;  // 解除行会联盟成功
   SM_GUILDBREAKALLY_FAIL  = 771;  // 解除行会联盟失败
   SM_DLGMSG               = 772;  // 对话框消息

   { 传送和特效消息 }
   SM_SPACEMOVE_HIDE       = 800;  // 瞬间移动消失
   SM_SPACEMOVE_SHOW       = 801;  // 瞬间移动出现
   SM_RECONNECT            = 802;  // 重新连接
   SM_GHOST                = 803;  // 屏幕上显示的残影
   SM_SHOWEVENT            = 804;  // 显示事件
   SM_HIDEEVENT            = 805;  // 隐藏事件
   SM_SPACEMOVE_HIDE2      = 806;  // 瞬间移动消失(类型2)
   SM_SPACEMOVE_SHOW2      = 807;  // 瞬间移动出现(类型2)

   { 系统消息 }
   SM_TIMECHECK_MSG        = 810;  // 客户端时间检查
   SM_ADJUST_BONUS         = 811;  // 调整奖励点数

   { 好友系统消息 }
   SM_FRIEND_DELETE        = 812;  // 删除好友
   SM_FRIEND_INFO          = 813;  // 好友信息添加/变更
   SM_FRIEND_RESULT        = 814;  // 好友操作结果

   { 私信系统消息 }
   SM_TAG_ALARM            = 815;  // 新私信提醒
   SM_TAG_LIST             = 816;  // 私信列表
   SM_TAG_INFO             = 817;  // 私信信息变更
   SM_TAG_REJECT_LIST      = 818;  // 屏蔽名单列表
   SM_TAG_REJECT_ADD       = 819;  // 添加屏蔽
   SM_TAG_REJECT_DELETE    = 820;  // 移除屏蔽
   SM_TAG_RESULT           = 821;  // 私信操作结果

   { 用户系统消息 }
   SM_USER_INFO            = 822;  // 用户在线状态和地图信息

   // 1000 ~ 1099 预留给动作消息

   { 扩展功能消息 }
   SM_OPENHEALTH           = 1100;  // 显示生命值给对方
   SM_CLOSEHEALTH          = 1101;  // 隐藏生命值
   SM_BREAKWEAPON          = 1102;  // 武器损坏
   SM_INSTANCEHEALGUAGE    = 1103;  // 即时治疗量表
   SM_CHANGEFACE           = 1104;  // 变身
   SM_NEXTTIME_PASSWORD    = 1105;  // 下次登录需要密码
   SM_CHECK_CLIENTVALID    = 1106;  // 检查客户端有效性

   SM_PLAYDICE             = 1200;  // 掷骰子
   // 2003/02/11 组队成员位置信息
   SM_GROUPPOS             = 1312;  // 组队成员位置


   { ============================================================================
     客户端消息常量 (CM_* = Client Message)
     用途: 客户端发送给服务器的消息类型标识
     范围: 2000 ~ 4000
   ============================================================================ }

   { 登录认证消息 }
   CM_PROTOCOL             = 2000;  // 协议版本
   CM_IDPASSWORD           = 2001;  // 账号密码
   CM_ADDNEWUSER           = 2002;  // 注册新用户
   CM_CHANGEPASSWORD       = 2003;  // 修改密码
   CM_UPDATEUSER           = 2004;  // 更新用户信息

   {----------------------------}

   { 角色管理消息 }
   CM_QUERYCHR             = 100;   // 查询角色列表
   CM_NEWCHR               = 101;   // 创建新角色
   CM_DELCHR               = 102;   // 删除角色
   CM_SELCHR               = 103;   // 选择角色
   CM_SELECTSERVER         = 104;   // 选择服务器(附带服务器名)

   { 动作消息 (3000-3099 预留给移动消息)
     规则: CM_TURN - 3000 = SM_TURN，必须遵守此规则 }
   CM_THROW                = 3005;  // 投掷
   CM_TURN                 = 3010;  // 转身
   CM_WALK                 = 3011;  // 行走
   CM_SITDOWN              = 3012;  // 坐下
   CM_RUN                  = 3013;  // 跑步
   CM_HIT                  = 3014;  // 普通攻击
   CM_HEAVYHIT             = 3015;  // 重击
   CM_BIGHIT               = 3016;  // 大力击
   CM_SPELL                = 3017;  // 施法
   CM_POWERHIT             = 3018;  // 烈火剑法
   CM_LONGHIT              = 3019;  // 刺杀剑法
   CM_WIDEHIT              = 3024;  // 半月弯刀
   CM_FIREHIT              = 3025;  // 烈火攻击
   CM_SAY                  = 3030;  // 说话
   // 2003/03/15 新增武功
   CM_CROSSHIT             = 3035;  // 狂风斩

   { 查询消息 }
   CM_QUERYUSERNAME        = 80;    // 查询用户名
   CM_QUERYBAGITEMS        = 81;    // 查询背包物品
   CM_QUERYUSERSTATE       = 82;    // 查询其他玩家状态

   { 物品操作消息 }
   CM_DROPITEM             = 1000;  // 丢弃物品
   CM_PICKUP               = 1001;  // 拾取物品
   CM_OPENDOOR             = 1002;  // 开门
   CM_TAKEONITEM           = 1003;  // 穿戴装备
   CM_TAKEOFFITEM          = 1004;  // 脱下装备
   CM_EXCHGTAKEONITEM      = 1005;  // 交换装备位置(戒指/手镯左右互换)
   CM_EAT                  = 1006;  // 使用物品(吃/喝)
   CM_BUTCH                = 1007;  // 挖取尸体
   CM_MAGICKEYCHANGE       = 1008;  // 修改魔法快捷键
   CM_SOFTCLOSE            = 1009;  // 软关闭(正常退出)
   CM_CLICKNPC             = 1010;  // 点击NPC
   CM_MERCHANTDLGSELECT    = 1011;  // NPC对话选择
   CM_MERCHANTQUERYSELLPRICE = 1012; // 查询出售价格
   CM_USERSELLITEM         = 1013;  // 出售物品
   CM_USERBUYITEM          = 1014;  // 购买物品
   CM_USERGETDETAILITEM    = 1015;  // 获取物品详情
   CM_DROPGOLD             = 1016;  // 丢弃金币
   CM_TEST                 = 1017;  // 测试消息
   CM_LOGINNOTICEOK        = 1018;  // 登录公告确认

   { 组队消息 }
   CM_GROUPMODE            = 1019;  // 组队模式
   CM_CREATEGROUP          = 1020;  // 创建队伍
   CM_ADDGROUPMEMBER       = 1021;  // 添加队员
   CM_DELGROUPMEMBER       = 1022;  // 移除队员

   { 修理和交易消息 }
   CM_USERREPAIRITEM       = 1023;  // 修理物品
   CM_MERCHANTQUERYREPAIRCOST = 1024; // 查询修理费用
   CM_DEALTRY              = 1025;  // 发起交易
   CM_DEALADDITEM          = 1026;  // 添加交易物品
   CM_DEALDELITEM          = 1027;  // 移除交易物品
   CM_DEALCANCEL           = 1028;  // 取消交易
   CM_DEALCHGGOLD          = 1029;  // 修改交易金币
   CM_DEALEND              = 1030;  // 确认交易

   { 仓库消息 }
   CM_USERSTORAGEITEM      = 1031;  // 存入仓库
   CM_USERTAKEBACKSTORAGEITEM = 1032; // 取回仓库物品
   CM_WANTMINIMAP          = 1033;  // 请求小地图
   CM_USERMAKEDRUGITEM     = 1034;  // 制药

   { 行会消息 }
   CM_OPENGUILDDLG         = 1035;  // 打开行会对话框
   CM_GUILDHOME            = 1036;  // 行会主页
   CM_GUILDMEMBERLIST      = 1037;  // 行会成员列表
   CM_GUILDADDMEMBER       = 1038;  // 添加行会成员
   CM_GUILDDELMEMBER       = 1039;  // 移除行会成员
   CM_GUILDUPDATENOTICE    = 1040;  // 更新行会公告
   CM_GUILDUPDATERANKINFO  = 1041;  // 更新行会职位信息
   CM_SPEEDHACKUSER        = 1042;  // 加速外挂检测
   CM_ADJUST_BONUS         = 1043;  // 调整奖励点
   CM_GUILDMAKEALLY        = 1044;  // 建立行会联盟
   CM_GUILDBREAKALLY       = 1045;  // 解除行会联盟

   { 好友系统消息 }
   CM_FRIEND_ADD           = 1046;  // 添加好友
   CM_FRIEND_DELETE        = 1047;  // 删除好友
   CM_FRIEND_EDIT          = 1048;  // 修改好友备注
   CM_FRIEND_LIST          = 1049;  // 请求好友列表

   { 私信系统消息 }
   CM_TAG_ADD              = 1050;  // 发送私信
   CM_TAG_DELETE           = 1051;  // 删除私信
   CM_TAG_SETINFO          = 1052;  // 修改私信状态
   CM_TAG_LIST             = 1053;  // 请求私信列表
   CM_TAG_NOTREADCOUNT     = 1054;  // 请求未读私信数量
   CM_TAG_REJECT_LIST      = 1055;  // 请求屏蔽名单
   CM_TAG_REJECT_ADD       = 1056;  // 添加屏蔽
   CM_TAG_REJECT_DELETE    = 1057;  // 移除屏蔽

   CM_CLIENT_CHECKTIME     = 1100;  // 客户端时间检查


   {----------------------------}
   { ============================================================================
     内部路由消息常量 (RM_* = Route Message)
     用途: 服务器内部使用的消息类型标识，用于对象间通信
     范围: 10000+
   ============================================================================ }

   { 动作消息 }
   RM_TURN                 = 10001;  // 转身
   RM_WALK                 = 10002;  // 行走
   RM_RUN                  = 10003;  // 跑步
   RM_HIT                  = 10004;  // 普通攻击
   RM_HEAVYHIT             = 10005;  // 重击
   RM_BIGHIT               = 10006;  // 大力击
   RM_SPELL                = 10007;  // 施法
   RM_POWERHIT             = 10008;  // 烈火剑法
   RM_SITDOWN              = 10009;  // 坐下
   RM_MOVEFAIL             = 10010;  // 移动失败
   RM_LONGHIT              = 10011;  // 刺杀剑法
   RM_WIDEHIT              = 10012;  // 半月弯刀
   RM_PUSH                 = 10013;  // 推开
   RM_FIREHIT              = 10014;  // 烈火攻击
   RM_RUSH                 = 10015;  // 冲锋
   RM_RUSHKUNG             = 10016;  // 冲锋失败
   // 2003/03/15 新增武功
   RM_CROSSHIT             = 10017;  // 狂风斩
   RM_DECREFOBJCOUNT       = 10018;  // 减少对象引用计数

   { 战斗状态消息 }
   RM_STRUCK               = 10020;  // 被击中
   RM_DEATH                = 10021;  // 死亡
   RM_DISAPPEAR            = 10022;  // 消失
//   RM_HIDE                 = 10023;  // 隐藏(已注释)
   RM_SKELETON             = 10024;  // 变为骷髅
   RM_MAGSTRUCK            = 10025;  // 魔法击中(此时扣除生命值)
   RM_MAGHEALING           = 10026;  // 魔法治疗
   RM_STRUCK_MAG           = 10027;  // 被魔法击中
   RM_MAGSTRUCK_MINE       = 10028;  // 踩中地雷

   { 聊天消息 }
   RM_HEAR                 = 10030;  // 听到(聊天)
   RM_WHISPER              = 10031;  // 私聊
   RM_CRY                  = 10032;  // 喊话

   { 状态更新消息 }
   RM_LOGON                = 10050;  // 登录
   RM_ABILITY              = 10051;  // 能力值
   RM_HEALTHSPELLCHANGED   = 10052;  // 生命/魔法值改变
   RM_DAYCHANGING          = 10053;  // 昼夜变化
   RM_USERNAME             = 10043;  // 用户名
   RM_WINEXP               = 10044;  // 获得经验
   RM_LEVELUP              = 10045;  // 升级
   RM_CHANGENAMECOLOR      = 10046;  // 名字颜色改变

   { 系统消息 }
   RM_SYSMESSAGE           = 10100;  // 系统消息
   RM_REFMESSAGE           = 10101;  // 引用消息
   RM_GROUPMESSAGE         = 10102;  // 组队消息
   RM_SYSMESSAGE2          = 10103;  // 系统消息2
   RM_GUILDMESSAGE         = 10104;  // 行会消息
   RM_SYSMSG_BLUE          = 10105;  // 蓝色系统消息

   { 物品和场景消息 }
   RM_ITEMSHOW             = 10110;  // 物品显示
   RM_ITEMHIDE             = 10111;  // 物品隐藏
   RM_OPENDOOR_OK          = 10112;
   RM_CLOSEDOOR            = 10113;
   RM_SENDUSEITEMS         = 10114;
   RM_WEIGHTCHANGED        = 10115;
   RM_FEATURECHANGED       = 10116;
   RM_CLEAROBJECTS         = 10117;
   RM_CHANGEMAP            = 10118;
   RM_BUTCH                = 10119; //
   RM_MAGICFIRE            = 10120;
   RM_MAGICFIRE_FAIL       = 10121;
   RM_SENDMYMAGIC          = 10122;
   RM_MAGIC_LVEXP          = 10123;
   RM_SOUND                = 10124;
   RM_DURACHANGE           = 10125;
   RM_MERCHANTSAY          = 10126;
   RM_MERCHANTDLGCLOSE     = 10127;
   RM_SENDGOODSLIST        = 10128;
   RM_SENDUSERSELL         = 10129;
   RM_SENDBUYPRICE         = 10130;  //상점에서 사용자의 아이템을 사는 가격
   RM_USERSELLITEM_OK      = 10131;
   RM_USERSELLITEM_FAIL    = 10132;
   RM_BUYITEM_SUCCESS      = 10133;
   RM_BUYITEM_FAIL         = 10134;
   RM_SENDDETAILGOODSLIST  = 10135;
   RM_GOLDCHANGED          = 10136;
   RM_CHANGELIGHT          = 10137;
   RM_LAMPCHANGEDURA       = 10138;
   RM_CHARSTATUSCHANGED    = 10139;
   RM_GROUPCANCEL          = 10140;
   RM_SENDUSERREPAIR       = 10141;
   RM_SENDREPAIRCOST       = 10142;
   RM_USERREPAIRITEM_OK    = 10143;
   RM_USERREPAIRITEM_FAIL  = 10144;
   //RM_ITEMDURACHANGE       = 10145;
   RM_SENDUSERSTORAGEITEM  = 10146;
   RM_SENDUSERSTORAGEITEMLIST = 10147;
   RM_DELITEMS             = 10148;  //아이템 읽어 버림, 클라이언테에 알림.
   RM_SENDUSERMAKEDRUGITEMLIST = 10149;
   RM_MAKEDRUG_SUCCESS     = 10150;
   RM_MAKEDRUG_FAIL        = 10151;
   RM_SENDUSERSPECIALREPAIR = 10152;
   RM_ALIVE                = 10153;
   RM_DELAYMAGIC           = 10154;
   RM_RANDOMSPACEMOVE      = 10155;

   RM_DIGUP                = 10200;
   RM_DIGDOWN              = 10201;
   RM_FLYAXE               = 10202;
   RM_ALLOWPOWERHIT        = 10203;
   RM_LIGHTING             = 10204;
   RM_NORMALEFFECT         = 10205;  //기본 효과

   RM_MAKEPOISON           = 10300;
   RM_CHANGEGUILDNAME      = 10301; //길드의 이름, 길드내 직책이름 변경
   RM_SUBABILITY           = 10302;
   RM_BUILDGUILD_OK        = 10303;
   RM_BUILDGUILD_FAIL      = 10304;
   RM_DONATE_FAIL          = 10305;
   RM_DONATE_OK            = 10306;
   RM_MYSTATUS             = 10307;
   RM_TRANSPARENT          = 10308;
   RM_MENU_OK              = 10309;

   RM_SPACEMOVE_HIDE       = 10330;
   RM_SPACEMOVE_SHOW       = 10331;
   RM_RECONNECT            = 10332;
   RM_HIDEEVENT            = 10333;
   RM_SHOWEVENT            = 10334;
   RM_SPACEMOVE_HIDE2      = 10335;
   RM_SPACEMOVE_SHOW2      = 10336;
   RM_ZEN_BEE              = 10337;  //비막원충이 비막충을 만들어 낸다.
   RM_DELAYATTACK          = 10338;  //타격 시점을 맞추기 위해서

   RM_ADJUST_BONUS         = 10400;  //보너스 포인트를 조정하라.
   RM_MAKE_SLAVE           = 10401;  //서버이동으로 부하가 따라온다.

   RM_OPENHEALTH           = 10410;  //체력이 상대방에 보임
   RM_CLOSEHEALTH          = 10411;  //체력이 상대방에게 보이지 않음
   RM_DOOPENHEALTH         = 10412;
   RM_BREAKWEAPON          = 10413;  //무기가 깨짐, 애미메이션 효과
   RM_INSTANCEHEALGUAGE    = 10414;
   RM_CHANGEFACE           = 10415;  //변신...
   RM_NEXTTIME_PASSWORD    = 10416;  //다음 한번은 비밀번호입력 모드
   RM_DOSTARTUPQUEST       = 10417;

   RM_PLAYDICE             = 10500;
   //2003/02/11 그룹원 위치 정보
   RM_GROUPPOS             = 11008;

   {----------------------------}
   //서버간 메세지서버를 거치지 않은 메세징

   ISM_PASSWDSUCCESS       = 100;  //패스워드 통과, Certification+ID
   ISM_CANCELADMISSION     = 101;  //Certification 승인취소..
   ISM_USERCLOSED          = 102;  //사용자 접속 끊음
   ISM_USERCOUNT           = 103;  //이 서버의 사용자 수
   ISM_TOTALUSERCOUNT      = 104;
   ISM_SHIFTVENTURESERVER  = 110;
   ISM_ACCOUNTEXPIRED      = 111;
   ISM_GAMETIMEOFTIMECARDUSER = 112;
   ISM_USAGEINFORMATION    = 113;

   {----------------------------}

   ISM_USERSERVERCHANGE    = 200;
   ISM_USERLOGON           = 201;
   ISM_USERLOGOUT          = 202;
   ISM_WHISPER             = 203;
   ISM_SYSOPMSG            = 204;
   ISM_ADDGUILD            = 205;
   ISM_DELGUILD            = 206;
   ISM_RELOADGUILD         = 207;
   ISM_GUILDMSG            = 208;
   ISM_CHATPROHIBITION     = 209;    //채금
   ISM_CHATPROHIBITIONCANCEL = 210;  //채금해제
   ISM_CHANGECASTLEOWNER   = 211;   //사북성 주인 변경
   ISM_RELOADCASTLEINFO    = 212;   //사북성정보가 변경됨
   ISM_RELOADADMIN         = 213;
   // Friend System -------------
   ISM_FRIEND_INFO         = 214;    // 친구정보 추가
   ISM_FRIEND_DELETE       = 215;    // 친구 삭제
   ISM_FRIEND_OPEN         = 216;    // 친구 시스템 열기
   ISM_FRIEND_CLOSE        = 217;    // 친구 시스템 닫기
   ISM_FRIEND_RESULT       = 218;    // 결과값 전송
   // Tag System ----------------
   ISM_TAG_SEND            = 219;    // 쪽지 전송
   ISM_TAG_RESULT          = 220;    // 결과값 전송
   // User System --------------
   ISM_USER_INFO           = 221;    // 유저의 접속상태 전송
   // 2003/06/12 슬레이브 패치
   ISM_CHANGESERVERRECIEVEOK = 222;
   {----------------------------}

   DB_LOADHUMANRCD         = 100;
   DB_SAVEHUMANRCD         = 101;
   DB_SAVEANDCHANGE        = 102;
   DB_IDPASSWD             = 103;
   DB_NEWUSERID            = 104;
   DB_CHANGEPASSWD         = 105;
   DB_QUERYCHR             = 106;
   DB_NEWCHR               = 107;
   DB_GETOTHERNAMES        = 108;
   DB_ISVALIDUSER          = 111;
   DB_DELCHR               = 112;
   DB_ISVALIDUSERWITHID    = 113;
   DB_CONNECTIONOPEN       = 114;
   DB_CONNECTIONCLOSE      = 115;
   DB_SAVELOGO             = 116;
   DB_GETACCOUNT           = 117;
   DB_SAVESPECFEE          = 118;
   DB_SAVELOGO2            = 119;
   DB_GETSERVER            = 120;
   DB_CHANGESERVER         = 121;
   DB_LOGINCLOSEUSER       = 122;
   DB_RUNCLOSEUSER         = 123;
   DB_UPDATEUSERINFO       = 124;
   // Friend System -------------
   DB_FRIEND_LIST          = 125;   // 친구 리스트 요구
   DB_FRIEND_ADD           = 126;   // 친구 추가
   DB_FRIEND_DELETE        = 127;   // 친구 삭제
   DB_FRIEND_OWNLIST       = 128;   // 친구로 등록한 사람 리스트 요구
   DB_FRIEND_EDIT          = 129;   // 친구 설명 수정
   // Tag System ----------------
   DB_TAG_ADD              = 130;   // 쪽지 추가
   DB_TAG_DELETE           = 131;   // 쪽지 삭제
   DB_TAG_DELETEALL        = 132;   // 쪽지 전부 삭제 ( 가능한것만 )
   DB_TAG_LIST             = 133;   // 쪽지 리스트 추가
   DB_TAG_SETINFO          = 134;   // 촉지 상태 변경
   DB_TAG_REJECT_ADD       = 135;   // 거부자 추가
   DB_TAG_REJECT_DELETE    = 136;   // 거부자 삭제
   DB_TAG_REJECT_LIST      = 137;   // 거부자 리스트 요청
   DB_TAG_NOTREADCOUNT     = 138;   // 읽지않은 쪽지 개수 요청

   DBR_LOADHUMANRCD         = 1100;
   DBR_SAVEHUMANRCD         = 1101;
   DBR_IDPASSWD             = 1103;
   DBR_NEWUSERID            = 1104;
   DBR_CHANGEPASSWD         = 1105;
   DBR_QUERYCHR             = 1106;
   DBR_NEWCHR               = 1107;
   DBR_GETOTHERNAMES        = 1108;
   DBR_ISVALIDUSER          = 1111;
   DBR_DELCHR               = 1112;
   DBR_ISVALIDUSERWITHID    = 1113;
   DBR_GETACCOUNT           = 1117;
   DBR_GETSERVER            = 1200;
   DBR_CHANGESERVER         = 1201;
   DBR_UPDATEUSERINFO       = 1202;
   // Friend System ---------------
   DBR_FRIEND_LIST          = 1203; // 친구 리스트 전송
   DBR_FRIEND_WONLIST       = 1204; // 친구로 등록한 사람 전송
   DBR_FRIEND_RESULT        = 1205; // 명령어에 대한 결과값
   // Tag System ------------------
   DBR_TAG_LIST             = 1206; // 쪽지 리스트 전송
   DBR_TAG_REJECT_LIST      = 1207; // 거부자 리스트 전송
   DBR_TAG_NOTREADCOUNT     = 1208; // 읽지않은 쪽지 새수 전송
   DBR_TAG_RESULT           = 1209; // 멸령에 대한 결과값

   DBR_FAIL                 = 2000;
   DBR_NONE                 = 2000;

   {----------------------------}

   MSM_LOGIN            = 1;
   MSM_GETUSERKEY       = 100;
   MSM_SELECTUSERKEY    = 101;
   MSM_GETGROUPKEY      = 102;
   MSM_SELECTGROUPKEY   = 103;
   MSM_UPDATEFEERCD     = 120;
   MSM_DELETEFEERCD     = 121;
   MSM_ADDFEERCD        = 122;
   MSM_GETTIMEOUTLIST   = 123;

   MCM_PASSWDSUCCESS    = 10;
   MCM_PASSWDFAIL       = 11;
   MCM_IDONUSE          = 12;
   MCM_GETFEERCD        = 1000;
   MCM_ADDFEERCD        = 1001;
   MCM_ENDTIMEOUT       = 1002;
   MCM_ONUSETIMEOUT     = 1003;


{ ============================================================================
  网关通信消息常量 (GM_* = Gate Message)
  用途: 网关(Gate)与游戏服务器之间的通信
============================================================================ }
   GM_OPEN              = 1;   // 打开连接
   GM_CLOSE             = 2;   // 关闭连接
   GM_CHECKSERVER       = 3;   // 服务器发送检查信号
   GM_CHECKCLIENT       = 4;   // 客户端发送检查信号
   GM_DATA              = 5;   // 数据传输
   GM_SERVERUSERINDEX   = 6;   // 服务器用户索引
   GM_RECEIVE_OK        = 7;   // 接收确认
   GM_TEST              = 20;  // 测试消息

   {----------------------------}

{ ============================================================================
  种族常量 (RC_* = Race Code)
  用途: 定义游戏中各种角色和怪物的种族类型，用于服务器AI程序识别
============================================================================ }
   { 玩家和NPC }
   RC_USERHUMAN   = 0;    // 玩家角色(无法获得经验)
   RC_NPC         = 10;   // 普通NPC
   RC_DOORGUARD   = 11;   // 门卫守卫
   RC_PEACENPC    = 15;   // 和平NPC
   RC_ARCHERPOLICE = 20;  // 弓箭手警卫

   { 动物类 }
   RC_ANIMAL      = 50;   // 动物
   RC_DEER        = 52;   // 鹿
   RC_WOLF        = 53;   // 狼
   RC_TRAINER     = 55;   // 训练师

   { 怪物类 }
   RC_MONSTER     = 80;   // 普通怪物
   RC_OMA         = 81;   // 沃玛怪
   RC_SPITSPIDER  = 82;   // 吐丝蜘蛛
   RC_SLOWMONSTER = 83;   // 慢速怪物
   RC_SCORPION     = 84;  // 蝎子
   RC_KILLINGHERB  = 85;  // 食人花
   RC_SKELETON     = 86;  // 骷髅
   RC_DUALAXESKELETON = 87;  // 双斧骷髅
   RC_HEAVYAXESKELETON = 88; // 巨斧骷髅
   RC_KNIGHTSKELETON = 89;   // 骷髅战士
   RC_BIGKUDEKI      = 90;   // 大库德奇
   RC_MAGCOWFACEMON  = 91;   // 魔法牛面怪
   RC_COWFACEKINGMON = 92;   // 牛面王
   RC_THORNDARK      = 93;   // 暗黑荆棘
   RC_LIGHTINGZOMBI  = 94;   // 雷电僵尸
   RC_DIGOUTZOMBI    = 95;   // 钻地僵尸
   RC_ZILKINZOMBI    = 96;   // 吉尔金僵尸
   RC_COWMON         = 97;   // 牛面鬼
   RC_WHITESKELETON  = 100;  // 白骨骷髅
   RC_SCULTUREMON    = 101;  // 石像怪
   RC_SCULKING       = 102;  // 祖玛王
   RC_BEEQUEEN       = 103;  // 蜂后
   RC_ARCHERMON      = 104;  // 魔弓手/骷髅弓手
   RC_GASMOTH        = 105;  // 毒蛾
   RC_DUNG           = 106;  // 毒气怪
   RC_CENTIPEDEKING  = 107;  // 蜈蚣王
   RC_BLACKPIG       = 108;  // 黑猪
   RC_CASTLEDOOR     = 110;  // 城门(沙巴克)
   RC_WALL           = 111;  // 城墙(沙巴克)
   RC_ARCHERGUARD    = 112;  // 弓箭手守卫
   RC_ELFMON         = 113;  // 精灵怪
   RC_ELFWARRIORMON  = 114;  // 精灵战士怪
   RC_BIGHEARTMON    = 115;  // 血巨人王(大心脏)
   RC_SPIDERHOUSEMON = 116;  // 爆眼蜘蛛
   RC_EXPLOSIONSPIDER = 117; // 爆走蜘蛛
   RC_HIGHRISKSPIDER      = 118;  // 巨型蜘蛛
   RC_BIGPOISIONSPIDER = 119;     // 巨型毒蜘蛛
   RC_SOCCERBALL     = 120;  // 足球
   RC_BAMTREE        = 121;  // 竹树怪

   RC_SCULKING_2     = 122;  // 假祖玛王
   RC_BLACKSNAKEKING = 123;  // 黑蛇王
   RC_NOBLEPIGKING   = 124;  // 贵猪王
   RC_FEATHERKINGOFKING = 125; // 黑天魔王
   // 2003/02/11 新增怪物
   RC_SKELETONKING      = 126; // 骷髅半王
   RC_TOXICGHOST        = 127; // 腐蚀鬼
   RC_SKELETONSOLDIER   = 128; // 骷髅兵卒
   // 2003/03/04 新增怪物
   RC_BANYAGUARD        = 129; // 般若左使/般若右使
   RC_DEADCOWKING       = 130; // 死牛天王

   { 客户端种族常量 (RCC_* = Race Client Code) }
   RCC_HUMAN      = 0;    // 人类
   RCC_GUARD      = 12;   // 守卫1
   RCC_GUARD2     = 24;   // 守卫2
   RCC_MERCHANT   = 50;   // 商人

   { 生命属性常量 (LA_* = Life Attribute) }
   LA_CREATURE    = 0;    // 生物
   LA_UNDEAD      = 1;    // 亡灵

{ ============================================================================
  地图移动属性常量 (MP_* = Map Property)
  用途: 定义地图格子的通行属性
============================================================================ }
   MP_CANMOVE		= 0;   // 可以移动
   MP_WALL			= 1;   // 障碍物(不可移动)
   MP_HIGHWALL    = 2;    // 高墙(不可移动且不可飞越)

{ ============================================================================
  方向常量 (DR_* = Direction)
  用途: 定义8个方向，用于角色移动和面向
  注意: 顺时针排列，0为正上方
============================================================================ }
   DR_UP          = 0;    // 上(北)
   DR_UPRIGHT     = 1;    // 右上(东北)
   DR_RIGHT       = 2;    // 右(东)
   DR_DOWNRIGHT   = 3;    // 右下(东南)
   DR_DOWN        = 4;    // 下(南)
   DR_DOWNLEFT    = 5;    // 左下(西南)
   DR_LEFT        = 6;    // 左(西)
   DR_UPLEFT      = 7;    // 左上(西北)

{ ============================================================================
  装备槽位常量 (U_* = Use Item Slot)
  用途: 定义角色装备栏的各个槽位索引
============================================================================ }
   U_DRESS        = 0;    // 衣服
   U_WEAPON       = 1;    // 武器
   U_RIGHTHAND    = 2;    // 右手(副手/蜡烛)
   U_NECKLACE     = 3;    // 项链
   U_HELMET       = 4;    // 头盔
   U_ARMRINGL     = 5;    // 左手镯
   U_ARMRINGR     = 6;    // 右手镯
   U_RINGL        = 7;    // 左戒指
   U_RINGR        = 8;    // 右戒指
   // 2003/03/15 装备栏扩展
   U_BUJUK        = 9;    // 护身符
   U_BELT         = 10;   // 腰带
   U_BOOTS        = 11;   // 靴子
   U_CHARM        = 12;   // 饰品/护符

{ 用户权限等级常量 (UD_* = User Degree) }
   UD_USER        = 0;    // 普通用户
   UD_USER2       = 1;    // 用户2
   UD_OBSERVER    = 2;    // 观察者
   UD_SYSOP       = 3;    // 系统操作员
   UD_ADMIN       = 4;    // 管理员
   UD_SUPERADMIN  = 5;    // 超级管理员

{ 地图事件类型常量 (ET_* = Event Type) }
   ET_DIGOUTZOMBI    = 1;  // 僵尸钻地痕迹
   ET_MINE           = 2;  // 矿石埋藏点
   ET_PILESTONES     = 3;  // 石堆
   ET_HOLYCURTAIN    = 4;  // 结界
   ET_FIRE           = 5;  // 火焰
   ET_SCULPEICE      = 6;  // 祖玛王碎石片
   ET_HEARTPALP      = 7;  // 血巨人王(心脏)房间触手攻击
   ET_MINE2          = 8;  // 宝石埋藏点

{ 普通效果常量 (NE_* = Normal Effect) }
   NE_HEARTPALP      = 1;  // 基本效果系列，1号触手攻击


{ 剑术技能常量 (SWD_* = Sword Skill) }
   SWD_LONGHIT       = 12; // 刺杀剑法
   SWD_WIDEHIT       = 25; // 半月弯刀
   SWD_FIREHIT       = 26; // 烈火剑法
   SWD_RUSHRUSH      = 27; // 野蛮冲撞
   // 2003/03/15 新增武功
   SWD_CROSSHIT      = 34; // 狂风斩


{ ============================================================================
  任务系统常量 (QI_* = Quest If条件, QA_* = Quest Action动作)
  用途: NPC脚本系统的条件判断和动作执行
============================================================================ }

   { 任务条件常量 (QI_* = Quest If) - 用于IF语句 }
   QI_CHECK          = 1;   // 检查标志(101以上)
   QI_RANDOM         = 2;   // 随机判断
   QI_GENDER         = 3;   // 性别检查(MAN/WOMAN)
   QI_DAYTIME        = 4;   // 时段检查(SUNRAISE/DAY/SUNSET/NIGHT)
   QI_CHECKOPENUNIT  = 5;   // 检查开放单元
   QI_CHECKUNIT      = 6;   // 检查单元
   QI_CHECKLEVEL     = 7;   // 检查等级
   QI_CHECKJOB       = 8;   // 检查职业(Warrior/Wizard/Taoist)
   QI_CHECKITEM      = 20;  // 检查背包物品
   QI_CHECKITEMW     = 21;  // 检查穿戴物品
   QI_CHECKGOLD      = 22;  // 检查金币
   QI_ISTAKEITEM     = 23;  // 检查刚才获得的物品是什么
   QI_CHECKDURA      = 24;  // 检查物品平均耐久(dura/1000)，多个时检查最高耐久
   QI_CHECKDURAEVA   = 25;  // 检查物品耐久评估
   QI_DAYOFWEEK      = 26;  // 检查星期几
   QI_TIMEHOUR       = 27;  // 检查小时(0..23)
   QI_TIMEMIN        = 28;  // 检查分钟
   QI_CHECKPKPOINT   = 29;  // 检查PK值
   QI_CHECKLUCKYPOINT = 30; // 检查幸运值
   QI_CHECKMON_MAP   = 31;  // 检查当前地图是否有怪物
   QI_CHECKMON_AREA  = 32;  // 检查特定区域是否有怪物
   QI_CHECKHUM       = 33;  // 检查玩家
   QI_CHECKBAGGAGE   = 34;  // 检查是否可以给予用户物品(背包空间)
   // 6-11
   QI_CHECKNAMELIST  = 35;  // 检查名单列表
   QI_CHECKANDDELETENAMELIST  = 36;  // 检查并删除名单
   QI_CHECKANDDELETEIDLIST    = 37;  // 检查并删除ID列表
   // 每日任务
   QI_IFGETDAILYQUEST = 40;  // 检查今日是否已接任务(含有效期检查)
   QI_CHECKDAILYQUEST = 41;  // 检查是否正在执行特定编号任务(含有效期检查)
   QI_RANDOMEX        = 42;  // 扩展随机(参数: 5 100 表示5%)

   { 变量比较条件 }
   QI_EQUAL          = 135;  // 等于检查(EQUAL P1 10 检查P1是否等于10)
   QI_LARGE          = 136;  // 大于检查(LARGE P1 10 检查P1是否大于10)
   QI_SMALL          = 137;  // 小于检查(SMALL P1 10 检查P1是否小于10)

   { 任务动作常量 (QA_* = Quest Action) }
   QA_SET            = 1;   // 设置标志(101以上)
   QA_TAKE           = 2;   // 收取物品
   QA_GIVE           = 3;   // 给予物品
   QA_TAKEW          = 4;   // 收取穿戴中的物品
   QA_CLOSE          = 5;   // 关闭对话窗口
   QA_RESET          = 6;   // 重置
   QA_OPENUNIT       = 7;   // 开放单元
   QA_SETUNIT        = 8;   // 设置单元(1..100)
   QA_RESETUNIT      = 9;   // 重置单元(1..100)
   QA_BREAK          = 10;  // 中断
   QA_TIMERECALL     = 11;  // 定时召唤(指定时间后召唤到当前位置)
   QA_PARAM1         = 12;  // 参数1
   QA_PARAM2         = 13;  // 参数2
   QA_PARAM3         = 14;  // 参数3
   QA_PARAM4         = 15;  // 参数4
   QA_MAPMOVE        = 20;  // 地图移动
   QA_MAPRANDOM      = 21;  // 随机地图移动
   QA_TAKECHECKITEM  = 22;  // 收取CHECK项检查到的物品
   QA_MONGEN         = 23;  // 生成怪物
   QA_MONCLEAR       = 24;  // 清除所有怪物
   QA_MOV            = 25;  // 变量赋值
   QA_INC            = 26;  // 变量增加
   QA_DEC            = 27;  // 变量减少
   QA_SUM            = 28;  // 求和(SUM P1 P2，P9 = P1 + P2)
   QA_BREAKTIMERECALL = 29; // 取消定时召唤

   QA_MOVRANDOM      = 50;  // 随机移动
   QA_EXCHANGEMAP    = 51;  // 交换位置(与R001区域内一人交换位置)
   QA_RECALLMAP      = 52;  // 召唤地图(召唤R001区域内所有人)
   QA_ADDBATCH       = 53;  // 添加批处理
   QA_BATCHDELAY     = 54;  // 批处理延迟
   QA_BATCHMOVE      = 55;  // 批处理移动
   QA_PLAYDICE       = 56;  // 掷骰子(PLAYDICE 2 @diceresult 掷2个骰子后跳转到@diceresult)
   // 6-11
   QA_ADDNAMELIST     = 57;  // 添加到名单
   QA_DELETENAMELIST  = 58;  // 从名单删除
   // 每日任务
   QA_RANDOMSETDAILYQUEST = 60;  // 随机设置每日任务(参数: 最小 最大，如401 450)
   QA_SETDAILYQUEST = 61;        // 设置每日任务

   { 任务流程控制 }
   QA_GOTOQUEST      = 100;  // 跳转到任务
   QA_ENDQUEST       = 101;  // 结束任务
   QA_GOTO           = 102;  // 跳转

{ ============================================================================
  版本号常量
  用途: 客户端版本控制和兼容性检查
============================================================================ }
   VERSION_NUMBER = 20030422;             // 当前版本号 (2003/04/22)
   VERSION_NUMBER_030328 = 20030328;      // 版本 2003/03/28
   VERSION_NUMBER_030317 = 20030317;      // 版本 2003/03/17
   VERSION_NUMBER_030211 = 20030211;      // 版本 2003/02/11
   VERSION_NUMBER_030122 = 20030122;      // 版本 2003/01/22
   VERSION_NUMBER_020819 = 20020819;      // 版本 2002/08/19
   VERSION_NUMBER_0522 = 20020522;        // 版本 2002/05/22
   VERSION_NUMBER_02_0403 = 20020403;     // 版本 2002/04/03
   VERSION_NUMBER_01_1006 = 20011006;     // 版本 2001/10/06
   VERSION_NUMBER_0925 = 20010925;        // 版本 2001/09/25
   VERSION_NUMBER_0704 = 20010704;        // 版本 2001/07/04
   //VERSION_NUMBER_0522 = 20010522;      // (已注释)
   VERSION_NUMBER_0419 = 20010419;        // 版本 2001/04/19
   VERSION_NUMBER_0407 = 20010407;        // 版本 2001/04/07
   VERSION_NUMBER_0305 = 20010305;        // 版本 2001/03/05
   VERSION_NUMBER_0216 = 20010216;        // 版本 2001/02/16
   BUFFERSIZE = 10000;                    // 缓冲区大小

{ ============================================================================
  命令结果常量 (CR_* = Command Result)
  用途: 操作执行结果的返回值 (PDS:2003-03-31)
============================================================================ }
    CR_SUCCESS          = 0;       // 成功
    CR_FAIL             = 1;       // 失败
    CR_DONTFINDUSER     = 2;       // 找不到用户
    CR_DONTADD          = 3;       // 无法添加
    CR_DONTDELETE       = 4;       // 无法删除
    CR_DONTUPDATE       = 5;       // 无法更新
    CR_DONTACCESS       = 6;       // 无法执行
    CR_LISTISMAX        = 7;       // 列表已达最大值
    CR_LISTISMIN        = 8;       // 列表已达最小值
    CR_DBWAIT           = 9;       // 数据库等待中

{ 连接状态常量 (CONNSTATE_*) PDS:2003-03-31 }
    CONNSTATE_UNKNOWN    = 0;       // 未知状态
    CONNSTATE_DISCONNECT = 1;       // 断开连接
    CONNSTATE_NOUSE1     = 2;       // 未使用
    CONNSTATE_NOUSE2     = 3;       // 未使用
    CONNSTATE_CONNECT_0  = 4;       // 连接到0号服务器
    CONNSTATE_CONNECT_1  = 5;       // 连接到1号服务器
    CONNSTATE_CONNECT_2  = 6;       // 连接到2号服务器
    CONNSTATE_CONNECT_3  = 7;       // 连接到3号服务器(预留)

{ 关系类型常量 (RT_*) 2003/04/15 好友系统 }
    RT_FRIENDS          = 1;       // 好友
    RT_LOVERS           = 2;       // 恋人
    RT_MASTER           = 3;       // 师父
    RT_DISCIPLE         = 4;       // 徒弟
    RT_BLACKLIST        = 8;       // 黑名单/恶缘

{ 私信状态常量 (TAGSTATE_*) PDS:2003-03-31 }
    TAGSTATE_NOTREAD     = 0;       // 未读
    TAGSTATE_READ        = 1;       // 已读
    TAGSTATE_DONTDELETE  = 2;       // 禁止删除
    TAGSTATE_DELETED     = 3;       // 已删除

    // 用于私信状态变更
    TAGSTATE_WANTDELETABLE = 3;     // 设为可删除

{ ============================================================================
  函数声明
  用途: 角色外观特征编码/解码和消息构造的工具函数
============================================================================ }
function  RACEfeature (feature: Longint): byte;      // 从特征码提取种族
function  DRESSfeature (feature: Longint): byte;     // 从特征码提取服装
function  WEAPONfeature (feature: Longint): byte;    // 从特征码提取武器
function  HAIRfeature (feature: Longint): byte;      // 从特征码提取发型
function  APPRfeature (feature: Longint): word;      // 从特征码提取外观
function  MakeFeature (race, dress, weapon, face: byte): Longint;  // 组合生成特征码
function  MakeFeatureAp (race, state: byte; appear: word): Longint; // 组合生成特征码(带外观)
function  MakeDefaultMsg (msg: word; soul: integer; wparam, atag, nseries: word): TDefaultMessage; // 创建默认消息
function  UpInt (r: Real): integer;                  // 向上取整


{ ============================================================================
  实现部分
  用途: 工具函数的具体实现
============================================================================ }
implementation


{ RACEfeature
  功能: 从角色特征码中提取种族信息
  参数: 
    feature - 32位特征码
  返回值: 种族编号(字节)
  实现原理: 
    特征码结构: [Dress:8][Hair:8][Weapon:8][Race:8]
    提取低16位的低字节 }
function RACEfeature (feature: Longint): byte;
begin
	Result := LOBYTE (LOWORD (feature));
end;

{ WEAPONfeature
  功能: 从角色特征码中提取武器信息
  参数: 
    feature - 32位特征码
  返回值: 武器编号(字节)
  实现原理: 
    提取低16位的高字节 }
function WEAPONfeature (feature: Longint): byte;
begin
	Result := HIBYTE (LOWORD (feature));
end;

{ HAIRfeature
  功能: 从角色特征码中提取发型信息
  参数: 
    feature - 32位特征码
  返回值: 发型编号(字节)
  实现原理: 
    提取高16位的低字节 }
function HAIRfeature (feature: Longint): byte;
begin
	Result := LOBYTE (HIWORD (feature));
end;

{ DRESSfeature
  功能: 从角色特征码中提取服装信息
  参数: 
    feature - 32位特征码
  返回值: 服装编号(字节)
  实现原理: 
    提取高16位的高字节 }
function DRESSfeature (feature: Longint): byte;
begin
	Result := HIBYTE (HIWORD (feature));
end;

{ APPRfeature
  功能: 从角色特征码中提取外观信息
  参数: 
    feature - 32位特征码
  返回值: 外观编号(16位)
  实现原理: 
    提取高16位(包含发型和服装) }
function APPRfeature (feature: Longint): word;
begin
	Result := HIWORD (feature);
end;

{ MakeFeature
  功能: 组合生成角色特征码
  参数: 
    race - 种族编号
    dress - 服装编号
    weapon - 武器编号
    face - 脸型/发型编号
  返回值: 32位特征码
  实现原理: 
    将4个字节按照 [dress][face][weapon][race] 顺序组合成32位整数 }
function MakeFeature (race, dress, weapon, face: byte): Longint;
begin
	Result := MakeLong (MakeWord (race, weapon), MakeWord (face, dress));
end;

{ MakeFeatureAp
  功能: 组合生成带外观的角色特征码
  参数: 
    race - 种族编号
    state - 状态编号
    appear - 外观编号(16位)
  返回值: 32位特征码
  实现原理: 
    将种族、状态和外观组合成32位整数 }
function MakeFeatureAp (race, state: byte; appear: word): Longint;
begin
	Result := MakeLong (MakeWord (race, state), appear);
end;

{ MakeDefaultMsg
  功能: 创建默认消息结构
  参数: 
    msg - 消息标识符
    soul - 识别码/角色ID
    wparam - 参数
    atag - 标签
    nseries - 序列号
  返回值: 填充好的TDefaultMessage结构
  实现原理: 
    将各参数填充到TDefaultMessage记录的对应字段 }
function  MakeDefaultMsg (msg: word; soul: integer; wparam, atag, nseries: word): TDefaultMessage;
begin
   with Result do begin
      Ident := msg;      // 消息标识
      Recog := soul;     // 识别码
      param := wparam;   // 参数
      Tag	:= atag;      // 标签
      Series := nseries; // 序列号
   end;
end;

{ UpInt
  功能: 向上取整函数
  参数: 
    r - 实数值
  返回值: 向上取整后的整数
  实现原理: 
    如果实数大于其整数部分，则返回整数部分+1，否则返回整数部分
  注意事项: 
    等同于Ceil函数的功能 }
function UpInt (r: Real): integer;
begin
   if r > int(r) then Result := Trunc(r)+1 else Result := Trunc(r);
end;


end.



