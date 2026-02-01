{ ============================================================================
  单元名称: ObjMon2
  功能描述: 特殊怪物类定义单元
  
  主要功能:
  - 定义固定型怪物(TStickMonster)及其派生类
  - 定义蜂巢怪物(TBeeQueen)和蜘蛛巢怪物(TSpiderHouseMonster)
  - 定义蜈蚣王怪物(TCentipedeKingMonster)
  - 定义心脏怪物(TBigHeartMonster)
  - 定义栗子树怪物(TBamTreeMonster)
  - 定义自爆蜘蛛(TExplosionSpider)
  - 定义城堡守卫单位(TGuardUnit)及其派生类
  - 定义城门(TCastleDoor)和城墙(TWallStructure)
  - 定义足球(TSoccerBall)
  
  作者: [原作者]
  创建日期: [创建日期]
  修改日期: [最后修改日期]
============================================================================ }
unit ObjMon2;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Dialogs,
  ScktComp, syncobjs, MudUtil, HUtil32, Grobal2, Envir, EdCode, ObjBase,
  M2Share, Event, ObjMon;


type
   { 门状态枚举类型
     功能: 定义城门的三种状态
     dsOpen - 门打开状态
     dsClose - 门关闭状态
     dsBroken - 门损坏状态 }
   TDoorState = (dsOpen, dsClose, dsBroken);

   { TStickMonster类 - 固定型怪物
     功能: 实现可以隐藏在地下并突然出现攻击玩家的怪物
     用途: 作为食人草等固定位置怪物的基类 }
   TStickMonster = class (TAnimal)
   private
   protected
      RunDone: Boolean;        // 运行完成标志
      DigupRange: integer;     // 出现范围(玩家进入此范围时怪物出现)
      DigdownRange: integer;   // 消失范围(玩家离开此范围时怪物消失)
      { AttackTarget - 攻击目标
        返回值: 是否成功攻击 }
      function  AttackTarget: Boolean; dynamic;
      { CheckComeOut - 检查是否应该出现
        功能: 检测玩家是否进入出现范围 }
      procedure CheckComeOut; dynamic;
      { ComeOut - 从地下出现
        功能: 怪物从隐藏状态变为可见状态 }
      procedure ComeOut; dynamic;
      { ComeDown - 返回地下
        功能: 怪物从可见状态变为隐藏状态 }
      procedure ComeDown; dynamic;
   public
      constructor Create;
      destructor Destroy; override;
      procedure RunMsg (msg: TMessageInfo); override;
      procedure Run; override;
   end;

   { TBeeQueen类 - 蜂巢怪物(蜜蜂女王)
     功能: 可以生成小蜜蜂子怪物的母体怪物
     用途: 作为能够召唤子怪物的蜂巢类怪物 }
   TBeeQueen = class (TAnimal)
   private
      childlist: TList;  // 已生成的子怪物列表
   protected
      { MakeChildBee - 生成子蜜蜂
        功能: 创建新的蜜蜂子怪物 }
      procedure MakeChildBee;
   public
      constructor Create;
      destructor Destroy; override;
      procedure RunMsg (msg: TMessageInfo); override;
      procedure Run; override;
   end;

   { TCentipedeKingMonster类 - 蜈蚣王/触龙神
     功能: 实现蜈蚣王BOSS怪物的特殊行为
     用途: 作为地下出现的大型BOSS怪物，具有范围攻击和中毒能力 }
   TCentipedeKingMonster = class (TStickMonster)
   private
      appeartime: longword;  // 出现时间记录
   protected
      { FindTarget - 查找目标
        返回值: 是否找到有效目标 }
      function  FindTarget: Boolean;
      { AttackTarget - 攻击目标(重写)
        功能: 实现范围攻击和中毒效果
        返回值: 是否成功攻击 }
      function  AttackTarget: Boolean; override;
   public
      constructor Create;
      { ComeOut - 从地下出现(重写)
        功能: 出现时恢复满血 }
      procedure ComeOut; override;
      procedure Run; override;
   end;

   { TBigHeartMonster类 - 赤月魔/心脏怪物
     功能: 实现大范围攻击的心脏类怪物
     用途: 作为赤月地图的特殊怪物，具有超远攻击范围 }
   TBigHeartMonster = class (TAnimal)
   private
   protected
      { AttackTarget - 攻击目标
        功能: 对视野范围内所有敌人进行攻击
        返回值: 是否成功攻击 }
      function  AttackTarget: Boolean; dynamic;
   public
      constructor Create;
      procedure Run; override;
   end;

   { TBamTreeMonster类 - 栗子树怪物
     功能: 实现需要被攻击特定次数才会死亡的树形怪物
     用途: 作为特殊的资源采集类怪物 }
   TBamTreeMonster = class (TAnimal)
   public
      StruckCount: integer;       // 当前被攻击次数
      DeathStruckCount: integer;  // 死亡所需攻击次数(基于HP)
      constructor Create;
      { Struck - 被攻击处理(重写)
        参数: hiter - 攻击者 }
      procedure Struck (hiter: TCreature); override;
      procedure Run; override;
   end;


   { TSpiderHouseMonster类 - 蜘蛛巢怪物(爆眼蜘蛛巢)
     功能: 可以生成小蜘蛛子怪物的母体怪物
     用途: 作为能够召唤自爆蜘蛛的蜘蛛巢 }
   TSpiderHouseMonster = class (TAnimal)
   private
      childlist: TList;  // 已生成的子怪物列表
   protected
      { MakeChildSpider - 生成子蜘蛛
        功能: 创建新的蜘蛛子怪物 }
      procedure MakeChildSpider;
   public
      constructor Create;
      destructor Destroy; override;
      procedure RunMsg (msg: TMessageInfo); override;
      procedure Run; override;
   end;

   { TExplosionSpider类 - 自爆蜘蛛
     功能: 实现接近目标后自爆造成范围伤害的蜘蛛
     用途: 作为蜘蛛巢生成的自杀式攻击怪物 }
   TExplosionSpider = class (TMonster)
   public
      maketime: longword;  // 创建时间(用于超时自爆)
      constructor Create;
      { DoSelfExplosion - 执行自爆
        功能: 对周围敌人造成范围伤害并死亡 }
      procedure DoSelfExplosion;
      { AttackTarget - 攻击目标(重写)
        功能: 接近目标后触发自爆
        返回值: 是否成功攻击 }
      function  AttackTarget: Boolean; override;
      procedure Run; override;
   end;


   { ============================================================
     守卫单位类定义区域
     包含: 守卫、城门、弓箭手等城堡防御单位
   ============================================================ }

   { TGuardUnit类 - 守卫单位基类
     功能: 作为所有城堡守卫单位的基类
     用途: 提供守卫单位的基本属性和行为 }
   TGuardUnit = class (TAnimal)
      OriginX: integer;    // 原始X坐标
      OriginY: integer;    // 原始Y坐标
      OriginDir: integer;  // 原始朝向
   public
      { Struck - 被攻击处理(重写)
        参数: hiter - 攻击者
        功能: 标记攻击者为城堡罪犯 }
      procedure Struck (hiter: TCreature); override;
      { IsProperTarget - 判断是否为有效目标(重写)
        参数: target - 目标生物
        返回值: 是否应该攻击该目标 }
      function  IsProperTarget (target: TCreature): Boolean; override;
   end;

   { TArcherGuard类 - 弓箭手守卫
     功能: 实现远程射箭攻击的守卫
     用途: 作为城堡的远程防御单位 }
   TArcherGuard = class (TGuardUnit)
   private
      { ShotArrow - 射箭攻击
        参数: targ - 攻击目标
        功能: 向目标发射箭矢造成伤害 }
      procedure ShotArrow (targ: TCreature);
   public
      constructor Create;
      procedure Run; override;
   end;

   { TArcherPolice类 - 弓箭手警察
     功能: 特殊的弓箭手守卫，不能被和平模式攻击
     用途: 作为城镇的治安维护单位 }
   TArcherPolice = class (TArcherGuard)
   private
   public
      constructor Create;
   end;

   { TCastleDoor类 - 城门
     功能: 实现可开关、可损坏、可修复的城门
     用途: 作为城堡的主要入口防御设施 }
   TCastleDoor = class (TGuardUnit)
   public
      BrokenTime: longword;  // 损坏时间记录
      BoOpenState: Boolean;  // 门是否处于打开状态
      constructor Create;
      procedure Run; override;
      procedure Initialize; override;
      procedure Die; override;
      { RepairStructure - 修复城门
        功能: 根据当前HP更新城门外观 }
      procedure RepairStructure;
      { ActiveDoorWall - 激活门墙状态
        参数: dstate - 门状态(打开/关闭/损坏)
        功能: 设置门周围格子的可通行性 }
      procedure ActiveDoorWall (dstate: TDoorState);
      { OpenDoor - 打开城门
        功能: 将城门设置为打开状态，允许通行 }
      procedure OpenDoor;
      { CloseDoor - 关闭城门
        功能: 将城门设置为关闭状态，阻止通行 }
      procedure CloseDoor;
   end;

   { TWallStructure类 - 城墙结构
     功能: 实现可损坏、可修复的城墙
     用途: 作为城堡的防御墙体 }
   TWallStructure = class (TGuardUnit)
   public
      BrokenTime: longword;  // 损坏时间记录
      BoBlockPos: Boolean;   // 是否阻挡位置(用于控制可通行性)
      constructor Create;
      procedure Initialize; override;
      procedure Die; override;
      { RepairStructure - 修复城墙
        功能: 根据当前HP更新城墙外观 }
      procedure RepairStructure;
      procedure Run; override;
   end;


   { TSoccerBall类 - 足球
     功能: 实现可被踢动的足球对象
     用途: 作为游戏中的娱乐互动对象 }
   TSoccerBall = class (TAnimal)
   public
      GoPower: integer;  // 移动力量(决定滚动距离)
      constructor Create;
      { Struck - 被攻击处理(重写)
        参数: hiter - 踢球者
        功能: 根据踢球者方向和力量设置球的移动 }
      procedure Struck (hiter: TCreature); override;
      procedure Run; override;
   end;



