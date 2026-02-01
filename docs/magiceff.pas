{ ============================================================================
  单元名称: magiceff
  功能描述: 魔法特效系统单元，负责游戏中各种魔法和技能的视觉效果处理
  
  主要功能:
  - 定义魔法特效相关的常量和帧数据
  - 实现各类魔法特效的基础类TMagicEff
  - 实现飞行类特效（飞斧、箭矢、火球等）
  - 实现角色特效和地图特效
  - 实现闪电、雷击、爆炸符咒等特殊效果
  - 提供特效绘制和动画播放功能
  
  作者: 传奇开发团队
  创建日期: 2003
  修改日期: 2024
============================================================================ }
unit magiceff;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Forms, Dialogs,
  Grobal2, CliUtil, ClFunc, HUtil32,GameImages,Textures;


const
   { ========== 魔法动画帧数常量 ========== }
   MG_READY       = 10;    // 魔法准备阶段的帧数
   MG_FLY         = 6;     // 魔法飞行阶段的帧数
   MG_EXPLOSION   = 10;    // 魔法爆炸阶段的帧数
   READYTIME  = 120;       // 准备阶段的持续时间（毫秒）
   EXPLOSIONTIME = 100;    // 爆炸阶段的持续时间（毫秒）
   
   { ========== 魔法图像索引基础值 ========== }
   FLYBASE = 10;           // 飞行效果图像的基础索引偏移
   EXPLOSIONBASE = 170;    // 爆炸效果图像的基础索引偏移
   //EFFECTFRAME = 260;
   MAXMAGIC = 10;          // 最大魔法数量
   
   { ========== 飞行物图像基础索引 ========== }
   FLYOMAAXEBASE = 447;    // 飞斧图像的基础索引（OMA格式）
   THORNBASE = 2967;       // 荆棘效果图像的基础索引
   ARCHERBASE = 2607;      // 弓箭手箭矢图像的基础索引
   ARCHERBASE2 = 272;      // 弓箭手备用箭矢图像索引 //2609;

   FLYFORSEC = 500;        // 飞行效果每秒的飞行距离（像素）
   FIREGUNFRAME = 6;       // 火枪/喷火效果的帧数


   { ========== 魔法效果图像索引数组 ========== }
   // 2003/03/15 新增武功技能
   MAXEFFECT = 36;         // 最大效果类型数量
   
   // EffectBase数组：存储各类魔法效果在WIL图像文件中的起始索引
   // 数组索引对应魔法编号，值为该魔法效果图像的起始位置
   EffectBase: array[0..MAXEFFECT-1] of integer = (
      0,             //0  火焰掌 - 基础火系攻击
      200,           //1  恢复术 - 治疗技能
      400,           //2  金刚火焰掌 - 强化火焰攻击
      600,           //3  暗燃术 - 暗属性燃烧
      0,             //4  剑光 - 剑气效果
      900,           //5  火焰风 - 火系范围攻击
      920,           //6  火焰喷射 - 喷火效果
      940,           //7  雷印掌 - 雷属性攻击（无施法效果）
      20,            //8  强击 - 物理强击，使用Magic2资源
      940,           //9  爆杀计 - 爆炸技能（无施法效果）
      940,           //10 大地援护 - 地系防护技能（无施法效果）
      940,           //11 大地援护魔 - 魔法版地系防护（无施法效果）
      0,             //12 御剑术 - 剑类飞行技能
      1380,          //13 结界 - 防护屏障
      1500,          //14 白骨骷髅召唤/召唤术 - 召唤系技能
      1520,          //15 隐身术 - 隐形技能
      940,           //16 大隐身 - 高级隐身
      1560,          //17 电击 - 闪电攻击
      1590,          //18 瞬间移动 - 传送技能
      1620,          //19 地烈掌 - 地系攻击
      1650,          //20 火焰爆炸 - 大范围火系爆炸
      1680,          //21 大银河（电流扩散） - 闪电扩散效果
      0,             //22 半月剑法 - 半月形剑气
      0,             //23 烈火剑法 - 火焰剑气
      0,             //24 无极步 - 轻功技能
      3960,          //25 探气破焰 - 气功攻击
      1790,          //26 大恢复术 - 高级治疗
      0,             //27 神兽召唤 - 使用Magic2资源
      3880,          //28 咒术之幕 - 咒术防护
      3920,          //29 狮子轮回 - 狮子攻击技能
      3840,          //30 冰雪风暴 - 冰系范围攻击
      0,             //31 魔法阵 - 阵法技能
      40,            //32 狂风斩 - 风系剑技（使用Magic2）
      130,           //33 灭天火 - 大范围火攻（使用Magic2）
      160,           //34 无极真气 - 真气技能（使用Magic2）
      190            //35 气功波 - 气功攻击（使用Magic2）
   );
   { ========== 命中效果图像索引数组 ========== }
   MAXHITEFFECT = 6;       // 最大命中效果类型数量
   
   // HitEffectBase数组：存储各类命中效果在WIL图像文件中的起始索引
   // 用于技能命中目标时显示的特效
   HitEffectBase: array[0..MAXHITEFFECT-1] of integer = (
      800,           //0  御剑术命中效果 - 剑气命中
      1410,          //1  御剑术命中效果 - 备用
      1700,          //2  半月剑法命中效果 - 半月剑气命中
      3480,          //3  烈火剑法效果 - 起始帧
      3390,          //4  烈火剑法效果 - 闪光帧
      40             //5  狂风斩命中效果
   );


   MAXMAGICTYPE = 15;      // 最大魔法类型数量

