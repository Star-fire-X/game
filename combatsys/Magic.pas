{ ============================================================================
  单元名称: Magic
  功能描述: 魔法管理单元
  
  主要功能:
  - 管理所有魔法技能的施放逻辑
  - 处理各种魔法效果（攻击、治疗、辅助等）
  - 计算魔法伤害和效果
  - 管理符纸消耗和技能修炼
  
  魔法类型:
  - 攻击魔法: 火球术、雷电术、爆裂火焰等
  - 治疗魔法: 治愈术、群体治愈术
  - 辅助魔法: 隐身术、魔法盾、神圣战甲术等
  - 召唤魔法: 召唤骷髅、召唤神兽
  - 特殊魔法: 瞬息移动、困魔咒、圣言术等
  
  作者: [原作者]
  创建日期: [创建日期]
============================================================================ }
unit Magic;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Dialogs,
  ScktComp, syncobjs, MudUtil, HUtil32, Grobal2, Envir, EdCode, ObjBase,
  M2Share, Event;


type
   { TMagicManager - 魔法管理器类
     功能: 管理所有魔法技能的施放和效果处理 }
   TMagicManager = class
   private
   public
      constructor Create;
      { 计算魔法威力 }
      function  MPow (pum: PTUserMagic): integer;
      { 野蛮冲撞 - 推开周围生物 (pushlevel: 0..3) }
      function  MagPushArround (user: TCreature; pushlevel: integer): integer;
      { 雷电术/噬血术 - 对目标造成雷电伤害并可能控制怪物 }
      function  MagLightingShock (user, target: TCreature; x, y, shocklevel: integer): Boolean;
      { 圣言术 - 对不死系怪物造成即死效果 }
      function  MagTurnUndead (user, target: TCreature; x, y, mlevel: integer): Boolean;
      { 瞬息移动 - 随机传送到安全区 }
      function  MagLightingSpaceMove (user: TCreature; slevel: integer): Boolean;
      { 困魔咒 - 创建结界困住怪物 }
      function  MagMakeHolyCurtain (user: TCreature; htime, x, y: integer): integer;
      { 火墙 - 创建十字形火焰区域 }
      function  MagMakeFireCross (user: TCreature; dam, htime, x, y: integer): integer;
      { 群体治愈术 - 治疗范围内友方单位 }
      function  MagBigHealing (user: TCreature; pwr, x, y: integer): Boolean;
      { 爆裂火焰/冰咆哮 - 范围爆炸伤害 }
      function  MagBigExplosion (user: TCreature; pwr, x, y, wide: integer): Boolean;
      { 地狱雷光 - 以自身为中心的范围雷电攻击 }
      function  MagElecBlizzard (user: TCreature; pwr: integer): Boolean;
      { 隐身术 - 使自己隐身 }
      function  MagMakePrivateTransparent (user: TCreature; htime: integer): Boolean;
      { 集体隐身术 - 使范围内友方单位隐身 }
      function  MagMakeGroupTransparent (user: TCreature; x, y, htime: integer): Boolean;
      { 判断是否为剑术技能 }
      function  IsSwordSkill (mid: integer): Boolean;
      { 施放魔法主函数 }
      function  SpellNow (user: TCreature; pum: PTUserMagic; xx, yy: integer; target: TCreature): Boolean;
   end;


implementation

uses
   svMain;

{ ========================================================================
  TMagicManager 方法实现
  ======================================================================== }

{ Create
  功能: 构造函数 }
constructor TMagicManager.Create;
begin
   inherited Create;
end;

{ IsSwordSkill
  功能: 判断是否为剑术技能（非按键施放的魔法）
  参数:
    mid - 魔法ID
  返回值: TRUE-是剑术技能, FALSE-不是
  说明: 剑术技能不是通过按键施放的魔法，而是通过攻击触发 }
function  TMagicManager.IsSwordSkill (mid: integer): Boolean;
begin
   Result := FALSE;
   // 剑术技能ID: 3=攻杀剑术, 4=刺杀剑术, 7=半月弯刀, 12=烈火剑法
   // 25=莲月剑法, 26=开天斩, 27=逐日剑法, 34=新增武功
   case mid of
      3, 4, 7, 12, 25, 26, 27, 34: Result := TRUE;
   end;
end;

{ MPow
  功能: 计算魔法基础威力
  参数:
    pum - 用户魔法指针
  返回值: 魔法威力值
  实现原理: 在最小威力和最大威力之间随机取值 }
function  TMagicManager.MPow (pum: PTUserMagic): integer;
begin
   Result := pum.pDef.MinPower + Random(pum.pDef.MaxPower - pum.pDef.MinPower);
end;

{ MagPushArround
  功能: 野蛮冲撞 - 推开周围的生物
  参数:
    user - 施法者
    pushlevel - 推力等级 (0..3)
  返回值: 成功推开的生物数量
  实现原理:
    1. 遍历施法者周围1格内的所有生物
    2. 根据等级差和技能等级计算推开概率
    3. 成功则将目标向外推开 }
function  TMagicManager.MagPushArround (user: TCreature; pushlevel: integer): integer;
var
   i, ndir, levelgap, push: integer;  // 循环变量、方向、等级差、推力
   cret: TCreature;                    // 目标生物
begin
   Result := 0;
   // 遍历可见的生物列表
   for i:=0 to user.VisibleActors.Count-1 do begin
      cret := TCreature (PTVisibleActor(user.VisibleActors[i]).cret);
      // 检查是否在周围1格内
      if (abs(user.CX-cret.CX) <= 1) and (abs(user.CY-cret.CY) <= 1) then begin
         if (not cret.Death) and (cret <> user) then begin
            // 只有等级高于目标且目标非固定模式才能推开
            if (user.Abil.Level > cret.Abil.Level) and (not cret.StickMode) then begin
               levelgap := user.Abil.Level - cret.Abil.Level;
               // 根据技能等级和等级差计算成功率
               if (Random(20) < 6+pushlevel*3+levelgap) then begin
                  if user.IsProperTarget(cret) then begin
                     // 计算推开距离
                     push := 1 + _MAX(0,pushlevel-1) + Random(2);
                     // 计算推开方向
                     ndir := GetNextDirection (user.CX, user.CY, cret.CX, cret.CY);
                     cret.CharPushed (ndir, push);
                     Inc (Result);
                  end;
               end;
            end;
         end;
      end;
   end;