implementation

uses
   svMain, Castle, Guild;


{ TStickMonster.Create - 固定型怪物构造函数
  功能: 初始化固定型怪物的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置视野范围、运行间隔、搜索频率等基本属性
    3. 设置出现/消失范围
    4. 设置为隐藏模式和固定模式
  注意事项:
    - 初始状态为隐藏模式，玩家接近时才出现
    - BoAnimal为TRUE表示击杀后可掉落食人草叶/果实 }
constructor TStickMonster.Create;
begin
   inherited Create;
   RunDone := FALSE;                              // 运行完成标志初始为FALSE
   ViewRange := 7;                                // 视野范围为7格
   RunNextTick := 250;                            // 运行间隔250毫秒
   SearchRate := 2500 + longword(Random(1500));   // 搜索频率随机设置(2500-4000毫秒)
   SearchTime := GetTickCount;                    // 记录当前时间作为搜索起始时间
   RaceServer := RC_KILLINGHERB;                  // 设置种族为食人草类
   DigupRange := 4;                               // 出现范围为4格
   DigdownRange := 4;                             // 消失范围为4格
   HideMode := TRUE;                              // 初始为隐藏模式
   StickMode := TRUE;                             // 固定模式，不会移动
   BoAnimal := TRUE;                              // 动物类型，击杀后掉落食人草叶/果实
end;

{ TStickMonster.Destroy - 固定型怪物析构函数
  功能: 释放固定型怪物占用的资源 }
destructor TStickMonster.Destroy;
begin
   inherited Destroy;
end;

{ TStickMonster.AttackTarget - 攻击目标
  功能: 尝试攻击当前目标
  返回值: 是否成功攻击或目标在攻击范围内
  实现原理:
    1. 检查目标是否存在
    2. 判断目标是否在攻击范围内
    3. 如果在范围内且攻击冷却已结束，执行攻击
    4. 如果不在范围内，设置目标坐标或失去目标
  注意事项:
    - 当目标不在同一地图时会失去目标(TargetCret变为nil) }
function  TStickMonster.AttackTarget: Boolean;
var
   targdir: byte;  // 攻击方向
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      // 检查目标是否在攻击范围内
      if TargetInAttackRange (TargetCret, targdir) then begin
         // 检查攻击冷却是否结束
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;           // 更新攻击时间
            TargetFocusTime := GetTickCount;     // 更新目标聚焦时间
            Attack (TargetCret, targdir);        // 执行攻击
         end;
         Result := TRUE;
      end else begin
         // 目标不在攻击范围内
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)  // 设置目标坐标
         else
            LoseTarget;  // 目标不在同一地图，失去目标(TargetCret变为nil)
      end;
   end;
end;

{ TStickMonster.ComeOut - 从地下出现
  功能: 怪物从隐藏状态变为可见状态
  实现原理:
    1. 取消隐藏模式
    2. 发送挖出消息通知客户端播放出现动画 }
procedure TStickMonster.ComeOut;
begin
   HideMode := FALSE;                            // 取消隐藏模式
   SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');    // 发送挖出消息
end;

{ TStickMonster.ComeDown - 返回地下
  功能: 怪物从可见状态变为隐藏状态
  实现原理:
    1. 发送挖下消息通知客户端播放消失动画
    2. 清理可见角色列表，释放内存
    3. 设置为隐藏模式
  注意事项:
    - 清理可见角色列表时需要异常处理 }
procedure TStickMonster.ComeDown;
var
   i: integer;
begin
   SendRefMsg (RM_DIGDOWN, Dir, CX, CY, 0, '');  // 发送挖下消息
   try
      // 清理可见角色列表，释放内存
      for i:=0 to VisibleActors.Count-1 do
         Dispose (PTVisibleActor(VisibleActors[i]));
      VisibleActors.Clear;
   except
      MainOutMessage ('[Exception] TStickMonster VisbleActors Dispose(..)');
   end;
   HideMode := TRUE;  // 设置为隐藏模式
end;

{ TStickMonster.CheckComeOut - 检查是否应该出现
  功能: 检测是否有玩家进入出现范围
  实现原理:
    1. 遍历可见角色列表
    2. 检查每个角色是否为有效目标且在出现范围内
    3. 如果找到符合条件的目标，调用ComeOut出现
  注意事项:
    - 隐藏模式的玩家不会触发怪物出现(除非BoViewFixedHide为TRUE) }
procedure TStickMonster.CheckComeOut;
var
   i: integer;
   cret: TCreature;
begin
   // 遍历可见角色列表
   for i:=0 to VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
      // 检查目标是否有效: 未死亡、是有效目标、未隐藏或可视固定隐藏
      if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
         // 检查目标是否在出现范围内
         if (abs(CX-cret.CX) <= DigupRange) and (abs(CY-cret.CY) <= DigupRange) then begin
            ComeOut;  // 从地下出现，变为可见状态
            break;
         end;
      end;
   end;
end;

{ TStickMonster.RunMsg - 处理消息
  参数: msg - 消息信息
  功能: 处理固定型怪物接收到的消息 }
procedure TStickMonster.RunMsg (msg: TMessageInfo);
begin
   inherited RunMsg (msg);
end;

{ TStickMonster.Run - 主运行循环
  功能: 处理固定型怪物的主要逻辑
  实现原理:
    1. 检查怪物状态(非幽灵、未死亡、未石化)
    2. 如果处于隐藏模式，检查是否应该出现
    3. 如果已出现，处理攻击和返回地下的逻辑
    4. 当目标离开消失范围时返回地下 }
procedure TStickMonster.Run;
var
   boidle: Boolean;  // 是否处于空闲状态
begin
   // 检查怪物状态: 非幽灵、未死亡、未石化
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      // 检查行走冷却是否结束
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         WalkTime := GetCurrentTime;
         if HideMode then begin
            // 隐藏模式: 检查是否应该出现
            CheckComeOut;
         end else begin
            // 已出现模式: 处理攻击逻辑
            if GetCurrentTime - HitTime > NextHitTime then begin
               // 攻击冷却结束，执行普通攻击
               MonsterNormalAttack;
            end;

            // 检查是否应该返回地下
            boidle := FALSE;
            if TargetCret <> nil then begin
               // 目标离开消失范围时设置为空闲
               if (abs(TargetCret.CX-CX) > DigdownRange) or (abs(TargetCret.CY-CY) > DigdownRange) then
                  boidle := TRUE;
            end else boidle := TRUE;  // 无目标时为空闲

            if boidle then
               ComeDown  // 空闲时返回地下
            else
               if AttackTarget then begin
                  // 攻击成功后直接返回
                  inherited Run;
                  exit;
               end;
         end;
      end;
   end;

   inherited Run;