type
   { TMagicType枚举
     功能: 定义魔法特效的类型，用于区分不同的效果处理方式
     用途: 根据不同类型选择对应的绘制和动画逻辑 }
   TMagicType = (
     mtReady,              // 准备阶段 - 施法前的蓄力动画
     mtFly,                // 飞行阶段 - 魔法弹道飞行动画
     mtExplosion,          // 爆炸阶段 - 命中目标后的爆炸动画
     mtFlyAxe,             // 飞斧效果 - 投掷斧头类技能
     mtFireWind,           // 火风效果 - 火焰风暴类技能
     mtFireGun,            // 喷火效果 - 火焰喷射类技能
     mtLightingThunder,    // 闪电雷击 - 闪电链条效果
     mtThunder,            // 雷击效果 - 单纯雷电攻击
     mtExploBujauk,        // 爆炸符咒 - 飞行符纸爆炸效果
     mtBujaukGroundEffect, // 符咒地面效果 - 符咒落地后的持续效果
     mtKyulKai,            // 结界效果 - 防护罩类技能
     mtFlyArrow,           // 飞箭效果 - 箭矢飞行类技能
     mtFireBall,           // 火球效果 - 火球术类技能
     mtGroundEffect,       // 地面效果 - 地面持续效果
     mtFireThunder         // 火雷效果 - 火系雷电结合技能
   );

   { TUseMagicInfo记录
     功能: 存储魔法使用的完整信息
     用途: 在施法时传递魔法参数给特效系统 }
   TUseMagicInfo = record
      ServerMagicCode: integer;  // 服务器端魔法代码，用于同步验证
      MagicSerial: integer;      // 魔法序列号，唯一标识本次施法
      Target: integer;           // 目标识别码(RecogCode)，指向攻击目标
      EffectType: TMagicType;    // 效果类型，决定使用哪种特效处理
      EffectNumber: integer;     // 效果编号，索引EffectBase数组
      TargX: integer;            // 目标X坐标（屏幕坐标）
      TargY: integer;            // 目标Y坐标（屏幕坐标）
      Recusion: Boolean;         // 是否循环播放动画
      AniTime: integer;          // 动画持续时间（毫秒）
   end;
   PTUseMagicInfo = ^TUseMagicInfo;  // TUseMagicInfo的指针类型

   { TMagicEff类
     功能: 魔法特效的基础类，提供特效的核心功能
     用途: 作为所有魔法特效类的父类，实现飞行、爆炸、绘制等基础功能 }
   TMagicEff = class
      Active: Boolean;             // 激活状态，True表示特效正在运行
      ServerMagicId: integer;      // 服务器端魔法ID，用于同步验证
      MagOwner: TObject;           // 魔法施放者（TActor类型）
      TargetActor: TObject;        // 目标角色（TActor类型）

      ImgLib: TGameImages;         // 图像库引用，存储特效图像数据
      EffectBase: integer;         // 效果图像的基础索引

      MagExplosionBase: integer;   // 爆炸效果图像的基础索引
      px, py: integer;             // 图像绘制偏移量
      RX, RY: integer;             // 地图坐标（从屏幕坐标换算得到）
      Dir16, OldDir16: byte;       // 16方向索引（当前方向和前一帧方向）
      TargetX, TargetY: integer;   // 目标的屏幕坐标
      TargetRx, TargetRy: integer; // 目标的地图坐标
      FlyX, FlyY, OldFlyX, OldFlyY: integer; // 飞行物当前坐标和前一帧坐标
      FlyXf, FlyYf: Real;          // 飞行物浮点精度坐标（用于平滑移动）
      Repetition: Boolean;         // 是否循环播放动画
      FixedEffect: Boolean;        // 是否为固定位置效果（不飞行）
      MagicType: integer;          // 魔法类型编号
      NextEffect: TMagicEff;       // 链接的下一个特效（用于连续特效）
      ExplosionFrame: integer;     // 爆炸动画的帧数
      NextFrameTime: integer;      // 下一帧的时间间隔（毫秒）
      Light: integer;              // 光照强度值
   private
      start, curframe, frame: integer;  // 起始帧、当前帧、总帧数
      framesteptime: longword;     // 帧步进时间计时器
      starttime:  longword;        // 特效开始时间
      repeattime: longword;        // 循环动画时间（-1表示无限循环）
      steptime: longword;          // 帧步进时间
      fireX, fireY: integer;       // 发射位置（起始点）
      firedisX, firedisY, newfiredisX, newfiredisY: integer; // 飞行方向分量和新方向分量
      FireMyselfX, FireMyselfY: integer; // 发射时自身角色的位置
      prevdisx, prevdisy: integer; // 前一帧与目标的距离（用于判断是否到达）
   protected
      { GetFlyXY: 根据时间计算飞行物位置 }
      procedure GetFlyXY (ms: integer; var fx, fy: integer);
   public
      { Create: 创建魔法特效实例
        参数: id-服务器ID, effnum-效果编号, sx,sy-起始坐标, tx,ty-目标坐标,
              mtype-魔法类型, Recusion-是否循环, anitime-动画时长 }
      constructor Create (id, effnum, sx, sy, tx, ty: integer; mtype: TMagicType; Recusion: Boolean; anitime: integer);
      destructor Destroy; override;
      { Run: 运行特效逻辑，返回False表示特效结束 }
      function  Run: Boolean; dynamic;
      { Shift: 更新特效状态，处理帧进度和位置移动 }
      function  Shift: Boolean; dynamic;
      { DrawEff: 在指定表面上绘制特效 }
      procedure DrawEff (surface: TTexture); dynamic;
   end;

   { TFlyingAxe类
     功能: 飞斧特效类，实现投掷斧头的飞行和显示效果
     继承: TMagicEff
     用途: 用于OMA怪物或角色的投掷斧头攻击 }
   TFlyingAxe = class (TMagicEff)
      FlyImageBase: integer;  // 飞行图像的基础索引
      ReadyFrame: integer;    // 准备阶段距离阈值（超过此距离才显示）
   public
      constructor Create (id, effnum, sx, sy, tx, ty: integer; mtype: TMagicType; Recusion: Boolean; anitime: integer);
      procedure DrawEff (surface: TTexture); override;
   end;

   { TFlyingArrow类
     功能: 飞箭特效类，实现箭矢的飞行和显示效果
     继承: TFlyingAxe
     用途: 用于弓箭手的远程箭矢攻击 }
   TFlyingArrow = class (TFlyingAxe)
   public
      procedure DrawEff (surface: TTexture); override;
   end;

   { TFlyingFireBall类
     功能: 飞行火球特效类，实现火球的飞行和显示效果
     继承: TFlyingAxe
     用途: 用于火球术等火系飞行技能
     创建日期: 2003/02/11 }
   TFlyingFireBall = class (TFlyingAxe)
   public
      constructor Create (id, effnum, sx, sy, tx, ty: integer; mtype: TMagicType; Recusion: Boolean; anitime: integer);
      procedure DrawEff (surface: TTexture); override;
   end;

   { TCharEffect类
     功能: 角色特效类，实现依附在角色身上的特效
     继承: TMagicEff
     用途: 用于BUFF效果、受击效果等跟随角色移动的特效 }
   TCharEffect = class (TMagicEff)
   public
      constructor Create (effbase, effframe: integer; target: TObject);
      { Run: 运行特效，返回False表示动画播放结束 }
      function  Run: Boolean; override;
      procedure DrawEff (surface: TTexture); override;
   end;

   { TMapEffect类
     功能: 地图特效类，实现固定在地图上的特效
     继承: TMagicEff
     用途: 用于魔法爆炸、地面火焰等不跟随角色移动的特效 }
   TMapEffect = class (TMagicEff)
   public
      RepeatCount: integer;   // 动画重复次数，0表示不重复
      constructor Create (effbase, effframe: integer; x, y: integer);
      { Run: 运行特效，返回False表示动画播放结束 }
      function  Run: Boolean; override;
      procedure DrawEff (surface: TTexture); override;
   end;

   { TScrollHideEffect类
     功能: 卷轴隐身特效类，实现角色消失的视觉效果
     继承: TMapEffect
     用途: 用于隐身卷轴、传送等技能的角色消失效果 }
   TScrollHideEffect = class (TMapEffect)
   public
      constructor Create (effbase, effframe: integer; x, y: integer; target: TObject);
      function  Run: Boolean; override;
   end;

   { TLightingEffect类
     功能: 照明效果类（当前未完全实现）
     继承: TMagicEff
     用途: 预留用于光照特效 }
   TLightingEffect = class (TMagicEff)
   public
      constructor Create (effbase, effframe: integer; x, y: integer);
      function  Run: Boolean; override;
   end;

   { TFireNode记录
     功能: 存储喷火效果的单个火焰节点信息
     用途: 用于TFireGunEffect中记录每个火焰粒子的位置 }
   TFireNode = record
      x: integer;          // 火焰节点X坐标
      y: integer;          // 火焰节点Y坐标
      firenumber: integer; // 火焰帧编号
   end;

   { TFireGunEffect类
     功能: 喷火/火枪特效类，实现连续喷射火焰的效果
     继承: TMagicEff
     用途: 用于火焰喷射、火龙喷息等技能 }
   TFireGunEffect = class (TMagicEff)
   public
      OutofOil: Boolean;    // 火焰耗尽标志，True表示停止喷射新火焰
      firetime: longword;   // 喷火开始时间
      FireNodes: array[0..FIREGUNFRAME-1] of TFireNode; // 火焰节点数组
      constructor Create (effbase, sx, sy, tx, ty: integer);
      function  Run: Boolean; override;
      procedure DrawEff (surface: TTexture); override;
   end;

   { TThuderEffect类
     功能: 雷击特效类，实现雷电攻击的视觉效果
     继承: TMagicEff
     用途: 用于雷电术等雷系攻击技能 }
   TThuderEffect = class (TMagicEff)
   public
      constructor Create (effbase, tx, ty: integer; target: TObject);
      procedure DrawEff (surface: TTexture); override;
   end;

   { TLightingThunder类
     功能: 闪电雷击特效类，实现闪电链条攻击效果
     继承: TMagicEff
     用途: 用于闪电术、连锁闪电等技能 }
   TLightingThunder = class (TMagicEff)
   public
      constructor Create (effbase, sx, sy, tx, ty: integer; target: TObject);
      procedure DrawEff (surface: TTexture); override;
   end;

   { TExploBujaukEffect类
     功能: 爆炸符咒特效类，实现符纸飞行并爆炸的效果
     继承: TMagicEff
     用途: 用于爆裂符等飞行符咒攻击 }
   TExploBujaukEffect = class (TMagicEff)
   public
      constructor Create (effbase, sx, sy, tx, ty: integer; target: TObject);
      procedure DrawEff (surface: TTexture); override;
   end;

   { TBujaukGroundEffect类
     功能: 符咒地面效果类，实现符咒落地后的持续效果
     继承: TMagicEff
     用途: 用于地火符、结界符等需要在地面持续显示的技能 }
   TBujaukGroundEffect = class (TMagicEff)
   public
      MagicNumber: integer;     // 魔法编号，用于区分不同符咒效果
      BoGroundEffect: Boolean;  // 地面效果标志，标识是否已触发地面效果
      constructor Create (effbase, magicnumb, sx, sy, tx, ty: integer);
      function  Run: Boolean; override;
      procedure DrawEff (surface: TTexture); override;
   end;

   { TNormalDrawEffect类
     功能: 普通绘制效果类，实现一次性播放的简单特效
     继承: TMagicEff
     用途: 用于一次性显示然后消失的简单效果，可选择是否Alpha混合 }
   TNormalDrawEffect = class (TMagicEff)
   private
      BoBlending: Boolean;  // 是否使用Alpha混合绘制
   public
      { Create: 创建普通绘制效果
        参数: xx,yy-坐标, iLib-图像库, eff_base-图像基础索引,
              eff_frame-帧数, eff_time-帧间隔, blending-是否混合 }
      constructor Create (xx, yy: integer;
                          iLib: TGameImages;
                          eff_base: integer;
                          eff_frame: integer;
                          eff_time: integer;
                          blending: Boolean);
      function Run: Boolean; override;
      procedure DrawEff (surface: TTexture); override;
   end;