end;
{ MagLightingShock
  功能: 雷电术/噬血术 - 对目标造成雷电伤害并可能控制怪物
  参数:
    user - 施法者
    target - 目标生物
    x, y - 目标坐标
    shocklevel - 技能等级
  返回值: TRUE-成功, FALSE-失败
  实现原理:
    1. 对非玩家目标有效
    2. 可能使目标失去目标、进入狂暴状态或被控制
    3. 对不死系怪物有即死效果
    4. 可以抢夺他人的召唤怪物 }
function  TMagicManager.MagLightingShock (user, target: TCreature; x, y, shocklevel: integer): Boolean;
var
   ran: integer;  // 随机值
begin
   Result := FALSE;
   // 只对非玩家目标有效
   if (target.RaceServer <> RC_USERHUMAN) and (Random(4-shocklevel) = 0) then begin
      target.TargetCret := nil;
      // 提示: 对自己的召唤物使用会使其停止
      if target.Master = user then begin
         target.MakeHolySeize ((10 + shocklevel * 5) * 1000);  // 失去意识，忘记敌人
         Result := TRUE;
         exit;
      end;

      if (Random(2) = 0) then begin
         // 等级检查
         if target.Abil.Level <= user.Abil.Level + 2 then begin
            if Random(3) = 0 then begin
               // 控制成功率计算
               if (10 + target.Abil.Level) < Random(20 + user.Abil.Level + shocklevel * 5) then begin
                  // 检查是否可以被控制
                  if (not target.NoMaster) and
                     (target.LifeAttrib = LA_CREATURE) and
                     (target.Abil.Level < MAXLEVEL-1) and     // 等级小于50
                     (user.SlaveList.Count < (2 + shocklevel))  // 召唤物数量限制
                  then begin
                     // 有生命的怪物，等级小于50，召唤物数量小于5
                     ran := target.WAbil.MaxHP div 100;
                     if ran <= 2 then ran := 2
                     else ran := ran + ran;

                     // 尝试控制目标
                     if (target.Master <> user) and (Random(ran) = 0) then begin
                        target.BreakCrazyMode;
                        if target.Master <> nil then  // 抢夺他人的召唤物
                           target.WAbil.HP := target.WAbil.HP div 10;
                        target.Master := user;  // 设置为自己的召唤物
                        // 设置忠诚时间
                        target.MasterRoyaltyTime := GetTickCount +
                                                    longword(20 + shocklevel * 20 +
                                                             Random(user.Abil.Level * 2))
                                                    * 60 * 1000;
                        target.SlaveMakeLevel := shocklevel;
                        if target.SlaveLifeTime = 0 then target.SlaveLifeTime := GetTickCount;
                        target.BreakHolySeize;
                        // 根据技能等级提升召唤物速度
                        if target.NextWalkTime > 1500-(shocklevel*200)  then target.NextWalkTime := 1500-(shocklevel*200);
                        if target.NextHitTime > 2000-(shocklevel*200) then target.NextHitTime := 2000-(shocklevel*200);
                        target.UserNameChanged;
                        user.SlaveList.Add (target);
                     end else begin
                        // 控制失败，小概率直接击杀
                        if (Random(20) = 0) then begin
                           target.WAbil.HP := 0;
                        end;
                     end;
                  end else begin
                     // 不死系怪物有50%概率直接死亡
                     if (target.LifeAttrib = LA_UNDEAD) and (Random(2) = 0) then begin
                        target.WAbil.HP := 0;
                     end;
                  end;
               end else begin
                  // 非不死系怪物进入狂暴状态
                  if (target.LifeAttrib <> LA_UNDEAD) and (Random(2) = 0) then begin
                     target.MakeCrazyMode (10 + Random(20));
                  end;
               end;
            end else begin
               // 非不死系怪物进入狂暴状态
               if target.LifeAttrib <> LA_UNDEAD then begin
                  target.MakeCrazyMode (10 + Random(20));
               end;
            end;
         end;
      end else begin
         // 使目标失去意识
         target.MakeHolySeize ((10 + shocklevel * 5) * 1000);
      end;
      Result := TRUE;
   end else
      if Random(2) = 0 then
         Result := TRUE;
end;

{ MagTurnUndead
  功能: 圣言术 - 对不死系怪物造成即死效果
  参数:
    user - 施法者
    target - 目标生物
    x, y - 目标坐标
    mlevel - 技能等级
  返回值: TRUE-成功击杀, FALSE-失败
  实现原理:
    1. 只对不死系怪物有效
    2. 根据等级差和技能等级计算即死概率
    3. 即使未击杀也会使目标逃跑 }
function  TMagicManager.MagTurnUndead (user, target: TCreature; x, y, mlevel: integer): Boolean;
var
   lvgap: integer;  // 等级差
begin
   Result := FALSE;
   // 只对不死系怪物有效
   if (not target.NeverDie) and
      (target.LifeAttrib = LA_UNDEAD)
   then begin
      TAnimal(target).Struck (user);
      // 如果目标空闲，使其逃跑
      if target.TargetCret = nil then begin
         TAnimal(target).BoRunAwayMode := TRUE;
         TAnimal(target).RunAwayStart := GetTickCount;
         TAnimal(target).RunAwayTime := 10 * 1000;
      end;
      user.SelectTarget (target);
      // 等级检查
      if (target.Abil.Level < (user.Abil.Level - 1 + Random(4))) and
         (target.Abil.Level < MAXLEVEL-1)   // 等级小于50
      then begin
         lvgap := user.Abil.Level - target.Abil.Level;
         // 计算即死概率
         if Random(100) < (15 + mlevel * 7 + lvgap) then begin
            target.SetLastHiter (user);
            target.WAbil.HP := 0;  // 直接死亡
            Result := TRUE;
         end;
      end;
   end;
end;

{ MagLightingSpaceMove
  功能: 瞬息移动 - 随机传送到安全区
  参数:
    user - 施法者
    slevel - 技能等级
  返回值: TRUE-成功传送, FALSE-失败
  实现原理:
    1. 根据技能等级计算成功率
    2. 传送到玩家的出生点地图
    3. 如果跨地图传送，取消时间回城 }
function  TMagicManager.MagLightingSpaceMove (user: TCreature; slevel: integer): Boolean;
var
   oldenvir: TEnvirnoment;  // 原地图
   hum: TUserHuman;         // 玩家对象