end;



{--------------------------------------------------------------}
{ TBeeQueen类实现 - 蜂巢怪物(蜜蜂女王) }
{--------------------------------------------------------------}

{ TBeeQueen.Create - 蜂巢怪物构造函数
  功能: 初始化蜂巢怪物的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置视野范围、运行间隔、搜索频率等基本属性
    3. 设置为固定模式(不会移动)
    4. 创建子怪物列表 }
constructor TBeeQueen.Create;
begin
   inherited Create;
   ViewRange := 9;                                // 视野范围为9格
   RunNextTick := 250;                            // 运行间隔250毫秒
   SearchRate := 2500 + longword(Random(1500));   // 搜索频率随机设置(2500-4000毫秒)
   SearchTime := GetTickCount;                    // 记录当前时间作为搜索起始时间
   StickMode := TRUE;                             // 固定模式，不会移动
   childlist := TList.Create;                     // 创建子怪物列表
end;

{ TBeeQueen.Destroy - 蜂巢怪物析构函数
  功能: 释放蜂巢怪物占用的资源 }
destructor TBeeQueen.Destroy;
begin
   childlist.Free;      // 释放子怪物列表
   inherited Destroy;
end;

{ TBeeQueen.MakeChildBee - 生成子蜜蜂
  功能: 创建新的蜜蜂子怪物
  实现原理:
    1. 检查子怪物数量是否小于15
    2. 发送攻击动作消息
    3. 延迟500毫秒后发送生成蜜蜂消息
  注意事项:
    - 最多同时存在15只子蜜蜂 }
procedure TBeeQueen.MakeChildBee;
begin
   if childlist.Count < 15 then begin
      SendRefMsg (RM_HIT, self.Dir, CX, CY, 0, '');              // 发送攻击动作消息
      SendDelayMsg (self, RM_ZEN_BEE, 0, 0, 0, 0, '', 500);      // 延迟500毫秒后生成蜜蜂
   end;
end;

{ TBeeQueen.RunMsg - 处理消息
  参数: msg - 消息信息
  功能: 处理蜂巢怪物接收到的消息
  实现原理:
    1. 处理RM_ZEN_BEE消息，创建蜜蜂子怪物
    2. 将新创建的蜜蜂设置为攻击当前目标
    3. 将蜜蜂添加到子怪物列表 }
procedure TBeeQueen.RunMsg (msg: TMessageInfo);
var
   nx, ny: integer;
   monname: string;
   mon: TCreature;
begin
   case msg.Ident of
      RM_ZEN_BEE:  // 生成蜜蜂消息
         begin
            monname := __Bee;  // 蜜蜂怪物名称
            // 在当前位置创建蜜蜂
            mon := UserEngine.AddCreatureSysop (PEnvir.MapName, CX, CY, monname);
            if mon <> nil then begin
               mon.SelectTarget (TargetCret);    // 设置蜜蜂攻击当前目标
               childlist.Add (mon);              // 添加到子怪物列表
            end;
         end;
   end;
   inherited RunMsg (msg);
end;

{ TBeeQueen.Run - 主运行循环
  功能: 处理蜂巢怪物的主要逻辑
  实现原理:
    1. 检查怪物状态(非幽灵、未死亡、未石化)
    2. 执行普通攻击逻辑
    3. 如果有目标则生成子蜜蜂
    4. 清理已死亡或已消失的子怪物 }
procedure TBeeQueen.Run;
var
   i: integer;
begin
   // 检查怪物状态: 非幽灵、未死亡、未石化
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      // 检查行走冷却是否结束
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         WalkTime := GetCurrentTime;
         // 检查攻击冷却是否结束
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetTickCount;
            MonsterNormalAttack;                 // 执行普通攻击

            // 如果有目标则生成子蜜蜂
            if TargetCret <> nil then
               MakeChildBee;

         end;

         // 清理已死亡或已消失的子怪物(从后向前遍历以安全删除)
         for i:=childlist.Count-1 downto 0 do begin
            if (TCreature(childlist[i]).Death) or (TCreature(childlist[i]).BoGhost) then begin
               childlist.Delete(i);
            end;
         end;
      end;
   end;

   inherited Run;
end;



{--------------------------------------------------------------}
{ TCentipedeKingMonster类实现 - 蜈蚣王/触龙神 }
{--------------------------------------------------------------}

{ TCentipedeKingMonster.Create - 蜈蚣王构造函数
  功能: 初始化蜈蚣王怪物的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置视野范围、出现/消失范围
    3. 设置为非动物类型(不掉落动物类物品)
    4. 记录出现时间 }
constructor TCentipedeKingMonster.Create;
begin
   inherited Create;
   ViewRange := 6;                    // 视野范围为6格
   DigupRange := 4;                   // 出现范围为4格
   DigdownRange := 6;                 // 消失范围为6格
   BoAnimal := FALSE;                 // 非动物类型
   appeartime := GetTickCount;        // 记录出现时间
end;

{ TCentipedeKingMonster.FindTarget - 查找目标
  功能: 在视野范围内查找有效目标
  返回值: 是否找到有效目标
  实现原理:
    1. 遍历可见角色列表
    2. 检查每个角色是否为有效目标且在视野范围内
    3. 找到第一个符合条件的目标即返回TRUE }
function  TCentipedeKingMonster.FindTarget: Boolean;
var
   i: integer;
   cret: TCreature;
begin
   Result := FALSE;
   // 遍历可见角色列表
   for i:=0 to VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
      // 检查目标是否有效且在视野范围内
      if (not cret.Death) and IsProperTarget(cret) then begin
         if (abs(CX-cret.CX) <= ViewRange) and (abs(CY-cret.CY) <= ViewRange) then begin
            Result := TRUE;
            break;
         end;
      end;
   end;
end;

{ TCentipedeKingMonster.AttackTarget - 攻击目标(重写)
  功能: 对视野范围内所有敌人进行范围攻击
  返回值: 是否找到目标并攻击
  实现原理:
    1. 调用FindTarget查找目标
    2. 检查攻击冷却是否结束
    3. 计算攻击伤害
    4. 对视野内所有有效目标发送延迟魔法伤害
    5. 随机使目标中毒(体力下降或石化)
  注意事项:
    - 25%概率使目标中毒
    - 中毒类型: 2/3概率为体力下降，1/3概率为石化 }
function  TCentipedeKingMonster.AttackTarget: Boolean;
var
   i, pwr: integer;
   cret: TCreature;
   targdir: byte;
begin
   Result := FALSE;
   if FindTarget then begin
      // 检查攻击冷却是否结束
      if GetCurrentTime - HitTime > NextHitTime then begin
         HitTime := GetCurrentTime;
         HitMotion (RM_HIT, self.Dir, CX, CY);    // 发送攻击动作

         // 计算攻击伤害: DC最小值 + 随机值(0~DC差值)
         with WAbil do
            pwr := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);

         // 遍历可见角色列表，对所有有效目标造成伤害
         for i:=0 to VisibleActors.Count-1 do begin
            cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
            if (not cret.Death) and IsProperTarget(cret) then begin
               if (abs(CX-cret.CX) <= ViewRange) and (abs(CY-cret.CY) <= ViewRange) then begin
                  TargetFocusTime := GetTickCount;    // 更新目标聚焦时间

                  // 发送延迟魔法伤害消息(600毫秒后生效)
                  SendDelayMsg (self, RM_DELAYMAGIC, pwr, MakeLong(cret.CX, cret.CY), 2, integer(cret), '', 600);
                  
                  // 25%概率使目标中毒
                  if Random(4) = 0 then begin
                     if Random(3) <> 0 then
                        cret.MakePoison (POISON_DECHEALTH, 60, 3)   // 2/3概率: 体力下降毒(持续60秒，等级3)
                     else
                        cret.MakePoison (POISON_STONE, 5, 0);       // 1/3概率: 石化毒(持续5秒)
                  end;

                  TargetCret := cret;                 // 设置为当前目标
               end;
            end;
         end;
      end;
      Result := TRUE;
   end;