implementation

uses
   ClMain, Actor, SoundUtil, Path;

{------------------------------------------------------------------------------}
{ GetEffectBase
  功能: 获取魔法效果的图像库和基础索引
  参数:
    mag - 魔法编号，对应EffectBase数组的索引
    mtype - 效果类型（0=施法效果, 1=剑法/命中效果）
    wimg - 输出参数，返回对应的图像库引用
    idx - 输出参数，返回图像在库中的起始索引
  实现原理:
    1. 根据mtype区分施法效果和命中效果
    2. 根据魔法编号选择不同的图像库(WMagic/WMagic2/WMon21Img)
    3. 从EffectBase或HitEffectBase数组获取图像索引 }
{------------------------------------------------------------------------------}

procedure GetEffectBase (mag, mtype: integer; var wimg: TGameImages; var idx: integer);
begin
   // 初始化输出参数
   wimg := nil;
   idx := 0;
   
   case mtype of
      0:  // 施法效果 - 魔法施放时的起始动画
         begin
            case mag of
               // 2003/03/15 新增武功技能 - 使用Magic2图像库
               33, // 灭天火
               34, // 无极真气
               35, // 气功波
               8,  // 强击
               27: // 神兽召唤
                  begin
                     wimg := WMagic2;  // 使用Magic2图像库
                     if mag in [0..MAXEFFECT-1] then
                        idx := EffectBase[mag];
                  end;
               // 2003/03/04 魔法阵 - 使用怪物图像库
               31:
                  begin
                     wimg := WMon21Img;  // 使用Mon21图像库
                     if mag in [0..MAXEFFECT-1] then
                        idx := EffectBase[mag];
                  end;
               else
                  begin
                     wimg := WMagic;  // 默认使用主魔法图像库
                     if mag in [0..MAXEFFECT-1] then
                        idx := EffectBase[mag];
                  end;
            end;
         end;
      1: // 剑法/命中效果 - 技能命中目标时的动画
         begin
            wimg := WMagic;  // 默认使用主魔法图像库
            if mag in [0..MAXHITEFFECT-1] then begin
               idx := HitEffectBase[mag];
            end;
            // 2003/03/15 狂风斩使用Magic2图像库
            if (mag = 5) then wimg := WMagic2;
         end;
   end;
end;

{ TMagicEff.Create
  功能: 创建魔法特效实例并初始化所有参数
  参数:
    id - 服务器魔法ID
    effnum - 效果图像基础索引
    sx, sy - 发射位置坐标
    tx, ty - 目标位置坐标
    mtype - 魔法类型
    Recusion - 是否循环播放
    anitime - 动画持续时间
  实现原理:
    1. 根据魔法类型设置不同的帧数和动画参数
    2. 计算飞行方向和速度
    3. 初始化位置和时间计时器 }
constructor TMagicEff.Create (id, effnum, sx, sy, tx, ty: integer; mtype: TMagicType; Recusion: Boolean; anitime: integer);
var
   tax, tay: integer;  // 目标距离的绝对值
 begin
   ImgLib := WMagic;  // 默认使用主魔法图像库
   
   // 根据魔法类型设置动画参数
   case mtype of
      mtReady:  // 准备阶段 - 不需要特殊初始化
         begin
         end;
      mtFly,                   // 飞行效果
      mtBujaukGroundEffect,    // 符咒地面效果
      mtExploBujauk:           // 爆炸符咒
         begin
            start := 0;              // 起始帧
            frame := 6;              // 飞行动画6帧
            curframe := start;
            FixedEffect := FALSE;    // 非固定效果（会飞行）
            Repetition := Recusion;  // 是否循环
            ExplosionFrame := 10;    // 爆炸动画10帧
         end;
      // 2003/02/11 火球效果
      mtFireBall:
         begin
            start := 0;
            frame := 6;              // 飞行动画6帧
            curframe := start;
            FixedEffect := FALSE;
            Repetition  := Recusion;
            ExplosionFrame := 1;     // 爆炸动画1帧
         end;
      // 2003/03/04 地面效果
      mtGroundEffect:
         begin
            start := 0;
            frame := 20;             // 地面效果20帧
            curframe := start;
            FixedEffect := TRUE;     // 固定效果
            Repetition  := FALSE;    // 不循环
            ExplosionFrame := 20;
            ImgLib := WMon21Img;     // 使用Mon21图像库
         end;
      mtExplosion,       // 爆炸效果
      mtThunder,         // 雷击效果
      mtLightingThunder: // 闪电雷击
         begin
            start := 0;
            frame := -1;             // 帧数待定（由ExplosionFrame决定）
            ExplosionFrame := 10;
            curframe := start;
            FixedEffect := TRUE;     // 固定效果
            Repetition := FALSE;
         end;
      // 2003/03/15 新增武功 - 火雷效果
      mtFireThunder:
         begin
            start := 0;
            frame := -1;
            ExplosionFrame := 10;
            curframe := start;
            FixedEffect := TRUE;
            Repetition := FALSE;
            ImgLib := WMagic2;       // 使用Magic2图像库
         end;
      mtFlyAxe:  // 飞斧效果
         begin
            start := 0;
            frame := 3;              // 飞斧动画3帧
            curframe := start;
            FixedEffect := FALSE;
            Repetition := Recusion;
            ExplosionFrame := 3;
         end;
      mtFlyArrow:  // 飞箭效果
         begin
            start := 0;
            frame := 1;              // 飞箭动画1帧（静态图像）
            curframe := start;
            FixedEffect := FALSE;
            Repetition := Recusion;
            ExplosionFrame := 1;
         end;
   end;
   
   // 初始化基本属性
   ServerMagicId := id;     // 服务器魔法ID
   EffectBase := effnum;    // 效果图像基础索引
   TargetX := tx;           // 目标X坐标
   TargetY := ty;           // 目标Y坐标
   fireX := sx;             // 发射位置X
   fireY := sy;             // 发射位置Y
   FlyX := sx;              // 当前飞行位置X
   FlyY := sy;              // 当前飞行位置Y
   OldFlyX := sx;           // 前一帧飞行位置X
   OldFlyY := sy;           // 前一帧飞行位置Y
   FlyXf := sx;             // 浮点精度X坐标
   FlyYf := sy;             // 浮点精度Y坐标
   
   // 记录发射时自身角色的位置（用于补偿屏幕滚动）
   FireMyselfX := Myself.RX*UNITX + Myself.ShiftX;
   FireMyselfY := Myself.RY*UNITY + Myself.ShiftY;
   
   // 计算爆炸效果的图像索引
   MagExplosionBase := EffectBase + EXPLOSIONBASE;
   light := 1;  // 光照强度

   // 计算飞行方向和速度分量
   // 根据距离较大的轴向进行归一化计算
   if fireX <> TargetX then tax := abs(TargetX-fireX)
   else tax := 1;
   if fireY <> TargetY then tay := abs(TargetY-fireY)
   else tay := 1;
   if abs(fireX-TargetX) > abs(fireY-TargetY) then begin
      // X轴距离较大，以X轴为基准计算
      firedisX := Round((TargetX-fireX) * (500 / tax));
      firedisY := Round((TargetY-fireY) * (500 / tax));
   end else begin
      // Y轴距离较大，以Y轴为基准计算
      firedisX := Round((TargetX-fireX) * (500 / tay));
      firedisY := Round((TargetY-fireY) * (500 / tay));
   end;

   // 初始化时间和状态参数
   NextFrameTime := 50;              // 帧间隔默认50毫秒
   framesteptime := GetTickCount;    // 帧步进计时器
   starttime := GetTickCount;        // 特效开始时间
   steptime := GetTickCount;         // 帧时间计时器
   RepeatTime := anitime;            // 动画持续时间
   Dir16 := GetFlyDirection16 (sx, sy, tx, ty);  // 计算16方向索引
   OldDir16 := Dir16;                // 保存前一帧方向
   NextEffect := nil;                // 无后续特效
   Active := TRUE;                   // 激活特效
   prevdisx := 99999;                // 初始化前一帧距离（极大值）
   prevdisy := 99999;