begin
   Result := FALSE;
   // 台湾活动用户不能使用
   if not user.BoTaiwanEventUser then begin
      // 根据技能等级计算成功率
      if Random(11) < 4 + slevel*2 then begin
         user.SendRefMsg (RM_SPACEMOVE_HIDE2, 0, 0, 0, 0, '');
         if user is TUserHuman then begin
            oldenvir := user.PEnvir;
            // 随机传送到出生点地图
            TUserHuman(user).RandomSpaceMove (user.HomeMap, 1);
            // 如果跨地图传送
            if oldenvir <> user.PEnvir then begin
               if user.RaceServer = RC_USERHUMAN then begin
                  hum := TUserHuman (self);
                  hum.BoTimeRecall := FALSE;    // 取消时间回城
               end;
            end;
         end;
         Result := TRUE;
      end;
   end;      
end;

{ MagMakeHolyCurtain
  功能: 困魔咒 - 创建结界困住怪物
  参数:
    user - 施法者
    htime - 持续时间（秒）
    x, y - 目标坐标
  返回值: 被困住的怪物数量
  实现原理:
    1. 检查目标位置是否可行走
    2. 获取范围内的怪物并使其进入结界状态
    3. 在周围8个位置创建结界光效
    4. 如果范围内有无法困住的目标则取消 }
function  TMagicManager.MagMakeHolyCurtain (user: TCreature; htime, x, y: integer): integer;
var
   event: TEvent;           // 事件对象
   i: integer;              // 循环变量
   rlist: TList;            // 生物列表
   cret: TCreature;         // 目标生物
   phs: PTHolySeizeInfo;    // 结界信息