end;

{ TCentipedeKingMonster.ComeOut - 从地下出现(重写)
  功能: 怪物出现时恢复满血
  实现原理:
    1. 调用父类的ComeOut方法
    2. 将HP恢复为最大值 }
procedure TCentipedeKingMonster.ComeOut;
begin
   inherited ComeOut;
   WAbil.HP := WAbil.MaxHP;   // 重新出现时恢复满血
end;


{ TCentipedeKingMonster.Run - 主运行循环(蜈蚣王/触龙神)
  功能: 处理蜈蚣王怪物的主要逻辑
  实现原理:
    1. 检查怪物状态(非幽灵、未死亡、未石化)
    2. 隐藏模式: 等待10秒后检查是否应该出现
    3. 出现模式: 等待3秒后开始攻击，10秒无敌人则返回地下
  注意事项:
    - 出现后有3秒的保护时间不会攻击
    - 出现后10秒内无敌人会返回地下 }
procedure TCentipedeKingMonster.Run;
var
   i, dis, d: integer;
   cret, nearcret: TCreature;
begin
   // 检查怪物状态: 非幽灵、未死亡、未石化
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      // 检查行走冷却是否结束
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         WalkTime := GetCurrentTime;
         if HideMode then begin
            // 隐藏模式: 等待10秒后检查是否应该出现
            if GetTickCount - appeartime > 10 * 1000 then begin
               for i:=0 to VisibleActors.Count-1 do begin
                  cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
                  // 检查目标是否有效且在出现范围内
                  if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
                     if (abs(CX-cret.CX) <= DigupRange) and (abs(CY-cret.CY) <= DigupRange) then begin
                        ComeOut;                      // 从地下出现
                        appeartime := GetTickCount;   // 记录出现时间
                        break;
                     end;
                  end;
               end;
            end;
         end else begin
            // 出现模式: 等待3秒后开始攻击
            if GetTickCount - appeartime > 3 * 1000 then begin
               if AttackTarget then begin
                  inherited Run;
                  exit;
               end else begin
                  // 无敌人且出现超10秒，返回地下
                  if GetTickCount - appeartime > 10 * 1000 then begin
                     ComeDown;                        // 返回地下
                     appeartime := GetTickCount;      // 记录返回时间
                  end;
               end;
            end;
         end;
      end;
   end;

   inherited Run;
end;


{--------------------------------------------------------------}
{ TBigHeartMonster类实现 - 赤月魔/心脏怪物 }
{--------------------------------------------------------------}

{ TBigHeartMonster.Create - 心脏怪物构造函数
  功能: 初始化心脏怪物的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置超大视野范围(16格)
    3. 设置为非动物类型 }
constructor TBigHeartMonster.Create;
begin
   inherited Create;
   ViewRange := 16;       // 视野范围为16格(超远攻击范围)
   BoAnimal := FALSE;     // 非动物类型
end;

{ TBigHeartMonster.AttackTarget - 攻击目标
  功能: 对视野范围内所有敌人进行攻击
  返回值: 是否成功攻击
  实现原理:
    1. 检查攻击冷却是否结束
    2. 计算攻击伤害
    3. 遍历可见角色列表，对所有有效目标发送延迟魔法伤害
    4. 发送心跳特效消息
  注意事项:
    - 攻击范围为1格(单体攻击)
    - 伤害延迟200毫秒生效 }
function  TBigHeartMonster.AttackTarget: Boolean;
var
   i, pwr: integer;
   cret: TCreature;
   ev2: TEvent;
begin
   Result := FALSE;
   // 检查攻击冷却是否结束
   if GetCurrentTime - HitTime > NextHitTime then begin
      HitTime := GetCurrentTime;
      HitMotion (RM_HIT, self.Dir, CX, CY);    // 发送攻击动作

      // 计算攻击伤害: DC最小值 + 随机值(0~DC差值)
      with WAbil do
         pwr := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);

      // 遍历可见角色列表，对所有有效目标造成伤害
      for i:=0 to VisibleActors.Count-1 do begin
         cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
         if (not cret.Death) and IsProperTarget(cret) then begin
            if (abs(CX-cret.CX) <= ViewRange) and (abs(CY-cret.CY) <= ViewRange) then begin

               // 发送延迟魔法伤害消息(200毫秒后生效，范围1格)
               SendDelayMsg (self, RM_DELAYMAGIC, pwr, MakeLong(cret.CX, cret.CY), 1, integer(cret), '', 200);
               // 发送心跳特效消息
               SendRefMsg (RM_NORMALEFFECT, 0, cret.CX, cret.CY, NE_HEARTPALP, '');

               // 以下为注释掉的攻击效果/痕迹代码
               //ev2 := TEvent (PEnvir.GetEvent (cret.CX, cret.CY));
               //if ev2 = nil then begin
               //   ev2 := TPileStones.Create (PEnvir, cret.CX, cret.CY, ET_HEARTPALP, 3 * 60 * 1000, TRUE);
               //   EventMan.AddEvent (ev2);
               //end;


            end;
         end;
      end;

      Result := TRUE;
   end;
end;

{ TBigHeartMonster.Run - 主运行循环
  功能: 处理心脏怪物的主要逻辑
  实现原理:
    1. 检查怪物状态(非幽灵、未死亡、未石化)
    2. 如果有可见角色则执行攻击
  注意事项:
    - 只要有可见角色就会尝试攻击，不需要特定目标 }
procedure TBigHeartMonster.Run;
begin
   // 检查怪物状态: 非幽灵、未死亡、未石化
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      // 如果有可见角色则执行攻击
      if VisibleActors.Count > 0 then
         AttackTarget;
   end;
   inherited Run;
end;


{--------------------------------------------------------------}
{ TBamTreeMonster类实现 - 栗子树怪物 }
{--------------------------------------------------------------}

{ TBamTreeMonster.Create - 栗子树怪物构造函数
  功能: 初始化栗子树怪物的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置为非动物类型
    3. 初始化攻击计数器 }
constructor TBamTreeMonster.Create;
begin
   inherited Create;
   BoAnimal := FALSE;         // 非动物类型
   StruckCount := 0;          // 当前被攻击次数初始为0
   DeathStruckCount := 0;     // 死亡所需攻击次数(将基于HP设置)
end;

{ TBamTreeMonster.Struck - 被攻击处理(重写)
  参数: hiter - 攻击者
  功能: 记录被攻击次数
  实现原理:
    1. 调用父类的Struck方法
    2. 增加被攻击次数计数器 }
procedure TBamTreeMonster.Struck (hiter: TCreature);
begin
   inherited Struck (hiter);
   Inc (StruckCount);         // 增加被攻击次数
end;

{ TBamTreeMonster.Run - 主运行循环
  功能: 处理栗子树怪物的主要逻辑
  实现原理:
    1. 初始化死亡所需攻击次数(基于MaxHP)
    2. 保持HP为满血状态
    3. 当被攻击次数达到阈值时，设置HP为0触发死亡
  注意事项:
    - 该怪物不会因为伤害而死亡，只会因为被攻击次数达到阈值而死亡 }
procedure TBamTreeMonster.Run;
begin
   // 初始化死亡所需攻击次数(基于MaxHP)
   if DeathStruckCount = 0 then
      DeathStruckCount := WAbil.MaxHP;
   // 保持HP为满血状态(不会因伤害死亡)
   WAbil.HP := WAbil.MaxHP;

   // 当被攻击次数达到阈值时，设置HP为0触发死亡
   if StruckCount >= DeathStruckCount then
      WAbil.HP := 0;
      
   inherited Run;
end;



{--------------------------------------------------------------}
{ TSpiderHouseMonster类实现 - 蜘蛛巢怪物(爆眼蜘蛛巢) }
{--------------------------------------------------------------}

{ TSpiderHouseMonster.Create - 蜘蛛巢怪物构造函数
  功能: 初始化蜘蛛巢怪物的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置视野范围、运行间隔、搜索频率等基本属性
    3. 设置为固定模式(不会移动)
    4. 创建子怪物列表 }