end;

{ TMagicEff.Destroy
  功能: 销毁魔法特效实例，释放资源 }
destructor TMagicEff.Destroy;
begin
   inherited Destroy;
end;

{ TMagicEff.Shift
  功能: 更新特效状态，处理帧进度和位置移动
  返回值: True表示特效继续运行，False表示动画结束
  实现原理:
    1. 处理帧动画进度（循环或单次播放）
    2. 对于飞行效果，计算飞行位置和方向
    3. 检测是否命中目标，触发爆炸效果
    4. 对于固定效果，更新显示位置 }
function  TMagicEff.Shift: Boolean;

   { OverThrough: 检查是否穿过目标
     功能: 判断飞行物是否已经穿过目标位置
     参数: olddir-前一帧方向, newdir-当前方向
     返回值: True表示已穿过目标 }
   function OverThrough (olddir, newdir: integer): Boolean;
   begin
      Result := FALSE;
      // 如果方向变化超过2个方向单位，认为已穿过
      if abs(olddir-newdir) >= 2 then begin
         Result := TRUE;
         // 特殊情况：方向0和方吖15是相邻的（循环）
         if ((olddir=0) and (newdir=15)) or ((olddir=15) and (newdir=0)) then
            Result := FALSE;
      end;
   end;
var
   i, rrx, rry, ms, stepx, stepy, newstepx, newstepy, nn: integer;
   tax, tay, shx, shy, passdir16: integer;
   crash: Boolean;      // 碰撞/命中标志
   stepxf, stepyf: Real; // 浮点精度步进值