begin
   Result := 0;
   // 检查目标位置是否可行走
   if user.PEnvir.CanWalk (x, y, TRUE) then begin
      rlist := TList.Create;
      phs := nil;
      // 获取范围内的生物
      user.GetMapCreatures (user.PEnvir, x, y, 1, rlist);
      for i:=0 to rlist.Count-1 do begin
         cret := TCreature (rlist[i]);
         // 检查是否可以被困住
         if (cret.RaceServer >= RC_ANIMAL) and
            (cret.Abil.Level < (user.Abil.Level - 1 + Random(4))) and
            (cret.Abil.Level < MAXLEVEL-1) and    // 等级小于50
            (cret.Master = nil)                    // 非召唤物
         then begin
            // 使目标进入结界状态
            cret.MakeHolySeize (htime * 1000);
            if phs = nil then begin
               phs := new (PTHolySeizeInfo);    // 创建结界信息
               FillChar (phs^, sizeof(THolySeizeInfo), #0);
               phs.seizelist := TList.Create;
               phs.OpenTime := GetTickCount;    // 结界创建时间
               phs.SeizeTime := htime * 1000;   // 结界持续时间
            end;
            phs.seizelist.Add (cret);  // 记录被困住的怪物
            Inc (Result);
         end else begin
            Result := 0;  // 如果有无法困住的目标则取消
            break;
         end;
      end;
      rlist.Free;

      // 创建结界成功
      if (Result > 0) and (phs <> nil) then begin
         // 在8个方向创建结界光效
         event := THolyCurtainEvent.Create (user.PEnvir, x-1, y-2, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[0] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x+1, y-2, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[1] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x-2, y-1, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[2] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x+2, y-1, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[3] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x-2, y+1, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[4] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x+2, y+1, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[5] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x-1, y+2, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[6] := event;
         event := THolyCurtainEvent.Create (user.PEnvir, x+1, y+2, ET_HOLYCURTAIN, htime * 1000);
         EventMan.AddEvent (event);  phs.earr[7] := event;

         UserEngine.HolySeizeList.Add (phs);  // 添加到结界列表
      end else begin
         // 创建失败，释放资源
         if phs <> nil then begin
            phs.seizelist.Free;
            Dispose (phs);
         end;
      end;
   end;
end;

{ MagMakeFireCross
  功能: 火墙 - 创建十字形火焰区域
  参数:
    user - 施法者
    dam - 伤害值
    htime - 持续时间（秒）
    x, y - 目标坐标
  返回值: 1-成功
  实现原理: 在目标位置及上下左右4个方向创建火焰事件 }
function  TMagicManager.MagMakeFireCross (user: TCreature; dam, htime, x, y: integer): integer;
var
   event: TEvent;   // 事件对象
   i: integer;      // 循环变量
   rlist: TList;    // 生物列表
   cret: TCreature; // 目标生物
begin
   // 上方
   if user.PEnvir.GetEvent(x, y-1) = nil then begin
      event := TFireBurnEvent.Create (user, x, y-1, ET_FIRE, htime * 1000, dam);
      EventMan.AddEvent (event);
   end;
   // 左方
   if user.PEnvir.GetEvent(x-1, y) = nil then begin
      event := TFireBurnEvent.Create (user, x-1, y, ET_FIRE, htime * 1000, dam);
      EventMan.AddEvent (event);
   end;
   // 中心
   if user.PEnvir.GetEvent(x, y) = nil then begin
      event := TFireBurnEvent.Create (user, x, y, ET_FIRE, htime * 1000, dam);
      EventMan.AddEvent (event);
   end;
   // 右方
   if user.PEnvir.GetEvent(x+1, y) = nil then begin
      event := TFireBurnEvent.Create (user, x+1, y, ET_FIRE, htime * 1000, dam);
      EventMan.AddEvent (event);
   end;
   // 下方
   if user.PEnvir.GetEvent(x, y+1) = nil then begin
      event := TFireBurnEvent.Create (user, x, y+1, ET_FIRE, htime * 1000, dam);
      EventMan.AddEvent (event);
   end;

   Result := 1;
end;

{ MagBigHealing
  功能: 群体治愈术 - 治疗范围内友方单位
  参数:
    user - 施法者
    pwr - 治疗量
    x, y - 目标坐标
  返回值: TRUE-治疗了至少一个目标, FALSE-无目标 }
function  TMagicManager.MagBigHealing (user: TCreature; pwr, x, y: integer): Boolean;
var
   i: integer;       // 循环变量
   rlist: TList;     // 生物列表
   cret: TCreature;  // 目标生物
begin
   Result := FALSE;
   rlist := TList.Create;
   // 获取范围内的生物
   user.GetMapCreatures (user.PEnvir, x, y, 1, rlist);
   for i:=0 to rlist.Count-1 do begin
      cret := TCreature (rlist[i]);
      // 检查是否为友方
      if user.IsProperFriend (cret) then begin
         // 如果HP未满则治疗
         if cret.WAbil.HP < cret.WAbil.MaxHP then begin
            cret.SendDelayMsg (user, RM_MAGHEALING, 0, pwr, 0, 0, '', 800);
            Result := TRUE;
         end;
         // 如果有查看治疗量的能力
         if user.BoAbilSeeHealGauge then begin
            user.SendMsg (cret, RM_INSTANCEHEALGUAGE, 0, 0, 0, 0, '');
         end;
      end;
   end;
   rlist.Free;
end;

{ MagBigExplosion
  功能: 爆裂火焰/冰咆哮 - 范围爆炸伤害
  参数:
    user - 施法者
    pwr - 伤害值
    x, y - 目标坐标
    wide - 范围大小
  返回值: TRUE-命中了至少一个目标, FALSE-无目标 }
function  TMagicManager.MagBigExplosion (user: TCreature; pwr, x, y, wide: integer): Boolean;
var
   i: integer;       // 循环变量
   rlist: TList;     // 生物列表
   cret: TCreature;  // 目标生物
begin
   Result := FALSE;
   rlist := TList.Create;
   // 获取范围内的生物
   user.GetMapCreatures (user.PEnvir, x, y, wide, rlist);
   for i:=0 to rlist.Count-1 do begin
      cret := TCreature (rlist[i]);
      // 检查是否为有效目标
      if user.IsProperTarget (cret) then begin
         user.SelectTarget (cret);
         // 发送魔法伤害消息
         cret.SendMsg (user, RM_MAGSTRUCK, 0, pwr, 0, 0, '');
         Result := TRUE;
      end;
   end;
   rlist.Free;
end;

{ MagElecBlizzard
  功能: 地狱雷光 - 以自身为中心的范围雷电攻击
  参数:
    user - 施法者
    pwr - 伤害值
  返回值: TRUE-命中了至少一个目标, FALSE-无目标
  特殊效果: 对不死系怪物伤害更高 }
function  TMagicManager.MagElecBlizzard (user: TCreature; pwr: integer): Boolean;
var
   i, acpwr: integer;  // 循环变量、实际伤害
   rlist: TList;       // 生物列表
   cret: TCreature;    // 目标生物
begin
   Result := FALSE;
   rlist := TList.Create;
   // 获取以自身为中心半径2格内的生物
   user.GetMapCreatures (user.PEnvir, user.CX, user.CY, 2, rlist);
   for i:=0 to rlist.Count-1 do begin
      cret := TCreature (rlist[i]);
      // 对不死系怪物伤害更高，非不死系只有1/10伤害
      if cret.LifeAttrib <> LA_UNDEAD then
         acpwr := pwr div 10
      else acpwr := pwr;
      if user.IsProperTarget (cret) then begin
         cret.SendMsg (user, RM_MAGSTRUCK, 0, acpwr, 0, 0, '');
         Result := TRUE;
      end;
   end;
   rlist.Free;
end;


{ MagMakePrivateTransparent
  功能: 隐身术 - 使自己隐身
  参数:
    user - 施法者
    htime - 持续时间（秒）
  返回值: TRUE-成功, FALSE-失败（已处于隐身状态）
  实现原理:
    1. 使周围怪物失去对自己的目标
    2. 设置隐身状态
    3. 移动时隐身会解除 }
function  TMagicManager.MagMakePrivateTransparent (user: TCreature; htime: integer): Boolean;
var
   i: integer;       // 循环变量
   rlist: TList;     // 生物列表
   cret: TCreature;  // 目标生物
begin
   Result := FALSE;
   // 如果已经处于隐身状态则退出
   if user.StatusArr[STATE_TRANSPARENT] > 0 then exit;

   rlist := TList.Create;
   // 获取周围9格内的生物
   user.GetMapCreatures (user.PEnvir, user.CX, user.CY, 9, rlist);
   for i:=0 to rlist.Count-1 do begin
      cret := TCreature (rlist[i]);
      // 检查是否为怪物
      if cret.RaceServer >= RC_ANIMAL then begin
         // 如果怪物正在攻击施法者
         if cret.TargetCret = user then begin
            // 近距离的怪物有50%概率不受影响
            if (abs(cret.CX-user.CX) > 1) or (abs(cret.CY-user.CY) > 1) or (Random(2) = 0) then begin
               cret.TargetCret := nil;  // 使怪物失去目标
            end;
         end;
      end;
   end;
   rlist.Free;
   // 设置隐身状态
   user.StatusArr[STATE_TRANSPARENT] := htime;
   user.CharStatus := user.GetCharStatus;
   user.CharStatusChanged;
   user.BoHumHideMode := TRUE;
   user.BoFixedHideMode := TRUE;  // 只能在原地隐身，移动会解除
   Result := TRUE;
end;

{ MagMakeGroupTransparent
  功能: 集体隐身术 - 使范围内友方单位隐身
  参数:
    user - 施法者
    x, y - 目标坐标
    htime - 持续时间（秒）
  返回值: TRUE-至少使一个目标隐身, FALSE-无目标 }
function  TMagicManager.MagMakeGroupTransparent (user: TCreature; x, y, htime: integer): Boolean;
var
   i: integer;       // 循环变量
   rlist: TList;     // 生物列表
   cret: TCreature;  // 目标生物
begin
   Result := FALSE;
   rlist := TList.Create;
   // 获取范围内的生物
   user.GetMapCreatures (user.PEnvir, x, y, 1, rlist);
   for i:=0 to rlist.Count-1 do begin
      cret := TCreature (rlist[i]);
      // 检查是否为友方
      if user.IsProperFriend(cret) then begin
         // 如果目标未处于隐身状态
         if cret.StatusArr[STATE_TRANSPARENT] = 0 then begin
            cret.SendDelayMsg (cret, RM_TRANSPARENT, 0, htime, 0, 0, '', 800);
            Result := TRUE;
         end;
      end;
   end;
   rlist.Free;
end;

{ SpellNow
  功能: 施放魔法主函数 - 处理所有魔法的施放逻辑
  参数:
    user - 施法者
    pum - 用户魔法指针
    xx, yy - 目标坐标
    target - 目标生物（可为nil）
  返回值: TRUE-施法成功, FALSE-施法失败
  实现原理:
    1. 根据魔法ID分发到不同的处理逻辑
    2. 计算魔法威力和效果
    3. 处理符纸消耗
    4. 处理技能修炼 }
function  TMagicManager.SpellNow (user: TCreature; pum: PTUserMagic; xx, yy: integer; target: TCreature): Boolean;

   { GetRPow - 获取随机威力值
     参数: pw - 威力值（高字节为最大值，低字节为最小值）
     返回值: 随机威力值 }
   function GetRPow (pw: word): byte;
   begin
      if Hibyte(pw) > Lobyte(pw) then begin
         Result := Lobyte(pw) + Random(Hibyte(pw)-Lobyte(pw)+1);
      end else
         Result := Lobyte(pw);
   end;

   { GetPower - 计算魔法威力（0级时为1/4威力）
     参数: pw - 基础威力
     返回值: 实际威力值 }
   function GetPower (pw: integer): integer;
   begin
      Result := Round(pw / (pum.pDef.MaxTrainLevel+1) * (pum.Level+1))
                      + (pum.pDef.DefMinPower + Random(pum.pDef.DefMaxPower - pum.pDef.DefMinPower));
   end;

   { GetPower13 - 计算魔法威力（0级时保留1/3威力）
     参数: pw - 基础威力
     返回值: 实际威力值 }
   function GetPower13 (pw: integer): integer;
   var
      p1, p2: Real;
   begin
      p1 := pw / 3;
      p2 := pw - p1;
      Result := Round(p1 + p2 / (pum.pDef.MaxTrainLevel+1) * (pum.Level+1)
                      + (pum.pDef.DefMinPower + Random(pum.pDef.DefMaxPower - pum.pDef.DefMinPower)));
   end;

   { CanUseBujuk - 检查是否可以使用符纸
     参数:
       user - 使用者
       count - 需要的符纸数量
     返回值: 0-不可用, 1-符纸位置可用, 2-左手镯位置可用 }
   function CanUseBujuk (user: TCreature; count: integer): integer;
   var
      pstd: PTStdItem;
   begin
      Result := 0;
      // 检查符纸位置
      if (user.UseItems[U_BUJUK].Index > 0) then begin
         pstd := UserEngine.GetStdItem (user.UseItems[U_BUJUK].Index);
         if pstd <> nil then begin
            if (pstd.StdMode = 25) and (pstd.Shape = 5) then begin  // 符纸
               if Round(user.UseItems[U_BUJUK].Dura / 100) >= (count-1) then begin
                  Result := 1;
               end;
            end;
         end;
      end;
      // 检查左手镯位置
      if (user.UseItems[U_ARMRINGL].Index > 0) and (Result = 0) then begin
         pstd := UserEngine.GetStdItem (user.UseItems[U_ARMRINGL].Index);
         if pstd <> nil then begin
            if (pstd.StdMode = 25) and (pstd.Shape = 5) then begin  // 符纸
               if Round(user.UseItems[U_ARMRINGL].Dura / 100) >= (count-1) then begin
                  Result := 2;
               end;
            end;
         end;
      end;
   end;

   { UseBujuk - 使用符纸后处理
     功能: 检查并删除用完的符纸 }
   procedure UseBujuk (user: TCreature);
   var
      hum: TUserHuman;
      pstd: PTStdItem;
   begin
      // 检查符纸位置是否用完
      if user.UseItems[U_BUJUK].Dura < 100 then begin
         user.UseItems[U_BUJUK].Dura := 0;
         // 删除用完的符纸
         if user.RaceServer = RC_USERHUMAN then begin
            hum := TUserHuman(user);
            hum.SendDelItem (user.UseItems[U_BUJUK]);
         end;
         user.UseItems[U_BUJUK].Index := 0;
      end;
      // 检查左手镯位置是否用完
      if(user.UseItems[U_ARMRINGL].Index > 0) then begin
         pstd := UserEngine.GetStdItem (user.UseItems[U_ARMRINGL].Index);
         if pstd <> nil then begin
            if (pstd.StdMode = 25) then begin  // 符纸
               if user.UseItems[U_ARMRINGL].Dura < 100 then begin
                  user.UseItems[U_ARMRINGL].Dura := 0;
                  // 删除用完的符纸
                  if user.RaceServer = RC_USERHUMAN then begin
                     hum := TUserHuman(user);
                     hum.SendDelItem (user.UseItems[U_ARMRINGL]);
                     hum.SysMsg('콱돨륜綠痰쐴。', 0);
                  end;
                  user.UseItems[U_ARMRINGL].Index := 0;
               end;
            end;
         end;
      end;
   end;

var
   idx, sx, sy, ndir, pwr: integer;  // 索引、坐标、方向、威力
   train, nofire, needfire: Boolean; // 是否修炼、是否不发射、是否需要发射
   bhasitem : integer;               // 是否有符纸
   pstd: PTStdItem;                  // 标准物品指针
   hum: TUserHuman;                  // 玩家对象
begin
   Result := FALSE;
   // 剑术技能不在此处理
   if IsSwordSkill (pum.MagicId) then exit;

   // 先发送魔法准备动作
   user.SendRefMsg (RM_SPELL, pum.pDef.Effect, xx, yy, pum.pDef.MagicId, '');
   // 如果目标已死亡则清空
   if target <> nil then
      if target.Death then begin
         target := nil;
      end;

   train := FALSE;     // 是否可以修炼技能
   nofire := FALSE;    // 是否不发射魔法效果
   needfire := TRUE;   // 是否需要发射魔法效果
   pwr := 0;           // 魔法威力

   // 根据魔法ID分发处理
   case pum.pDef.MagicId of
      1,  // 火球术
      5:  // 大火球
         if user.MagCanHitTarget (user.CX, user.CY, target) then begin
            if user.IsProperTarget (target) then begin
               if (target.AntiMagic <= Random(10)) and (abs(target.CX-xx) <= 1) and (abs(target.CY-yy) <= 1) then begin
                  with user do begin
                     pwr := GetAttackPower (
                                 GetPower (MPow(pum)) + Lobyte(WAbil.MC),
                                 ShortInt(Hibyte(WAbil.MC)-Lobyte(WAbil.MC)) + 1
                              );
                     //pwr := GetPower (MPow(pum)) + (Lobyte(WAbil.MC) + Random(Hibyte(WAbil.MC)-Lobyte(WAbil.MC) + 1));
                     //타겟 맞음, 후에 효과나타남
                     //target.SendDelayMsg (user, RM_MAGSTRUCK, 0, pwr, 0, 0, '', 1200 + _MAX(Abs(CX-xx),Abs(CY-yy)) * 50 );
                  end;
                  //rm-delaymagic에서 selecttarget을 처리한다.
                  user.SendDelayMsg (user, RM_DELAYMAGIC, pwr, MakeLong(xx, yy), 2, integer(target), '', 600);
                  if (target.RaceServer >= RC_ANIMAL) then train := TRUE;
               end else
                  target := nil;
            end else
               target := nil;
         end else
            target := nil;
      37,  // 野蛮冲撞（新增）
      8:   // 抱月刃
         begin
            // 推开周围的生物
            if MagPushArround (user, pum.Level) > 0 then
               train := TRUE;
         end;
      9:   // 地狱火
         begin
            ndir := GetNextDirection (user.CX, user.CY, xx, yy);
            if GetNextPosition (user.PEnvir, user.CX, user.CY, ndir, 1, sx, sy) then begin
               GetNextPosition (user.PEnvir, user.CX, user.CY, ndir, 5, xx, yy);
               with user do begin
                  pwr := GetAttackPower (
                           GetPower (MPow(pum)) + Lobyte(WAbil.MC),
                           ShortInt(Hibyte(WAbil.MC)-Lobyte(WAbil.MC)) + 1
                         );
               end;
               if user.MagPassThroughMagic (sx, sy, xx, yy, ndir, pwr, FALSE) > 0 then
                  train := TRUE;
            end;
         end;
      10:  // 雷电术
         begin
            ndir := GetNextDirection (user.CX, user.CY, xx, yy);
            if GetNextPosition (user.PEnvir, user.CX, user.CY, ndir, 1, sx, sy) then begin
               GetNextPosition (user.PEnvir, user.CX, user.CY, ndir, 8, xx, yy);
               with user do begin
                  pwr := GetAttackPower (
                              GetPower (MPow(pum)) + Lobyte(WAbil.MC),
                              ShortInt(Hibyte(WAbil.MC)-Lobyte(WAbil.MC)) + 1
                           );
               end;
               if user.MagPassThroughMagic (sx, sy, xx, yy, ndir, pwr, TRUE) > 0 then
                  train := TRUE;
            end;
         end;
      35,  // 嘘血术（新增）
      11:  // 灵魂火符
         if user.IsProperTarget (target) then begin
            if target.AntiMagic <= Random(10) then begin
               with user do begin
                  pwr := GetAttackPower (
                              GetPower (MPow(pum)) + Lobyte(WAbil.MC),
{ ... }
               if (target.RaceServer >= RC_ANIMAL) then train := TRUE;
            end else
               target := nil;
         end else
            target := nil;
      20:  // 雷电术/嘘血术
         if user.IsProperTarget (target) then begin
            if MagLightingShock (user, target, xx, yy, pum.Level) then
               train := TRUE;
         end;
      32:  // 圣言术
         if user.IsProperTarget (target) then begin
            if MagTurnUndead (user, target, xx, yy, pum.Level) then
               train := TRUE;
         end;
      21:  // 瞬息移动
         begin
            user.SendRefMsg (RM_MAGICFIRE, 0, MakeWord(pum.pDef.EffectType, pum.pDef.Effect), MakeLong(xx, yy), integer(target), '');
            needfire := FALSE;
            if MagLightingSpaceMove (user, pum.Level) then
               train := TRUE;
{ ... }
                                 xx,
                                 yy) > 0 then begin
               train := TRUE;
            end;
         end;
      23:  // 爆裂火焰
         begin
            if MagBigExplosion (user,
                                user.GetAttackPower (GetPower (MPow(pum)) + Lobyte(user.WAbil.MC),
                                                 ShortInt(Hibyte(user.WAbil.MC)-Lobyte(user.WAbil.MC)) + 1),
                                xx,
{ ... }
                                yy,
                                1) then begin
               train := TRUE;
            end;
         end;
      24:  // 地狱雷光
         begin
            if MagElecBlizzard (user,
                                user.GetAttackPower (GetPower (MPow(pum)) + Lobyte(user.WAbil.MC),
                                                 ShortInt(Hibyte(user.WAbil.MC)-Lobyte(user.WAbil.MC)) + 1)
                                ) then begin
               train := TRUE;
            end;
         end;
      31:  // 魔法盾
         begin
            if user.MagBubbleDefenceUp (pum.Level, GetPower (15 + GetRPow(user.WAbil.MC))) then
               train := TRUE;
         end;

      2:   // 治愈术
         begin
            if target = nil then begin
               target := user;
               xx := user.CX;
               yy := user.CY;
{ ... }
            end;
            if user.IsProperFriend (target) then begin
               with user do begin
                  pwr := GetAttackPower (
                              GetPower (MPow(pum)) + Lobyte(WAbil.SC)*2,
                              ShortInt(Hibyte(WAbil.SC)-Lobyte(WAbil.SC))*2 + 1
                           );
               end;
               if target.WAbil.HP < target.WAbil.MaxHP then begin
                  target.SendDelayMsg (user, RM_MAGHEALING, 0, pwr, 0, 0, '', 800);
                  train := TRUE;
               end;
               if user.BoAbilSeeHealGauge then begin
                  user.SendMsg (target, RM_INSTANCEHEALGUAGE, 0, 0, 0, 0, '');
               end;
            end;
         end;
      29: //횐竟撈悼減
         begin
            with user do begin
               pwr := GetAttackPower (
                           GetPower (MPow(pum)) + Lobyte(WAbil.SC)*2,
                           ShortInt(Hibyte(WAbil.SC)-Lobyte(WAbil.SC))*2 + 1
                        );
            end;
            if MagBigHealing (user, pwr, xx, yy) then
               train := TRUE;
         end;

      6:   // 施毒术
         begin
            nofire := TRUE;
            bhasitem := 0;
            pstd := nil;
            if user.IsProperTarget (target) then begin
               // 施毒术需要毒粉袋
               // 2003/03/15 COPARK 아이템 인벤토리 확장
               if (user.UseItems[U_BUJUK].Index > 0) then begin        // U_ARMRINGL->U_BUJUK
                  pstd := UserEngine.GetStdItem (user.UseItems[U_BUJUK].Index);
                  if pstd <> nil then begin
                     if (pstd.StdMode = 25) and (pstd.Shape <= 2) then begin  //25:독주머니
                        if user.UseItems[U_BUJUK].Dura >= 100 then begin
                           user.UseItems[U_BUJUK].Dura := user.UseItems[U_BUJUK].Dura - 100;
                           //내구성 변경은 알림
                           user.SendMsg (user, RM_DURACHANGE, U_BUJUK, user.UseItems[U_BUJUK].Dura, user.UseItems[U_BUJUK].DuraMax, 0, '');
                           bhasItem := 1;
                        end;
                     end;
                  end;
               end;
               if (user.UseItems[U_ARMRINGL].Index > 0) and (bhasitem = 0) then begin
                  pstd := UserEngine.GetStdItem (user.UseItems[U_ARMRINGL].Index);
                  if pstd <> nil then begin
                     if (pstd.StdMode = 25) and (pstd.Shape <= 2) then begin  //25:독주머니
                        if user.UseItems[U_ARMRINGL].Dura >= 100 then begin
                           user.UseItems[U_ARMRINGL].Dura := user.UseItems[U_ARMRINGL].Dura - 100;
                           //내구성 변경은 알림
                           user.SendMsg (user, RM_DURACHANGE, U_ARMRINGL, user.UseItems[U_ARMRINGL].Dura, user.UseItems[U_ARMRINGL].DuraMax, 0, '');
                           bhasItem := 2;
                        end;
                     end;
                  end;
               end;

               if pstd <> nil then begin

               if bhasItem > 0 then begin
                  //스킬정도에 따라 성공여부가 결정
                  if 6 >= Random(7 + target.AntiPoison) then begin
                     case pstd.Shape of
                        1: //회색독가루: 중독
                           begin
                              //pwr = 중독시간  60초 + 알파
                              pwr := GetPower13 (30) + 2 * GetRPow(user.WAbil.SC);
                              target.SendDelayMsg (user, RM_MAKEPOISON, POISON_DECHEALTH{wparam}, pwr, integer(user), pum.Level, '', 1000);
                           end;
                        2: //황색독가루: 방아력감소
                           begin
                              //pwr = 중독시간 40초 + 알파
                              pwr := GetPower13 (40) + 2 * GetRPow(user.WAbil.SC); //(Lobyte(user.WAbil.SC) + Random(ShortInt(Hibyte(user.WAbil.SC)-Lobyte(user.WAbil.SC)) + 1));
                              target.SendDelayMsg (user, RM_MAKEPOISON, POISON_DAMAGEARMOR{wparam}, pwr, integer(user), pum.Level, '', 1000);
                           end;
                     end;

                     //사람,몬스터에게 걸었을때 수련된다.
                     if (target.RaceServer = RC_USERHUMAN) or (target.RaceServer >= RC_ANIMAL) then
                        train := TRUE;
                  end;
                  user.SelectTarget (target);
                  nofire := FALSE;
               end;
               
               end;
               //다 쓴 약은 사라진다.
               if bhasitem = 1 then begin
                  if user.UseItems[U_BUJUK].Dura < 100 then begin
                     user.UseItems[U_BUJUK].Dura := 0;
                     //다 쓴약은 사라진다.
                     if user.RaceServer = RC_USERHUMAN then begin
                        hum := TUserHuman(user);
                        hum.SendDelItem (user.UseItems[U_BUJUK]); //클라이언트에 없어진거 보냄
                        hum.SysMsg('콱돨뗀綠痰쐴。', 0);   //뗀浪뢴痰供瓊刻(2012/04/18)
                     end;
                     user.UseItems[U_BUJUK].Index := 0;
                  end;
               end;
               if(user.UseItems[U_ARMRINGL].Index > 0) and (bhasitem = 2) then begin
                  pstd := UserEngine.GetStdItem (user.UseItems[U_ARMRINGL].Index);
                  if pstd <> nil then begin
                     if (pstd.StdMode = 25) then begin  //25:독주머니
                        if user.UseItems[U_ARMRINGL].Dura < 100 then begin
                           user.UseItems[U_ARMRINGL].Dura := 0;
                           //다 쓴약은 사라진다.
                           if user.RaceServer = RC_USERHUMAN then begin
                              hum := TUserHuman(user);
                              hum.SendDelItem (user.UseItems[U_ARMRINGL]); //클라이언트에 없어진거 보냄
                              hum.SysMsg('콱돨뗀綠痰쐴。', 0);   //뗀浪뢴痰供瓊刻(2012/04/18)
                           end;
                           user.UseItems[U_ARMRINGL].Index := 0;
                        end;
                     end;
                  end;
               end;
            end;
         end;
      //2003/03/15 신규무공 추가
      36, //轟섐廬폭
      13, //쥣산삽륜
      14, //聃쥣뜀
      15, //�加濫솖減
      16, //위침麓
      17, //梁뻥太胎
      18, //茶�減
      19: //섞竟茶�減
         begin
            nofire := TRUE;
            bhasItem := CanUseBujuk (user, 1);
            if bhasItem > 0 then begin
               if bhasitem = 1 then begin
                  // 2003/03/15 COPARK 아이템 인벤토리 확장
                  if user.UseItems[U_BUJUK].Dura >= 100 then      // U_ARMRINGL->U_BUJUK
                     user.UseItems[U_BUJUK].Dura := user.UseItems[U_BUJUK].Dura - 100
                  else user.UseItems[U_BUJUK].Dura := 0;
                  user.SendMsg (user, RM_DURACHANGE, U_BUJUK, user.UseItems[U_BUJUK].Dura, user.UseItems[U_BUJUK].DuraMax, 0, '');
               end;
               if bhasitem = 2 then begin
                  if user.UseItems[U_ARMRINGL].Dura >= 100 then
                     user.UseItems[U_ARMRINGL].Dura := user.UseItems[U_ARMRINGL].Dura - 100
                  else user.UseItems[U_ARMRINGL].Dura := 0;
                  user.SendMsg (user, RM_DURACHANGE, U_ARMRINGL, user.UseItems[U_ARMRINGL].Dura, user.UseItems[U_ARMRINGL].DuraMax, 0, '');
               end;

               case pum.pDef.MagicId of
                  13:  //쥣산삽륜
                     if user.MagCanHitTarget (user.CX, user.CY, target) then begin
                        if user.IsProperTarget(target) then begin
                           if (target.AntiMagic <= Random(10)) and (abs(target.CX-xx) <= 1) and (abs(target.CY-yy) <= 1) then begin
                              with user do begin //파워
                                 pwr := GetAttackPower (
                                             GetPower (MPow(pum)) + Lobyte(WAbil.SC),
                                             ShortInt(Hibyte(WAbil.SC)-Lobyte(WAbil.SC)) + 1
                                          );
                                 //타겟 맞음, 후에 효과나타남
                                 //target.SendDelayMsg (user, RM_MAGSTRUCK, 0, pwr, 0, 0, '', 1200 + _MAX(Abs(CX-xx),Abs(CY-yy)) * 50 );
                              end;
                              //user.SelectTarget (target);
                              user.SendDelayMsg (user, RM_DELAYMAGIC, pwr, MakeLong(xx, yy), 2, integer(target), '', 1200);
                              if (target.RaceServer >= RC_ANIMAL) then train := TRUE;
                           end;
                        end;
                     end else
                        target := nil;
                  14: //聃쥣뜀
                     begin
                        pwr := user.GetAttackPower (
                                    GetPower13 (60) + 5 * Lobyte(user.WAbil.SC),
                                    5 * (ShortInt(Hibyte(user.WAbil.SC)-Lobyte(user.WAbil.SC)) + 1)
                                 );
                        if user.MagMakeDefenceArea (xx, yy, 3, pwr{초}, TRUE) > 0 then
                           train := TRUE;
                     end;
                  //2003/03/15 신규무공 추가
                  36: //轟섐廬폭
                     begin
                        pwr := user.GetAttackPower (
                                    GetPower13 (60) + 5 * Lobyte(user.WAbil.SC),
                                    5 * (ShortInt(Hibyte(user.WAbil.SC)-Lobyte(user.WAbil.SC)) + 1)
                                 );
                        if user.MagDcUp (pwr{초}, TRUE) then
                           train := TRUE;
                     end;
                  15: //�加濫솖減
                     begin
                        pwr := user.GetAttackPower (
                                    GetPower13 (60) + 5 * Lobyte(user.WAbil.SC),
                                    5 * (ShortInt(Hibyte(user.WAbil.SC)-Lobyte(user.WAbil.SC)) + 1)
                                 );
                        if user.MagMakeDefenceArea (xx, yy, 3, pwr{초}, FALSE) > 0 then
                           train := TRUE;
                     end;
                  16:  // 困魔咒
                     begin
                        if MagMakeHolyCurtain (user,
                                          GetPower13 (40) + 3 * GetRPow(user.WAbil.SC), //Lobyte(user.WAbil.SC),
                                          xx, yy) > 0 then begin
                           train := TRUE;
                        end;
                     end;
                  17: //梁뻥太胎
                     begin
                        if not TUserHuman(user).IsReservedMakingSlave then
                           if user.MakeSlave (__WhiteSkeleton, pum.Level, 1, 10 * 24 * 60 * 60) <> nil then
                              train := TRUE;
                     end;
                  18:  // 隐身术
                     begin
                        if MagMakePrivateTransparent (user, GetPower13 (30) + 3 * GetRPow(user.WAbil.SC)) then begin
                           train := TRUE;
                        end;
                     end;
                  19: //섞竟茶�減
                     begin
                        if MagMakeGroupTransparent (user, xx, yy, GetPower13 (30) + 3 * GetRPow(user.WAbil.SC)) then begin
                           train := TRUE;
                        end;
                     end;
               end;

               nofire := FALSE;
               UseBujuk (user);
            end;
         end;
      30: //梁뻥�艱
         begin
            nofire := TRUE;
            bhasItem := CanUseBujuk (user, 5);
            if bhasItem > 0 then begin
               if bhasitem = 1 then begin
                  // 2003/03/15 COPARK 아이템 인벤토리 확장
                  if user.UseItems[U_BUJUK].Dura >= 500 then      // U_ARMRINGL->U_BUJUK
                     user.UseItems[U_BUJUK].Dura := user.UseItems[U_BUJUK].Dura - 500
                  else user.UseItems[U_BUJUK].Dura := 0;
                  //내구성 변경은 알림
                  user.SendMsg (user, RM_DURACHANGE, U_BUJUK, user.UseItems[U_BUJUK].Dura, user.UseItems[U_BUJUK].DuraMax, 0, '');
               end;
               if bhasitem = 2 then begin
                  if user.UseItems[U_ARMRINGL].Dura >= 500 then
                     user.UseItems[U_ARMRINGL].Dura := user.UseItems[U_ARMRINGL].Dura - 500
                  else user.UseItems[U_ARMRINGL].Dura := 0;
                  user.SendMsg (user, RM_DURACHANGE, U_ARMRINGL, user.UseItems[U_ARMRINGL].Dura, user.UseItems[U_ARMRINGL].DuraMax, 0, '');
               end;
               case pum.pDef.MagicId of
                  30: //梁뻥�艱
                     begin
                        if not TUserHuman(user).IsReservedMakingSlave then
                           if user.MakeSlave (__ShinSu, pum.Level, 1, 10 * 24 * 60 * 60) <> nil then
                              train := TRUE;
                     end;
               end;
               nofire := FALSE;
            end;
         end;

      28:  //懃쥣폘刻
         begin
            if target <> nil then begin
               if not target.BoOpenHealth then begin
                  if Random(6) <= 3+pum.Level then begin
                     target.OpenHealthStart := GetTickCount;
                     target.OpenHealthTime := GetPower13 (30 + GetRPow(user.WAbil.SC) * 2) * 1000;
                     target.SendDelayMsg (target, RM_DOOPENHEALTH, 0, 0, 0, 0, '', 1500);
                     train := TRUE;
                  end;
               end;
            end;
         end;
   end;

   // 发射魔法效果
   if not nofire then begin
      with user do begin
         // 发送魔法火焰效果
         if needfire then
            SendRefMsg (RM_MAGICFIRE, 0, MakeWord(pum.pDef.EffectType, pum.pDef.Effect), MakeLong(xx, yy), integer(target), '');
         // 处理技能修炼
         if (pum.Level < 3) and train then
            if Abil.Level >= pum.pDef.NeedLevel[pum.Level] then begin
               // 达到修炼等级要求
               user.TrainSkill (pum, 1 + Random(3));
               // 检查是否升级
               if not CheckMagicLevelup (pum) then
                  SendDelayMsg (user, RM_MAGIC_LVEXP, 0, pum.pDef.MagicId, pum.Level, pum.CurTrain, '', 1000);
            end;
      end;
      Result := TRUE;
   end;
end;

end.