constructor TSpiderHouseMonster.Create;
begin
   inherited Create;
   ViewRange := 9;                                // 视野范围为9格
   RunNextTick := 250;                            // 运行间隔250毫秒
   SearchRate := 2500 + longword(Random(1500));   // 搜索频率随机设置(2500-4000毫秒)
   SearchTime := GetTickCount;                    // 记录当前时间作为搜索起始时间
   StickMode := TRUE;                             // 固定模式，不会移动
   childlist := TList.Create;                     // 创建子怪物列表
end;

{ TSpiderHouseMonster.Destroy - 蜘蛛巢怪物析构函数
  功能: 释放蜘蛛巢怪物占用的资源 }
destructor TSpiderHouseMonster.Destroy;
begin
   childlist.Free;      // 释放子怪物列表
   inherited Destroy;
end;

{ TSpiderHouseMonster.MakeChildSpider - 生成子蜘蛛
  功能: 创建新的蜘蛛子怪物
  实现原理:
    1. 检查子怪物数量是否小于15
    2. 发送攻击动作消息
    3. 延迟500毫秒后发送生成蜘蛛消息
  注意事项:
    - 最多同时存在15只子蜘蛛 }
procedure TSpiderHouseMonster.MakeChildSpider;
begin
   if childlist.Count < 15 then begin
      SendRefMsg (RM_HIT, self.Dir, CX, CY, 0, '');              // 发送攻击动作消息
      SendDelayMsg (self, RM_ZEN_BEE, 0, 0, 0, 0, '', 500);      // 延迟500毫秒后生成蜘蛛
   end;
end;

{ TSpiderHouseMonster.RunMsg - 处理消息
  参数: msg - 消息信息
  功能: 处理蜘蛛巢怪物接收到的消息
  实现原理:
    1. 处理RM_ZEN_BEE消息，创建蜘蛛子怪物
    2. 在蜘蛛巢下方位置(CY+1)创建蜘蛛
    3. 将新创建的蜘蛛设置为攻击当前目标
    4. 将蜘蛛添加到子怪物列表 }
procedure TSpiderHouseMonster.RunMsg (msg: TMessageInfo);
var
   nx, ny: integer;
   monname: string;
   mon: TCreature;
begin
   case msg.Ident of
      RM_ZEN_BEE:  // 生成蜘蛛消息(复用蜜蜂消息类型)
         begin
            monname := __Spider;  // 自爆蜘蛛怪物名称

            // 根据蜘蛛巢位置计算子蜘蛛生成位置(下方一格)
            nx := CX;
            ny := CY+1;

            // 检查生成位置是否可通行
            if PEnvir.CanWalk (nx, ny, TRUE) then begin
               mon := UserEngine.AddCreatureSysop (PEnvir.MapName, nx, ny, monname);
               if mon <> nil then begin
                  mon.SelectTarget (TargetCret);    // 设置蜘蛛攻击当前目标
                  childlist.Add (mon);              // 添加到子怪物列表
               end;
            end;
         end;
   end;
   inherited RunMsg (msg);
end;

{ TSpiderHouseMonster.Run - 主运行循环
  功能: 处理蜘蛛巢怪物的主要逻辑
  实现原理:
    1. 检查怪物状态(非幽灵、未死亡、未石化)
    2. 执行普通攻击逻辑
    3. 如果有目标则生成子蜘蛛
    4. 清理已死亡或已消失的子怪物 }
procedure TSpiderHouseMonster.Run;
var
   i: integer;
begin
   // 检查怪物状态: 非幽灵、未死亡、未石化
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      // 检查行走冷却是否结束
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         WalkTime := GetCurrentTime;
         // 检查攻击冷却是否结束
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetTickCount;
            MonsterNormalAttack;                 // 执行普通攻击

            // 如果有目标则生成子蜘蛛
            if TargetCret <> nil then
               MakeChildSpider;

         end;

         // 清理已死亡或已消失的子怪物(从后向前遍历以安全删除)
         for i:=childlist.Count-1 downto 0 do begin
            if (TCreature(childlist[i]).Death) or (TCreature(childlist[i]).BoGhost) then begin
               childlist.Delete(i);
            end;
         end;
      end;
   end;

   inherited Run;
end;



{--------------------------------------------------------------}
{ TExplosionSpider类实现 - 自爆蜘蛛 }
{--------------------------------------------------------------}

{ TExplosionSpider.Create - 自爆蜘蛛构造函数
  功能: 初始化自爆蜘蛛的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置视野范围、运行间隔、搜索频率等基本属性
    3. 记录创建时间(用于超时自爆) }
constructor TExplosionSpider.Create;
begin
   inherited Create;
   ViewRange := 5;                                // 视野范围为5格
   RunNextTick := 250;                            // 运行间隔250毫秒
   SearchRate := 2500 + longword(Random(1500));   // 搜索频率随机设置(2500-4000毫秒)
   SearchTime := 0;                               // 搜索时间初始为0
   maketime := GetTickCount;                      // 记录创建时间
end;

{ TExplosionSpider.DoSelfExplosion - 执行自爆
  功能: 对周围敌人造成范围伤害并死亡
  实现原理:
    1. 设置HP为0触发死亡
    2. 计算攻击伤害
    3. 遍历可见角色列表，对周围1格内的有效目标造成伤害
    4. 伤害分为物理伤害和魔法伤害各占一半
  注意事项:
    - 自爆范围为1格(周围8个方向)
    - 伤害延迟700毫秒生效 }
procedure TExplosionSpider.DoSelfExplosion;
var
   i, pwr, dam: integer;
   cret: TCreature;
begin
   WAbil.HP := 0;  // 设置HP为0触发死亡

   // 计算攻击伤害: DC最小值 + 随机值(0~DC差值)
   with WAbil do
      pwr := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);

   // 遍历可见角色列表，对周围1格内的有效目标造成伤害
   for i:=0 to VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
      // 检查目标是否在自爆范围内(1格)
      if (abs(cret.CX-CX) <= 1) and (abs(cret.CY-CY) <= 1) then begin
         if (not cret.Death) and (IsProperTarget(cret)) then begin
            dam := 0;
            // 计算物理伤害(伤害的一半)
            dam := dam + cret.GetHitStruckDamage (self, pwr div 2);
            // 计算魔法伤害(伤害的一半)
            dam := dam + cret.GetMagStruckDamage (self, pwr div 2);
            if dam > 0 then begin
               cret.StruckDamage (dam, self);    // 应用伤害
               // 发送延迟受击消息(700毫秒后生效)
               cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam,
                        cret.WAbil.HP, cret.WAbil.MaxHP, Longint(self), '', 700);
            end;
         end;
      end;
   end;
end;

{ TExplosionSpider.AttackTarget - 攻击目标(重写)
  功能: 接近目标后触发自爆
  返回值: 是否成功攻击或目标在攻击范围内
  实现原理:
    1. 检查目标是否存在
    2. 判断目标是否在攻击范围内
    3. 如果在范围内且攻击冷却已结束，执行自爆
    4. 如果不在范围内，设置目标坐标或失去目标
  注意事项:
    - 当目标不在同一地图时会失去目标(TargetCret变为nil) }
function  TExplosionSpider.AttackTarget: Boolean;
var
   targdir: byte;  // 攻击方向
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      // 检查目标是否在攻击范围内
      if TargetInAttackRange (TargetCret, targdir) then begin
         // 检查攻击冷却是否结束
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;           // 更新攻击时间
            TargetFocusTime := GetTickCount;     // 更新目标聚焦时间
            DoSelfExplosion;                     // 执行自爆
         end;
         Result := TRUE;
      end else begin
         // 目标不在攻击范围内
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)  // 设置目标坐标
         else
            LoseTarget;  // 目标不在同一地图，失去目标(TargetCret变为nil)
      end;
   end;
end;

{ TExplosionSpider.Run - 主运行循环
  功能: 处理自爆蜘蛛的主要逻辑
  实现原理:
    1. 检查怪物状态(未死亡、非幽灵)
    2. 如果存活超过1分钟则自动自爆
  注意事项:
    - 自爆蜘蛛最多存活1分钟，超时会自动自爆 }
procedure TExplosionSpider.Run;
begin
   // 检查怪物状态: 未死亡、非幽灵
   if (not Death) and (not BoGhost) then begin
      // 如果存活超过1分钟则自动自爆
      if GetTickCount - maketime > 1 * 60 * 1000 then begin
         maketime := GetTickCount;
         DoSelfExplosion;                        // 执行自爆
      end;
   end;
   inherited Run;