begin
   Result := TRUE;
   
   // ========== 处理帧动画进度 ==========
   if Repetition then begin
      // 循环播放模式
      if GetTickCount - steptime > longword(NextFrameTime) then begin
         steptime := GetTickCount;
         Inc (curframe);
         if curframe > start+frame-1 then
            curframe := start;  // 循环回到起始帧
      end;
   end else begin
      // 单次播放模式
      if (frame > 0) and (GetTickCount - steptime > longword(NextFrameTime)) then begin
         steptime := GetTickCount;
         Inc (curframe);
         if curframe > start+frame-1 then begin
            curframe := start+frame-1;  // 停留在最后一帧
            Result := FALSE;  // 动画结束
         end;
      end;
   end;
   
   // ========== 处理飞行效果 ==========
   if (not FixedEffect) then begin

      crash := FALSE;  // 初始化碰撞标志
      
      if TargetActor <> nil then begin
         // 有目标角色的追踪飞行
         ms := GetTickCount - framesteptime;  // 计算距上次绘制过去的时间
         framesteptime := GetTickCount;
         
         // 重新计算目标坐标（目标可能在移动）
         PlayScene.ScreenXYfromMCXY (TActor(TargetActor).RX,
                                     TActor(TargetActor).RY,
                                     TargetX,
                                     TargetY);
         // 补偿屏幕滚动导致的偏移
         shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
         shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;
         TargetX := TargetX + shx;
         TargetY := TargetY + shy;

         // 重新计算向目标的飞行方向和速度
         if FlyX <> TargetX then tax := abs(TargetX-FlyX)
         else tax := 1;
         if FlyY <> TargetY then tay := abs(TargetY-FlyY)
         else tay := 1;
         if abs(FlyX-TargetX) > abs(FlyY-TargetY) then begin
            newfiredisX := Round((TargetX-FlyX) * (500 / tax));
            newfiredisY := Round((TargetY-FlyY) * (500 / tax));
         end else begin
            newfiredisX := Round((TargetX-FlyX) * (500 / tay));
            newfiredisY := Round((TargetY-FlyY) * (500 / tay));
         end;

         // 平滑过渡到新的飞行方向（避免突然转向）
         if firedisX < newfiredisX then firedisX := firedisX + _MAX(1, (newfiredisX - firedisX) div 10);
         if firedisX > newfiredisX then firedisX := firedisX - _MAX(1, (firedisX - newfiredisX) div 10);
         if firedisY < newfiredisY then firedisY := firedisY + _MAX(1, (newfiredisY - firedisY) div 10);
         if firedisY > newfiredisY then firedisY := firedisY - _MAX(1, (firedisY - newfiredisY) div 10);

         // 计算本帧的移动距离
         stepxf := (firedisX/700) * ms;
         stepyf := (firedisY/700) * ms;
         FlyXf := FlyXf + stepxf;
         FlyYf := FlyYf + stepyf;
         FlyX := Round (FlyXf);
         FlyY := Round (FlyYf);

         // 保存当前位置为下一帧的旧位置
         OldFlyX := FlyX;
         OldFlyY := FlyY;
         
         // 计算当前朝向目标的方向（用于检测是否穿过）
         passdir16 := GetFlyDirection16 (FlyX, FlyY, TargetX, TargetY);

         // 检测是否命中目标（三种情况）
         // 1. 距离目标小于15像素
         // 2. 距离开始增大（说明已经越过）
         // 3. 方向变化过大（说明已穿过）
         if ((abs(TargetX-FlyX) <= 15) and (abs(TargetY-FlyY) <= 15)) or
            ((abs(TargetX-FlyX) >= prevdisx) and (abs(TargetY-FlyY) >= prevdisy)) or
            OverThrough(OldDir16, passdir16) then begin
            crash := TRUE;  // 命中目标
         end else begin
            // 记录当前距离供下次比较
            prevdisx := abs(TargetX-FlyX);
            prevdisy := abs(TargetY-FlyY);
         end;
         OldDir16 := passdir16;  // 保存当前方向

      end else begin
         // 无目标角色的直线飞行（飞向固定坐标）
         ms := GetTickCount - framesteptime;  // 计算特效开始后的时间

         rrx := TargetX - fireX;
         rry := TargetY - fireY;

         // 根据时间计算当前位置
         stepx := Round ((firedisX/900) * ms);
         stepy := Round ((firedisY/900) * ms);
         FlyX := fireX + stepx;
         FlyY := fireY + stepy;
      end;

      // 将屏幕坐标转换为地图坐标
      PlayScene.CXYfromMouseXY (FlyX, FlyY, Rx, Ry);

      // 如果命中目标，切换到爆炸效果
      if crash and (TargetActor <> nil) then begin
         FixedEffect := TRUE;  // 切换为固定效果（爆炸）
         start := 0;
         frame := ExplosionFrame;  // 使用爆炸帧数
         curframe := start;
         Repetition := FALSE;

         // 播放爆炸音效
         PlaySound (TActor(MagOwner).magicexplosionsound);
      end;
   end;
   
   // ========== 处理固定效果（爆炸等） ==========
   if FixedEffect then begin
      if frame = -1 then frame := ExplosionFrame;  // 设置帧数
      
      if TargetActor = nil then begin
         // 无目标角色，在固定坐标显示（补偿屏幕滚动）
         FlyX := TargetX - ((Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX);
         FlyY := TargetY - ((Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY);
         PlayScene.CXYfromMouseXY (FlyX, FlyY, Rx, Ry);
      end else begin
         // 有目标角色，跟随目标位置显示
         Rx := TActor(TargetActor).Rx;
         Ry := TActor(TargetActor).Ry;
         PlayScene.ScreenXYfromMCXY (Rx, Ry, FlyX, FlyY);
         FlyX := FlyX + TActor(TargetActor).ShiftX;
         FlyY := FlyY + TActor(TargetActor).ShiftY;
      end;
   end;
end;

{ TMagicEff.GetFlyXY
  功能: 根据时间计算飞行物的当前位置
  参数:
    ms - 特效开始后的毫秒数
    fx, fy - 输出参数，返回计算得到的坐标 }
procedure TMagicEff.GetFlyXY (ms: integer; var fx, fy: integer);
var
   rrx, rry, stepx, stepy: integer;
begin
   rrx := TargetX - fireX;  // X方向总距离
   rry := TargetY - fireY;  // Y方向总距离

   // 根据时间和速度计算当前位置
   stepx := Round ((firedisX/900) * ms);
   stepy := Round ((firedisY/900) * ms);
   fx := fireX + stepx;
   fy := fireY + stepy;
end;

{ TMagicEff.Run
  功能: 运行特效逻辑，更新状态并检查是否超时
  返回值: True表示特效继续运行，False表示特效结束
  注意: 特效最长运行10秒，超时自动结束 }
function  TMagicEff.Run: Boolean;
begin
   Result := Shift;  // 先更新状态
   if Result then
      // 检查是否超时（10秒超时保护）
      if GetTickCount - starttime > 10000 then
         Result := FALSE
      else Result := TRUE;
end;

{ TMagicEff.DrawEff
  功能: 在指定表面上绘制特效
  参数: surface - 目标绘制表面
  实现原理:
    1. 检查特效是否激活且已离开起始点
    2. 计算屏幕滚动补偿
    3. 根据是飞行还是爆炸状态选择不同图像
    4. 使用Alpha混合绘制图像 }
procedure TMagicEff.DrawEff (surface: TTexture);
var
   img: integer;     // 图像索引
   d: TTexture;      // 图像纹理
   shx, shy: integer; // 屏幕滚动补偿量
begin
   // 只有在特效激活且已离开起始点时才绘制
   if Active and ((Abs(FlyX-fireX) > 15) or (Abs(FlyY-fireY) > 15) or FixedEffect) then begin

      // 计算屏幕滚动补偿量
      shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
      shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

      if not FixedEffect then begin
         // 绘制飞行状态的图像
         // 图像索引 = 基础索引 + 飞行偏移 + 方向*10 + 当前帧
         img := EffectBase + FLYBASE + Dir16 * 10;
         d := ImgLib.GetCachedImage (img + curframe, px, py);
         if d <> nil then begin
            DrawBlend (surface,
                       FlyX + px - UNITX div 2 - shx,
                       FlyY + py - UNITY div 2 - shy,
                       d);
         end;
      end else begin
         // 绘制爆炸/固定状态的图像
         img := MagExplosionBase + curframe;
         d := ImgLib.GetCachedImage (img, px, py);
         if d <> nil then begin
            DrawBlend (surface,
                       FlyX + px - UNITX div 2,
                       FlyY + py - UNITY div 2,
                       d);
         end;
      end;
   end;
end;


{------------------------------------------------------------}
{ TFlyingAxe类实现 - 飞斧特效 }
{------------------------------------------------------------}

{ TFlyingAxe.Create
  功能: 创建飞斧特效实例
  实现: 调用父类构造器后设置飞斧特有的图像基础索引 }
constructor TFlyingAxe.Create (id, effnum, sx, sy, tx, ty: integer; mtype: TMagicType; Recusion: Boolean; anitime: integer);
begin
   inherited Create (id, effnum, sx, sy, tx, ty, mtype, Recusion, anitime);
   FlyImageBase := FLYOMAAXEBASE;  // 设置飞斧图像基础索引
   ReadyFrame := 65;               // 设置准备阈值（距离超过65才显示）
end;

{ TFlyingAxe.DrawEff
  功能: 绘制飞斧特效
  特点:
    - 不使用Alpha混合，直接绘制
    - 只有距离超过ReadyFrame时才显示 }
procedure TFlyingAxe.DrawEff (surface: TTexture);
var
   img: integer;
   d: TTexture;
   shx, shy: integer;
begin
   // 只有距离超过准备阈值时才绘制
   if Active and ((Abs(FlyX-fireX) > ReadyFrame) or (Abs(FlyY-fireY) > ReadyFrame)) then begin

      // 计算屏幕滚动补偿
      shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
      shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

      if not FixedEffect then begin
         // 绘制飞行中的斧头
         img := FlyImageBase + Dir16 * 10;  // 根据方向选择图像
         d := ImgLib.GetCachedImage (img + curframe, px, py);
         if d <> nil then begin
            // 不使用Alpha混合，直接绘制
            surface.Draw (FlyX + px - UNITX div 2 - shx,
                          FlyY + py - UNITY div 2 - shy,
                          d.ClientRect, d, TRUE);
         end;
      end else begin
         {//정지, 도끼에 찍힌 모습.
         img := FlyImageBase + Dir16 * 10;
         d := ImgLib.GetCachedImage (img, px, py);
         if d <> nil then begin
            //알파블랭딩하지 않음
            surface.Draw (FlyX + px - UNITX div 2,
                          FlyY + py - UNITY div 2,
                          d.ClientRect, d, TRUE);
         end;  }
      end;
   end;
end;


{------------------------------------------------------------}
{ TFlyingArrow类实现 - 飞箭特效 }
{------------------------------------------------------------}

{ TFlyingArrow.DrawEff
  功能: 绘制飞箭特效
  特点:
    - 不使用Alpha混合
    - 箭矢图像索引不乘以10，直接使用方向值
    - Y坐标有额外偏移(-46)用于调整箭矢高度 }
procedure TFlyingArrow.DrawEff (surface: TTexture);
var
   img: integer;
   d: TTexture;
   shx, shy: integer;
begin
   // 只有距离超过40像素时才绘制
   if Active and ((Abs(FlyX-fireX) > 40) or (Abs(FlyY-fireY) > 40)) then begin
      // 计算屏幕滚动补偿
      shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
      shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

      if not FixedEffect then begin
         // 绘制飞行中的箭矢
         // 注意：箭矢图像索引不乘以10，直接使用方向值
         img := FlyImageBase + Dir16;
         d := ImgLib.GetCachedImage (img + curframe, px, py);
         if d <> nil then begin
            // 不使用Alpha混合，Y坐标有-46的高度调整
            surface.Draw (FlyX + px - UNITX div 2 - shx,
                          FlyY + py - UNITY div 2 - shy - 46,
                          d.ClientRect, d, TRUE);
         end;
      end;
   end;
end;

{--------------------------------------------------------}
{ TCharEffect类实现 - 角色特效 }
{--------------------------------------------------------}

{ TCharEffect.Create
  功能: 创建角色特效实例
  参数:
    effbase - 效果图像基础索引
    effframe - 动画帧数
    target - 目标角色对象 }
constructor TCharEffect.Create (effbase, effframe: integer; target: TObject);
begin
   // 调用父类构造器，起始和目标位置都设为目标角色位置
   inherited Create (111, effbase,
                     TActor(target).XX, TActor(target).YY,
                     TActor(target).XX, TActor(target).YY,
                     mtExplosion,
                     FALSE,
                     0);
   TargetActor := target;      // 保存目标角色引用
   frame := effframe;          // 设置动画帧数
   NextFrameTime := 30;        // 帧间隔30毫秒
end;

{ TCharEffect.Run
  功能: 运行角色特效，更新帧进度
  返回值: True表示继续运行，False表示动画结束 }
function  TCharEffect.Run: Boolean;
begin
   Result := TRUE;
   // 检查是否到达下一帧时间
   if GetTickCount - steptime > longword(NextFrameTime) then begin
      steptime := GetTickCount;
      Inc (curframe);
      // 检查是否播放完毕
      if curframe > start+frame-1 then begin
         curframe := start+frame-1;  // 停留在最后一帧
         Result := FALSE;  // 动画结束
      end;
   end;
end;

{ TCharEffect.DrawEff
  功能: 绘制角色特效
  特点: 特效位置跟随目标角色移动 }
procedure TCharEffect.DrawEff (surface: TTexture);
var
   d: TTexture;
begin
   if TargetActor <> nil then begin
      // 获取目标角色当前位置
      Rx := TActor(TargetActor).Rx;
      Ry := TActor(TargetActor).Ry;
      PlayScene.ScreenXYfromMCXY (Rx, Ry, FlyX, FlyY);
      // 加上角色的偏移量
      FlyX := FlyX + TActor(TargetActor).ShiftX;
      FlyY := FlyY + TActor(TargetActor).ShiftY;
      // 绘制特效图像
      d := ImgLib.GetCachedImage (EffectBase + curframe, px, py);
      if d <> nil then begin
         DrawBlend (surface,
                    FlyX + px - UNITX div 2,
                    FlyY + py - UNITY div 2,
                    d);
      end;
   end;
end;


{--------------------------------------------------------}
{ TMapEffect类实现 - 地图特效 }
{--------------------------------------------------------}

{ TMapEffect.Create
  功能: 创建地图特效实例
  参数:
    effbase - 效果图像基础索引
    effframe - 动画帧数
    x, y - 地图坐标 }
constructor TMapEffect.Create (effbase, effframe: integer; x, y: integer);
begin
   // 调用父类构造器，起始和目标位置相同
   inherited Create (111, effbase,
                     x, y,
                     x, y,
                     mtExplosion,
                     FALSE,
                     0);
   TargetActor := nil;         // 无目标角色
   frame := effframe;          // 设置动画帧数
   NextFrameTime := 30;        // 帧间隔30毫秒
   RepeatCount := 0;           // 默认不重复
end;

{ TMapEffect.Run
  功能: 运行地图特效，支持重复播放
  返回值: True表示继续运行，False表示动画结束 }
function  TMapEffect.Run: Boolean;
begin
   Result := TRUE;
   if GetTickCount - steptime > longword(NextFrameTime) then begin
      steptime := GetTickCount;
      Inc (curframe);
      if curframe > start+frame-1 then begin
         curframe := start+frame-1;
         // 检查是否需要重复播放
         if RepeatCount > 0 then begin
            Dec (RepeatCount);    // 减少重复计数
            curframe := start;    // 重新开始
         end else
            Result := FALSE;      // 动画结束
      end;
   end;
end;

{ TMapEffect.DrawEff
  功能: 绘制地图特效
  特点: 特效位置固定在地图坐标上 }
procedure TMapEffect.DrawEff (surface: TTexture);
var
   d: TTexture;
begin
   // 将地图坐标转换为屏幕坐标
   Rx := TargetX;
   Ry := TargetY;
   PlayScene.ScreenXYfromMCXY (Rx, Ry, FlyX, FlyY);
   // 绘制特效图像
   d := ImgLib.GetCachedImage (EffectBase + curframe, px, py);
   if d <> nil then begin
      DrawBlend (surface,
                 FlyX + px - UNITX div 2,
                 FlyY + py - UNITY div 2,
                 d);
   end;
end;


{--------------------------------------------------------}
{ TScrollHideEffect类实现 - 卷轴隐身特效 }
{--------------------------------------------------------}

{ TScrollHideEffect.Create
  功能: 创建卷轴隐身特效实例
  参数:
    effbase - 效果图像基础索引
    effframe - 动画帧数
    x, y - 坐标
    target - 要隐身的目标角色 }
constructor TScrollHideEffect.Create (effbase, effframe: integer; x, y: integer; target: TObject);
begin
   inherited Create (effbase, effframe, x, y);
   TargetCret := TActor(target);  // 保存目标角色引用
end;

{ TScrollHideEffect.Run
  功能: 运行隐身特效，在特定帧删除目标角色
  实现: 当播放到第7帧时，从场景中删除目标角色 }
function  TScrollHideEffect.Run: Boolean;
begin
   Result := inherited Run;
   // 在第7帧时删除目标角色
   if frame = 7 then
      if TargetCret <> nil then
         PlayScene.DeleteActor (TargetCret.RecogId);
end;


{--------------------------------------------------------}
{ TLightingEffect类实现 - 照明效果（未完成） }
{--------------------------------------------------------}

{ TLightingEffect.Create
  功能: 创建照明效果实例
  注意: 当前未实现 }
constructor TLightingEffect.Create (effbase, effframe: integer; x, y: integer);
begin
   // 未实现
end;

{ TLightingEffect.Run
  功能: 运行照明效果
  注意: 当前未实现 }
function  TLightingEffect.Run: Boolean;
begin
   // 未实现
end;


{--------------------------------------------------------}
{ TFireGunEffect类实现 - 喷火特效 }
{--------------------------------------------------------}

{ TFireGunEffect.Create
  功能: 创建喷火特效实例
  参数:
    effbase - 效果图像基础索引
    sx, sy - 起始位置
    tx, ty - 目标位置 }
constructor TFireGunEffect.Create (effbase, sx, sy, tx, ty: integer);
begin
   inherited Create (111, effbase,
                     sx, sy,
                     tx, ty,
                     mtFireGun,
                     TRUE,
                     0);
   NextFrameTime := 50;         // 帧间隔50毫秒
   FillChar (FireNodes, sizeof(TFireNode)*FIREGUNFRAME, #0);  // 初始化火焰节点
   OutofOil := FALSE;           // 火焰未耗尽
   firetime := GetTickCount;    // 记录开始时间
end;

{ TFireGunEffect.Run
  功能: 运行喷火特效，管理火焰节点的生成和消失
  返回值: True表示继续运行，False表示所有火焰已消失
  实现原理:
    1. 在未耗尽时，不断生成新的火焰节点
    2. 耗尽后，等待所有火焰节点消失
    3. 当距离施法者超过5格或超过800毫秒时耗尽 }
function  TFireGunEffect.Run: Boolean;
var
   i, fx, fy: integer;
   allgone: Boolean;    // 所有火焰是否已消失
begin
   Result := TRUE;
   if GetTickCount - steptime > longword(NextFrameTime) then begin
      Shift;
      steptime := GetTickCount;
      
      if not OutofOil then begin
         // 火焰未耗尽，继续生成新火焰
         // 检查是否超出范围或超时
         if (abs(RX-TActor(MagOwner).RX) >= 5) or (abs(RY-TActor(MagOwner).RY) >= 5) or (GetTickCount - firetime > 800) then
            OutofOil := TRUE;  // 设置耗尽标志
         
         // 将现有火焰节点向后移动
         for i:=FIREGUNFRAME-2 downto 0 do begin
            FireNodes[i].FireNumber := FireNodes[i].FireNumber + 1;
            FireNodes[i+1] := FireNodes[i];
         end;
         // 在当前位置生成新的火焰节点
         FireNodes[0].FireNumber := 1;
         FireNodes[0].x := FlyX;
         FireNodes[0].y := FlyY;
      end else begin
         // 火焰已耗尽，等待现有火焰消失
         allgone := TRUE;
         for i:=FIREGUNFRAME-2 downto 0 do begin
            if FireNodes[i].FireNumber <= FIREGUNFRAME then begin
               FireNodes[i].FireNumber := FireNodes[i].FireNumber + 1;
               FireNodes[i+1] := FireNodes[i];
               allgone := FALSE;  // 还有火焰未消失
            end;
         end;
         if allgone then Result := FALSE;  // 所有火焰已消失，特效结束
      end;
   end;
end;

{ TFireGunEffect.DrawEff
  功能: 绘制喷火特效
  实现: 遍历所有火焰节点，绘制有效的火焰图像 }
procedure TFireGunEffect.DrawEff (surface: TTexture);
var
   i, num, shx, shy, firex, firey, prx, pry, img: integer;
   d: TTexture;
begin
   prx := -1;  // 前一个绘制位置（避免重复绘制）
   pry := -1;
   
   // 遍历所有火焰节点
   for i:=0 to FIREGUNFRAME-1 do begin
      if (FireNodes[i].FireNumber <= FIREGUNFRAME) and (FireNodes[i].FireNumber > 0) then begin
         // 计算屏幕滚动补偿
         shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
         shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

         // 根据火焰编号选择图像
         img := EffectBase + (FireNodes[i].FireNumber - 1);
         d := ImgLib.GetCachedImage (img, px, py);
         if d <> nil then begin
            firex := FireNodes[i].x + px - UNITX div 2 - shx;
            firey := FireNodes[i].y + py - UNITY div 2 - shy;
            // 避免在相同位置重复绘制
            if (firex <> prx) or (firey <> pry) then begin
               prx := firex;
               pry := firey;
               DrawBlend (surface, firex, firey, d);
            end;
         end;
      end;
   end;
end;

{--------------------------------------------------------}
{ TThuderEffect类实现 - 雷击特效 }
{--------------------------------------------------------}

{ TThuderEffect.Create
  功能: 创建雷击特效实例
  参数:
    effbase - 效果图像基础索引（10=普通雷击，其他=火雷）
    tx, ty - 目标位置
    target - 目标角色 }
constructor TThuderEffect.Create (effbase, tx, ty: integer; target: TObject);
begin
   // 根据效果索引选择雷击类型
   if (effbase = 10) then begin
     // 普通雷击效果
     inherited Create (111, effbase,
                       tx, ty,
                       tx, ty,
                       mtThunder,
                       FALSE,
                       0);
   end else begin
     // 火雷效果
     inherited Create (111, effbase,
                       tx, ty,
                       tx, ty,
                       mtFireThunder,
                       FALSE,
                       0);
   end;
   TargetActor := target;  // 保存目标角色引用
end;

{ TThuderEffect.DrawEff
  功能: 绘制雷击特效 }
procedure TThuderEffect.DrawEff (surface: TTexture);
var
   img, px, py: integer;
   d: TTexture;
begin
   img := EffectBase;
   d := ImgLib.GetCachedImage (img + curframe, px, py);
   if d <> nil then begin
      DrawBlend (surface,
                 FlyX + px - UNITX div 2,
                 FlyY + py - UNITY div 2,
                 d);
   end;
end;


{--------------------------------------------------------}
{ TLightingThunder类实现 - 闪电雷击特效 }
{--------------------------------------------------------}

{ TLightingThunder.Create
  功能: 创建闪电雷击特效实例
  参数:
    effbase - 效果图像基础索引
    sx, sy - 起始位置（施法者位置）
    tx, ty - 目标位置
    target - 目标角色 }
constructor TLightingThunder.Create (effbase, sx, sy, tx, ty: integer; target: TObject);
begin
   inherited Create (111, effbase,
                     sx, sy,
                     tx, ty,
                     mtLightingThunder,
                     FALSE,
                     0);
   TargetActor := target;  // 保存目标角色引用
end;

{ TLightingThunder.DrawEff
  功能: 绘制闪电雷击特效
  实现: 在施法者位置绘制闪电链条效果 }
procedure TLightingThunder.DrawEff (surface: TTexture);
var
   img, sx, sy, px, py, shx, shy: integer;
   d: TTexture;
begin
   // 根据方向选择图像
   img := EffectBase + Dir16 * 10;
   
   // 只绘制前6帧
   if curframe < 6 then begin
      // 计算屏幕滚动补偿
      shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
      shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

      d := ImgLib.GetCachedImage (img + curframe, px, py);
      if d <> nil then begin
         // 在施法者位置绘制
         PlayScene.ScreenXYfromMCXY (TActor(MagOwner).RX,
                                     TActor(MagOwner).RY,
                                     sx,
                                     sy);
         DrawBlend (surface,
                    sx + px - UNITX div 2,
                    sy + py - UNITY div 2,
                    d);
      end;
   end;
   {if (curframe < 10) and (TargetActor <> nil) then begin
      d := ImgLib.GetCachedImage (EffectBase + 17*10 + curframe, px, py);
      if d <> nil then begin
         PlayScene.ScreenXYfromMCXY (TActor(TargetActor).RX,
                                     TActor(TargetActor).RY,
                                     sx,
                                     sy);
         DrawBlend (surface,
                    sx + px - UNITX div 2,
                    sy + py - UNITY div 2,
                    d, 1);
      end;
   end;}
end;


{--------------------------------------------------------}
{ TExploBujaukEffect类实现 - 爆炸符咒特效 }
{--------------------------------------------------------}

{ TExploBujaukEffect.Create
  功能: 创建爆炸符咒特效实例
  参数:
    effbase - 效果图像基础索引
    sx, sy - 起始位置
    tx, ty - 目标位置
    target - 目标角色 }
constructor TExploBujaukEffect.Create (effbase, sx, sy, tx, ty: integer; target: TObject);
begin
   inherited Create (111, effbase,
                     sx, sy,
                     tx, ty,
                     mtExploBujauk,
                     TRUE,
                     0);
   frame := 3;                  // 飞行动画3帧
   TargetActor := target;       // 保存目标角色
   NextFrameTime := 50;         // 帧间隔50毫秒
end;

{ TExploBujaukEffect.DrawEff
  功能: 绘制爆炸符咒特效
  实现:
    - 飞行阶段：绘制符纸飞行图像
    - 爆炸阶段：绘制爆炸效果 }
procedure TExploBujaukEffect.DrawEff (surface: TTexture);
var
   img: integer;
   d: TTexture;
   shx, shy: integer;
   meff: TMapEffect;
begin
   // 只有距离超过30像素或已爆炸时才绘制
   if Active and ((Abs(FlyX-fireX) > 30) or (Abs(FlyY-fireY) > 30) or FixedEffect) then begin

      // 计算屏幕滚动补偿
      shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
      shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

      if not FixedEffect then begin
         // 绘制飞行中的符纸
         img := EffectBase + Dir16 * 10;
         d := ImgLib.GetCachedImage (img + curframe, px, py);
         if d <> nil then begin
            // 不使用Alpha混合
            surface.Draw (FlyX + px - UNITX div 2 - shx,
                          FlyY + py - UNITY div 2 - shy,
                          d.ClientRect, d, TRUE);
         end;
      end else begin
         // 绘制爆炸效果
         img := MagExplosionBase + curframe;
         d := ImgLib.GetCachedImage (img, px, py);
         if d <> nil then begin
            DrawBlend (surface,
                       FLyX + px - UNITX div 2,
                       FlyY + py - UNITY div 2,
                       d);
         end;
      end;
   end;
end;

{--------------------------------------------------------}
{ TBujaukGroundEffect类实现 - 符咒地面效果 }
{--------------------------------------------------------}

{ TBujaukGroundEffect.Create
  功能: 创建符咒地面效果实例
  参数:
    effbase - 效果图像基础索引
    magicnumb - 魔法编号，用于区分不同爆炸效果
    sx, sy - 起始位置
    tx, ty - 目标位置 }
constructor TBujaukGroundEffect.Create (effbase, magicnumb, sx, sy, tx, ty: integer);
begin
   inherited Create (111, effbase,
                     sx, sy,
                     tx, ty,
                     mtBujaukGroundEffect,
                     TRUE,
                     0);
   frame := 3;                  // 飞行动画3帧
   MagicNumber := magicnumb;    // 保存魔法编号
   BoGroundEffect := FALSE;     // 未触发地面效果
   NextFrameTime := 50;         // 帧间隔50毫秒
end;

{ TBujaukGroundEffect.Run
  功能: 运行符咒地面效果
  实现: 检测是否到达目标位置，触发爆炸效果 }
function  TBujaukGroundEffect.Run: Boolean;
begin
   Result := inherited Run;
   if not FixedEffect then begin
      // 检测是否到达目标
      if ((abs(TargetX-FlyX) <= 15) and (abs(TargetY-FlyY) <= 15)) or
         ((abs(TargetX-FlyX) >= prevdisx) and (abs(TargetY-FlyY) >= prevdisy)) then begin
         FixedEffect := TRUE;  // 切换为爆炸状态
         start := 0;
         frame := ExplosionFrame;
         curframe := start;
         Repetition := FALSE;
         // 播放爆炸音效
         PlaySound (TActor(MagOwner).magicexplosionsound);

         Result := TRUE;
      end else begin
         // 记录当前距离供下次比较
         prevdisx := abs(TargetX-FlyX);
         prevdisy := abs(TargetY-FlyY);
      end;
   end;
end;

{ TBujaukGroundEffect.DrawEff
  功能: 绘制符咒地面效果
  实现:
    - 飞行阶段：绘制符纸飞行图像
    - 爆炸阶段：根据魔法编号选择不同的爆炸效果 }
procedure TBujaukGroundEffect.DrawEff (surface: TTexture);
var
   img: integer;
   d: TTexture;
   shx, shy: integer;
   meff: TMapEffect;
begin
   // 只有距离超过30像素或已爆炸时才绘制
   if Active and ((Abs(FlyX-fireX) > 30) or (Abs(FlyY-fireY) > 30) or FixedEffect) then begin

      // 计算屏幕滚动补偿
      shx := (Myself.RX*UNITX + Myself.ShiftX) - FireMyselfX;
      shy := (Myself.RY*UNITY + Myself.ShiftY) - FireMyselfY;

      if not FixedEffect then begin
         // 绘制飞行中的符纸
         img := EffectBase + Dir16 * 10;
         d := ImgLib.GetCachedImage (img + curframe, px, py);
         if d <> nil then begin
            // 不使用Alpha混合
            surface.Draw (FlyX + px - UNITX div 2 - shx,
                          FlyY + py - UNITY div 2 - shy,
                          d.ClientRect, d, TRUE);
         end;
      end else begin
         // 绘制爆炸效果 - 根据魔法编号选择不同图像
         if MagicNumber = 11 then  // 魔法版爆炸
            img := EffectBase + 16 * 10 + curframe
         else                      // 普通版爆炸
            img := EffectBase + 18 * 10 + curframe;
         d := ImgLib.GetCachedImage (img, px, py);
         if d <> nil then begin
            DrawBlend (surface,
                       FLyX + px - UNITX div 2,
                       FlyY + py - UNITY div 2,
                       d);
         end;

         {if not BoGroundEffect and (curframe = 8) then begin
            BoGroundEffect := TRUE;
            meff := TMapEffect.Create (img+2, 6, TargetRx, TargetRy);
            meff.NextFrameTime := 100;
            //meff.RepeatCount := 1;
            PlayScene.GroundEffectList.Add (meff);
         end; }
      end;
   end;
end;



{--------------------------------------------------------}
{ TNormalDrawEffect类实现 - 普通绘制效果 }
{--------------------------------------------------------}

{ TNormalDrawEffect.Create
  功能: 创建普通绘制效果实例
  参数:
    xx, yy - 坐标位置
    iLib - 图像库引用
    eff_base - 效果图像基础索引
    eff_frame - 动画帧数
    eff_time - 帧间隔（毫秒）
    blending - 是否使用Alpha混合 }
constructor TNormalDrawEffect.Create (xx, yy: integer;
                                       iLib: TGameImages;
                                       eff_base: integer;
                                       eff_frame: integer;
                                       eff_time: integer;
                                       blending: Boolean);
begin
   inherited Create (111, eff_base,
                     xx, yy,
                     xx, yy,
                     mtReady,  // 类型未使用
                     TRUE,
                     0);
   ImgLib := ilib;              // 图像库引用
   EffectBase := eff_base;      // 图像起始索引
   start := 0;                  // 起始帧
   curframe := 0;               // 当前帧
   frame := eff_frame;          // 总帧数
   NextFrameTime := eff_time;   // 帧间隔
   BoBlending := blending;      // 是否混合
end;

{ TNormalDrawEffect.Run
  功能: 运行普通绘制效果
  返回值: True表示继续运行，False表示动画播放完毕 }
function TNormalDrawEffect.Run: Boolean;
begin
   Result := TRUE;
   if Active then begin
      // 检查是否到达下一帧时间
      if GetTickCount - steptime > longword(NextFrameTime) then begin
         steptime := GetTickCount;
         Inc (curframe);
         // 检查是否播放完毕
         if curframe > start+frame-1 then begin
            curframe := start;    // 重置到起始帧
            Result := FALSE;      // 动画结束
         end;
      end;
   end;
end;

{ TNormalDrawEffect.DrawEff
  功能: 绘制普通效果
  实现: 根据BoBlending标志选择使用Alpha混合或直接绘制 }
procedure TNormalDrawEffect.DrawEff (surface: TTexture);
var
   img, sx, sy, px, py, shx, shy: integer;
   d: TTexture;
begin
   img := EffectBase + curframe;  // 计算当前帧图像索引

   d := ImgLib.GetCachedImage (img, px, py);
   if d <> nil then begin
      // 将地图坐标转换为屏幕坐标
      PlayScene.ScreenXYfromMCXY (FlyX,
                                  FlyY,
                                  sx,
                                  sy);
      if BoBlending then begin
         // 使用Alpha混合绘制
         DrawBlend (surface,
                    sx + px - UNITX div 2,
                    sy + py - UNITY div 2,
                    d);
      end else begin
         // 直接绘制（不混合）
         surface.Draw (
                    sx + px - UNITX div 2,
                    sy + py - UNITY div 2,
                    d.ClientRect,
                    d,
                    TRUE);
      end;
   end;
end;

{--------------------------------------------------------}
{ TFlyingFireBall类实现 - 飞行火球特效 }
{ 创建日期: 2003/02/11 }
{--------------------------------------------------------}

{ TFlyingFireBall.Create
  功能: 创建飞行火球特效实例
  实现: 直接调用父类构造器 }
constructor TFlyingFireBall.Create (id, effnum, sx, sy, tx, ty: integer; mtype: TMagicType; Recusion: Boolean; anitime: integer);
begin
   inherited Create (id, effnum, sx, sy, tx, ty, mtype, Recusion, anitime);
end;

{ TFlyingFireBall.DrawEff
  功能: 绘制飞行火球特效
  特点:
    - 使用Alpha混合绘制
    - 根据当前飞行方向动态选择图像 }
procedure TFlyingFireBall.DrawEff (surface: TTexture);
var
   img, tdir : integer;  // 图像索引和方向
   d: TTexture;
begin
   // 只有距离超过准备阈值时才绘制
   if Active and ((Abs(FlyX-fireX) > ReadyFrame) or (Abs(FlyY-fireY) > ReadyFrame)) then begin
      // 根据当前位置和目标位置计算8方向
      tdir := GetFlyDirection(FlyX, FlyY, TargetX, TargetY);
      img := FlyImageBase + tdir * 10;  // 根据方向选择图像
      d := ImgLib.GetCachedImage (img + curframe, px, py);
      if d <> nil then begin
         // 使用Alpha混合绘制火球
         DrawBlend (surface,
                    FLyX + px - UNITX div 2,
                    FlyY + py - UNITY div 2,
                    d);
      end;
   end;
end;

end.