end;


{--------------------------------------------------------------}
{ TGuardUnit类实现 - 守卫单位基类 }
{--------------------------------------------------------------}

{ TGuardUnit.Struck - 被攻击处理(重写)
  参数: hiter - 攻击者
  功能: 标记攻击者为城堡罪犯
  实现原理:
    1. 调用父类的Struck方法
    2. 如果属于城堡，则标记攻击者为城堡罪犯 }
procedure TGuardUnit.Struck (hiter: TCreature);
begin
   inherited Struck (hiter);
   // 如果属于城堡，标记攻击者为城堡罪犯
   if Castle <> nil then begin
      hiter.BoCrimeforCastle := TRUE;             // 标记为城堡罪犯
      hiter.CrimeforCastleTime := GetTickCount;   // 记录犯罪时间
   end;
end;

{ TGuardUnit.IsProperTarget - 判断是否为有效目标(重写)
  参数: target - 目标生物
  返回值: 是否应该攻击该目标
  功能: 判断目标是否为守卫应该攻击的有效目标
  实现原理:
    1. 如果属于城堡:
       - 攻击过自己的人是有效目标
       - 城堡罪犯(2分钟内)是有效目标
       - 攻城战期间所有人都是有效目标
       - 城主行会及盟友不是有效目标
       - 运营者、石化状态、NPC、自己、同城堡成员不是有效目标
    2. 如果不属于城堡:
       - 攻击过自己的人是有效目标
       - 攻击弓箭手的人是有效目标
       - PK红名玩家是有效目标
       - 运营者、石化状态、自己不是有效目标 }
function  TGuardUnit.IsProperTarget (target: TCreature): Boolean;
begin
   Result := FALSE;
   // 属于城堡的守卫判断逻辑
   if Castle <> nil then begin
      // 攻击过自己的人是有效目标
      if LastHiter = target then Result := TRUE;

      // 检查是否为城堡罪犯
      if target.BoCrimeforCastle then begin
         // 犯罪时间在2分钟内为有效目标
         if GetTickCount - target.CrimeforCastleTime < 2 * 60 * 1000 then begin
            Result := TRUE;
         end else
            target.BoCrimeforCastle := FALSE;     // 超时取消罪犯标记
         // 如果目标也属于城堡，取消罪犯标记
         if TCreature(target).Castle <> nil then begin
            target.BoCrimeforCastle := FALSE;
            Result := FALSE;
         end;
      end;

      // 基本攻击模式: 攻城战期间攻击所有敌人
      if TUserCastle(Castle).BoCastleUnderAttack then begin
         Result := TRUE;
      end;

      // 检查是否为城主行会或盟友
      if TUserCastle(Castle).OwnerGuild <> nil then begin
         if target.Master = nil then begin
            // 目标无主人: 检查是否为城主行会或盟友
            if ((TUserCastle(Castle).OwnerGuild = target.MyGuild) or
               TUserCastle(Castle).OwnerGuild.IsAllyGuild (TGuild(target.MyGuild))) and
               (LastHiter <> target)
            then
               Result := FALSE;                   // 城主行会或盟友不攻击
         end else begin
            // 目标有主人: 检查主人是否为城主行会或盟友
            if ((TUserCastle(Castle).OwnerGuild = target.Master.MyGuild) or
               TUserCastle(Castle).OwnerGuild.IsAllyGuild (TGuild(target.Master.MyGuild))) and
               (LastHiter <> target.Master) and
               (LastHiter <> target)
            then
               Result := FALSE;                   // 主人为城主行会或盟友不攻击
         end;
      end;
      
      // 排除特殊目标: 运营者、石化状态、NPC、自己、同城堡成员
      if target.BoSysopMode or
         target.BoStoneMode or
         (target.RaceServer >= RC_NPC) and (target.RaceServer < RC_ANIMAL) or
         (target = self) or
         (TCreature(target).Castle = self.Castle)
      then
          Result := FALSE;

   end else begin
      // 不属于城堡的守卫判断逻辑

      // 攻击过自己的人是有效目标
      if LastHiter = target then Result := TRUE;

      // 攻击弓箭手的人是有效目标
      if target.TargetCret <> nil then
         if target.TargetCret.RaceServer = RC_ARCHERGUARD then
            Result := TRUE;

      // PK红名玩家(级别>=2)是有效目标
      if target.PKLevel >= 2 then begin
         Result := TRUE;
      end;

      // 排除特殊目标: 运营者、石化状态、自己
      if target.BoSysopMode or target.BoStoneMode or (target = self) then
          Result := FALSE;
   end;
end;



{--------------------------------------------------------------}
{ TArcherGuard类实现 - 弓箭手守卫 }
{--------------------------------------------------------------}

{ TArcherGuard.Create - 弓箭手守卫构造函数
  功能: 初始化弓箭手守卫的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置超远视野范围(12格)
    3. 设置需要接收广播消息
    4. 设置种族为弓箭手守卫 }
constructor TArcherGuard.Create;
begin
   inherited Create;
   ViewRange := 12;                   // 视野范围为12格(远程攻击)
   WantRefMsg := TRUE;                // 需要接收广播消息
   Castle := nil;                     // 初始不属于任何城堡
   OriginDir := -1;                   // 原始朝向未设置
   RaceServer := RC_ARCHERGUARD;      // 设置种族为弓箭手守卫
end;

{ TArcherGuard.ShotArrow - 射箭攻击
  参数: targ - 攻击目标(必须不为nil)
  功能: 向目标发射箭矢造成伤害
  实现原理:
    1. 计算朝向目标的方向
    2. 计算攻击伤害
    3. 应用目标的物理防御计算实际伤害
    4. 设置目标的最后攻击者
    5. 发送延迟受击消息(根据距离计算延迟)
    6. 发送箭矢飞行消息
  注意事项:
    - 弓箭手击杀不给经验值(ExpHiter设为nil)
    - 伤害延迟根据距离计算: 600 + 距离*50毫秒 }
procedure TArcherGuard.ShotArrow (targ: TCreature);
var
   dam, armor: integer;
begin
   // 计算朝向目标的方向
   Dir := GetNextDirection (CX, CY, targ.CX, targ.CY);
   // 计算攻击伤害: DC最小值 + 随机值(0~DC差值)
   with WAbil do
      dam := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
   if dam > 0 then begin
      // 应用目标的物理防御计算实际伤害
      // 以下为注释掉的旧版本护甲计算代码
      //armor := (Lobyte(targ.WAbil.AC) + Random(ShortInt(Hibyte(targ.WAbil.AC)-Lobyte(targ.WAbil.AC)) + 1));
      //dam := dam - armor;
      //if dam <= 0 then
      //   if dam > -10 then dam := 1;
      dam := targ.GetHitStruckDamage (self, dam);
   end;
   if dam > 0 then begin
      targ.SetLastHiter (self);       // 设置目标的最后攻击者
      targ.ExpHiter := nil;           // 弓箭手击杀不给经验值
      targ.StruckDamage (dam, self);  // 应用伤害
      // 发送延迟受击消息(根据距离计算延迟)
      targ.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam,
               targ.WAbil.HP, targ.WAbil.MaxHP, Longint(self), '', 600 + _MAX(Abs(CX-targ.CX),Abs(CY-targ.CY)) * 50);
   end;
   // 发送箭矢飞行消息
   SendRefMsg (RM_FLYAXE, Dir, CX, CY, Integer(targ), '');
end;

{ TArcherGuard.Run - 主运行循环
  功能: 处理弓箭手守卫的主要逻辑
  实现原理:
    1. 检查怪物状态(未死亡、非幽灵、未石化)
    2. 遍历可见角色列表，查找最近的有效目标
    3. 如果有目标则射箭攻击
    4. 如果无目标则返回原始朝向 }
procedure TArcherGuard.Run;
var
   i, d, dis: integer;
   cret, nearcret: TCreature;
begin
   dis := 9999;                       // 初始化最近距离为最大值
   nearcret := nil;                   // 初始化最近目标为nil
   // 检查怪物状态: 未死亡、非幽灵、未石化
   if not Death and not BoGhost and (StatusArr[POISON_STONE] = 0) then begin
      // 检查行走冷却是否结束
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         WalkTime := GetCurrentTime;
         // 遍历可见角色列表，查找最近的有效目标
         for i:=0 to VisibleActors.Count-1 do begin
            cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
            if (not cret.Death) and (IsProperTarget(cret)) then begin
               // 计算曼哈顿距离
               d := abs(CX-cret.CX) + abs(CY-cret.CY);
               if d < dis then begin
                  dis := d;
                  nearcret := cret;   // 记录最近目标
               end;
            end;
         end;
         // 选择最近目标或失去目标
         if nearcret <> nil then begin
            SelectTarget (nearcret);
         end else begin
            LoseTarget;
         end;
      end;
      // 如果有目标则射箭攻击
      if TargetCret <> nil then begin
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;
            ShotArrow (TargetCret);   // 射箭攻击目标
         end;
      end else begin
         // 无目标时返回原始朝向
         if OriginDir >= 0 then
            if OriginDir <> Dir then
               Turn (OriginDir);
      end;
   end;
   inherited Run;

end;



{--------------------------------------------------------------}
{ TArcherPolice类实现 - 弓箭手警察 }
{--------------------------------------------------------------}

{ TArcherPolice.Create - 弓箭手警察构造函数
  功能: 初始化弓箭手警察的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置种族为弓箭手警察(和平模式不能攻击) }
constructor TArcherPolice.Create;
begin
   inherited Create;
   RaceServer := RC_ARCHERPOLICE;  // 设置种族为弓箭手警察(和平模式不能攻击)
end;

{--------------------------------------------------------------}
{ TCastleDoor类实现 - 城门 }
{--------------------------------------------------------------}

{ TCastleDoor.Create - 城门构造函数
  功能: 初始化城门的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置为非动物类型和固定模式
    3. 初始状态为关闭
    4. 设置高抗毒属性 }
constructor TCastleDoor.Create;
begin
   inherited Create;
   BoAnimal := FALSE;         // 非动物类型
   StickMode := TRUE;         // 固定模式，不会移动
   BoOpenState := FALSE;      // 初始状态为关闭
   AntiPoison := 200;         // 高抗毒属性
   //HideMode := TRUE;        // 创建时为不可见模式(已注释)
end;

{ TCastleDoor.Initialize - 初始化城门
  功能: 初始化城门状态和周围格子的可通行性
  实现原理:
    1. 设置初始方向为0
    2. 调用父类初始化
    3. 根据HP和开关状态设置门墙状态 }
procedure TCastleDoor.Initialize;
begin
   Dir := 0;                  // 初始方向为0
   inherited Initialize;
   // 根据HP和开关状态设置门墙状态
   if WAbil.HP > 0 then begin
      if BoOpenState then ActiveDoorWall (dsOpen)    // 打开状态
      else ActiveDoorWall (dsClose);                  // 关闭状态
   end else
      ActiveDoorWall (dsBroken);                      // 损坏状态
end;

{ TCastleDoor.RepairStructure - 修复城门
  功能: 根据当前HP更新城门外观
  实现原理:
    1. 仅在关闭状态下更新
    2. 根据HP比例计算新方向(0-2表示不同损坏程度)
    3. 发送复活消息更新客户端显示 }
procedure TCastleDoor.RepairStructure;
var
   n, newdir: integer;
begin
   // 仅在关闭状态下更新
   if not BoOpenState then begin
      // 根据HP比例计算新方向: 0=完好, 1=轻微损坏, 2=严重损坏
      newdir := 3 - Round (WAbil.HP / WAbil.MaxHP * 3);
      if not (newdir in [0..2]) then newdir := 0;
      Dir := newdir;
      SendRefMsg (RM_ALIVE, Dir, CX, CY, 0, '');      // 发送复活消息更新显示
   end;
end;

{ TCastleDoor.ActiveDoorWall - 激活门墙状态
  参数: dstate - 门状态(打开/关闭/损坏)
  功能: 设置门周围格子的可通行性
  实现原理:
    1. 先设置部分格子为可通行
    2. 根据门状态设置主要格子的可通行性
    3. 打开状态时部分格子仍不可通行(门框)
  注意事项:
    - 仅用于沙巴克城门 }
procedure TCastleDoor.ActiveDoorWall (dstate: TDoorState);
var
   bomove: Boolean;           // 是否可移动
   begin
   // 先设置部分格子为可通行
   PEnvir.GetMarkMovement (CX, CY-2, TRUE);
   PEnvir.GetMarkMovement (CX+1, CY-1, TRUE);
   PEnvir.GetMarkMovement (CX+1, CY-2, TRUE);
   // 根据门状态决定是否可移动
   if dstate = dsClose then bomove := FALSE    // 关闭状态不可移动
   else bomove := TRUE;                         // 打开/损坏状态可移动

   // 设置门周围格子的可通行性
   PEnvir.GetMarkMovement (CX, CY, bomove);
   PEnvir.GetMarkMovement (CX, CY-1, bomove);
   PEnvir.GetMarkMovement (CX, CY-2, bomove);
   PEnvir.GetMarkMovement (CX+1, CY-1, bomove);
   PEnvir.GetMarkMovement (CX+1, CY-2, bomove);
   PEnvir.GetMarkMovement (CX-1, CY, bomove);
   PEnvir.GetMarkMovement (CX-2, CY, bomove);
   PEnvir.GetMarkMovement (CX-1, CY-1, bomove);
   PEnvir.GetMarkMovement (CX-1, CY+1, bomove);
   // 打开状态时门框仍不可通行
   if dstate = dsOpen then begin
      PEnvir.GetMarkMovement (CX, CY-2, FALSE);
      PEnvir.GetMarkMovement (CX+1, CY-1, FALSE);
      PEnvir.GetMarkMovement (CX+1, CY-2, FALSE);
   end;
end;

{ TCastleDoor.OpenDoor - 打开城门
  功能: 将城门设置为打开状态，允许通行
  实现原理:
    1. 检查城门是否已损坏
    2. 设置方向为7(不可见状态)
    3. 发送挖出消息通知客户端
    4. 设置为石化模式(不可被攻击)
    5. 激活门墙为打开状态
    6. 取消占位 }
procedure TCastleDoor.OpenDoor;
begin
   if not Death then begin
      Dir := 7;                       // 设置为不可见状态
      SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');      // 发送挖出消息
      BoOpenState := TRUE;            // 设置为打开状态
      BoStoneMode := TRUE;            // 石化模式(不可被攻击)
      ActiveDoorWall (dsOpen);        // 激活门墙为打开状态，允许通行
      HoldPlace := FALSE;             // 取消占位
   end;
end;

{ TCastleDoor.CloseDoor - 关闭城门
  功能: 将城门设置为关闭状态，阻止通行
  实现原理:
    1. 检查城门是否已损坏
    2. 根据HP计算方向(损坏程度)
    3. 发送挖下消息通知客户端
    4. 取消石化模式(可被攻击)
    5. 激活门墙为关闭状态
    6. 设置占位 }
procedure TCastleDoor.CloseDoor;
begin
   if not Death then begin
      // 根据HP计算方向: 0=完好, 1=轻微损坏, 2=严重损坏
      Dir := 3 - Round (WAbil.HP / WAbil.MaxHP * 3);
      if not (Dir in [0..2]) then Dir := 0;
      SendRefMsg (RM_DIGDOWN, Dir, CX, CY, 0, '');    // 发送挖下消息
      BoOpenState := FALSE;           // 设置为关闭状态
      BoStoneMode := FALSE;           // 取消石化模式(可被攻击)
      ActiveDoorWall (dsClose);       // 激活门墙为关闭状态，阻止通行
      HoldPlace := TRUE;              // 设置占位
   end;
end;

{ TCastleDoor.Die - 城门死亡处理
  功能: 处理城门被摧毁时的逻辑
  实现原理:
    1. 调用父类的Die方法
    2. 记录损坏时间
    3. 激活门墙为损坏状态，允许通行 }
procedure TCastleDoor.Die;
begin
   inherited Die;
   BrokenTime := GetTickCount;        // 记录损坏时间
   ActiveDoorWall (dsBroken);         // 激活门墙为损坏状态，允许通行
end;

{ TCastleDoor.Run - 主运行循环
  功能: 处理城门的主要逻辑
  实现原理:
    1. 如果已损坏且属于城堡，保持不消失
    2. 如果未损坏，禁止自动恢复体力
    3. 如果处于关闭状态，根据HP更新外观 }
procedure TCastleDoor.Run;
var
   n, newdir: integer;
begin
   // 已损坏且属于城堡时保持不消失
   if Death and (Castle <> nil) then begin
      DeathTime := GetTickCount;      // 持续更新死亡时间，防止消失
   end else
      HealthTick := 0;                // 禁止自动恢复体力

   // 关闭状态下根据HP更新外观
   if not BoOpenState then begin
      // 根据HP计算新方向: 0=完好, 1=轻微损坏, 2=严重损坏
      newdir := 3 - Round (WAbil.HP / WAbil.MaxHP * 3);
      // 如果方向变化则更新显示
      if (newdir <> Dir) and (newdir < 3) then begin
         Dir := newdir;
         SendRefMsg (RM_TURN, Dir, CX, CY, 0, '');    // 发送转向消息更新显示
      end;
   end;

   inherited Run;
end;



{--------------------------------------------------------------}
{ TWallStructure类实现 - 城墙结构 }
{--------------------------------------------------------------}

{ TWallStructure.Create - 城墙构造函数
  功能: 初始化城墙的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置为非动物类型和固定模式
    3. 初始不阻挡位置
    4. 设置高抗毒属性 }
constructor TWallStructure.Create;
begin
   inherited Create;
   BoAnimal := FALSE;         // 非动物类型
   StickMode := TRUE;         // 固定模式，不会移动
   BoBlockPos := FALSE;       // 初始不阻挡位置
   AntiPoison := 200;         // 高抗毒属性
   //HideMode := TRUE;        // 创建时为不可见模式(已注释)
end;

{ TWallStructure.Initialize - 初始化城墙
  功能: 初始化城墙状态
  实现原理:
    1. 设置初始方向为0
    2. 调用父类初始化 }
procedure TWallStructure.Initialize;
begin
   Dir := 0;                  // 初始方向为0
   inherited Initialize;
end;

{ TWallStructure.RepairStructure - 修复城墙
  功能: 根据当前HP更新城墙外观
  实现原理:
    1. 根据HP计算新方向(0-3表示不同损坏程度，4表示完全损坏)
    2. 发送复活消息更新客户端显示 }
procedure TWallStructure.RepairStructure;
var
   n, newdir: integer;
begin
   // 根据HP计算新方向: 0=完好, 1-3=不同损坏程度, 4=完全损坏
   if WAbil.HP > 0 then newdir := 3 - Round (WAbil.HP / WAbil.MaxHP * 3)
   else newdir := 4;
   if not (newdir in [0..4]) then newdir := 0;
   Dir := newdir;
   SendRefMsg (RM_ALIVE, Dir, CX, CY, 0, '');      // 发送复活消息更新显示
end;

{ TWallStructure.Die - 城墙死亡处理
  功能: 处理城墙被摧毁时的逻辑
  实现原理:
    1. 调用父类的Die方法
    2. 记录损坏时间 }
procedure TWallStructure.Die;
begin
   inherited Die;
   BrokenTime := GetTickCount;        // 记录损坏时间
end;

{ TWallStructure.Run - 主运行循环
  功能: 处理城墙的主要逻辑
  实现原理:
    1. 如果已损坏:
       - 保持不消失
       - 设置位置为可通行
    2. 如果未损坏:
       - 禁止自动恢复体力
       - 设置位置为不可通行
    3. 根据HP更新外观 }
procedure TWallStructure.Run;
var
   n, newdir: integer;
begin
   if Death then begin
      // 已损坏时保持不消失
      DeathTime := GetTickCount;      // 持续更新死亡时间，防止消失
      // 设置位置为可通行
      if BoBlockPos then begin
         PEnvir.GetMarkMovement (CX, CY, TRUE);   // 设置为可移动
         BoBlockPos := FALSE;
      end;
   end else begin
      // 未损坏时禁止自动恢复体力
      HealthTick := 0;
      // 设置位置为不可通行
      if not BoBlockPos then begin
         PEnvir.GetMarkMovement (CX, CY, FALSE);  // 设置为不可移动
         BoBlockPos := TRUE;
      end;
   end;

   // 根据HP计算新方向: 0=完好, 1-3=不同损坏程度, 4=完全损坏
   if WAbil.HP > 0 then newdir := 3 - Round (WAbil.HP / WAbil.MaxHP * 3)
   else newdir := 4;
   // 如果方向变化则更新显示(播放损坏动画)
   if (newdir <> Dir) and (newdir < 5) then begin
      Dir := newdir;
      SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');  // 发送挖出消息播放损坏动画
   end;

   inherited Run;
end;


{--------------------------------------------------------------}
{ TSoccerBall类实现 - 足球 }
{--------------------------------------------------------------}

{ TSoccerBall.Create - 足球构造函数
  功能: 初始化足球的基本属性
  实现原理:
    1. 调用父类构造函数
    2. 设置为非动物类型
    3. 设置为不会死亡
    4. 初始化移动力量和目标位置 }
constructor TSoccerBall.Create;
begin
   inherited Create;
   BoAnimal := FALSE;         // 非动物类型
   NeverDie := TRUE;          // 不会死亡
   GoPower := 0;              // 初始移动力量为0
   TargetX := -1;             // 初始目标X坐标为-1(无目标)
end;

{ TSoccerBall.Struck - 被攻击处理(重写)
  参数: hiter - 踢球者
  功能: 根据踢球者方向和力量设置球的移动
  实现原理:
    1. 设置球的方向为踢球者的方向
    2. 增加移动力量(4-7点)
    3. 限制最大移动力量为20
    4. 计算目标位置 }
procedure TSoccerBall.Struck (hiter: TCreature);
var
   nx, ny: integer;
begin
   if hiter <> nil then begin
      Dir := hiter.Dir;                           // 设置球的方向为踢球者的方向
      GoPower := GoPower + 4 + Random (4);        // 增加移动力量(4-7点)
      GoPower := _MIN (20, GoPower);              // 限制最大移动力量为20
      // 计算目标位置
      GetNextPosition (PEnvir, CX, CY, Dir, GoPower, nx, ny);
      TargetX := nx;
      TargetY := ny;
   end;

end;


{ TSoccerBall.Run - 主运行循环
  功能: 处理足球的主要逻辑
  实现原理:
    1. 如果有移动力量，检查下一个位置是否可通行
    2. 如果撞墙则反弹(方向反转)
    3. 向目标位置移动
    4. 到达目标后清除移动力量
  注意事项:
    - 足球不能与其他物体重叠 }
procedure TSoccerBall.Run;
var
   i, dis, nx, ny, nnx, nny: integer;
   bohigh: Boolean;
begin
   bohigh := false;           // 足球不能与其他物体重叠
   if GoPower > 0 then begin
      // 检查下一个位置是否可通行
      if GetNextPosition (PEnvir, CX, CY, Dir, 1, nx, ny) then begin
         if not PEnvir.CanWalk (nx, ny, bohigh) then begin
            // 撞墙时反弹(方向反转)
            case Dir of
               0: Dir := 4;   // 上 -> 下
               1: Dir := 7;   // 右上 -> 左下
               2: Dir := 6;   // 右 -> 左
               3: Dir := 5;   // 右下 -> 左上
               4: Dir := 0;   // 下 -> 上
               5: Dir := 3;   // 左下 -> 右上
               6: Dir := 2;   // 左 -> 右
               7: Dir := 1;   // 左上 -> 右下
            end;
            // 重新计算目标位置
            GetNextPosition (PEnvir, CX, CY, Dir, GoPower, nx, ny);
            TargetX := nx;
            TargetY := ny;
         end;
      end;
   end else
      TargetX := -1;          // 无移动力量时清除目标

   // 向目标位置移动
   if TargetX <> -1 then begin
      GotoTargetXY;           // 移动到目标位置
      // 到达目标后清除移动力量
      if (TargetX = CX) and (TargetY = CY) then
         GoPower := 0;
   end;        

   inherited Run;

end;


end.
