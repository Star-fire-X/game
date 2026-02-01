{ ============================================================================
  单元名称: ObjMon
  功能描述: 怪物对象单元
  
  主要功能:
  - 定义各种怪物类型的基类和派生类
  - 实现怪物的AI逻辑、攻击、移动等行为
  - 包含普通怪物、远程怪物、BOSS怪物等
  
  怪物类型:
  - TMonster: 怪物基类
  - TChickenDeer: 鸡鹿(逃跑型)
  - TATMonster: 攻击型怪物
  - TSpitSpider: 吐液蜘蛛(远程攻击)
  - TGasAttackMonster: 毒气攻击怪物
  - TCowKingMonster: 牛魔王(BOSS)
  - TScultureKingMonster: 祖玛王(BOSS)
  - TSkeletonKingMonster: 骷髅王(BOSS)
============================================================================ }
unit ObjMon;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Dialogs,
  ScktComp, syncobjs, MudUtil, HUtil32, Grobal2, Envir, EdCode, ObjBase,
  Event;


type
   { TMonster类 - 怪物基类
     功能: 所有怪物的基类，提供基本的AI和攻击逻辑
     父类: TAnimal }
   TMonster = class (TAnimal)
   private
      thinktime: longword;
   protected
      RunDone: Boolean;
      DupMode: Boolean;
      function  AttackTarget: Boolean; dynamic;
   public
      constructor Create;
      destructor Destroy; override;
      function  MakeClone (mname: string; src: TCreature): TCreature;
      procedure RunMsg (msg: TMessageInfo); override;
      procedure Run; override;
      function  Think: Boolean;
   end;

   { TChickenDeer类 - 鸡鹿
     功能: 逃跑型怪物，遇到敌人会逃跑 }
   TChickenDeer = class (TMonster)
   public
      constructor Create;
      procedure Run; override;
   end;


   { TATMonster类 - 攻击型怪物
     功能: 主动攻击型怪物基类 }
   TATMonster = class (TMonster)
   private
   protected
   public
      constructor Create;
      destructor Destroy; override;
      procedure Run; override;
   end;

   { TSlowATMonster类 - 慢速攻击怪物
     功能: 攻击速度较慢的怪物 }
   TSlowATMonster = class (TATMonster)
   public
      constructor Create;
   end;

   { TScorpion类 - 蝎子
     功能: 可屠宰获得蝎子尾巴 }
   TScorpion = class (TATMonster)
   private
   public
      constructor Create;
   end;

   { TSpitSpider类 - 吐液蜘蛛
     功能: 远程吐液攻击，可使目标中毒 }
   TSpitSpider = class (TATMonster)
   private
   public
      BoUsePoison: Boolean;
      constructor Create;
      procedure  SpitAttack (dir: byte);
      function  AttackTarget: Boolean; override;
   end;

   { THighRiskSpider类 - 巨型蜘蛛
     功能: 巨型蜘蛛，不使用毒素 }
   THighRiskSpider = class (TSpitSpider)
   public
      constructor Create;
   end;

   { TBigPoisionSpider类 - 巨型毒蜘蛛
     功能: 巨型毒蜘蛛，使用毒素攻击 }
   TBigPoisionSpider = class (TSpitSpider)
   public
      constructor Create;
   end;

   { TGasAttackMonster类 - 毒气攻击怪物
     功能: 可使目标麻痹或中毒 }
   TGasAttackMonster = class (TATMonster)
   private
   public
      constructor Create;
      function  GasAttack (dir: byte): TCreature; dynamic;
      function  AttackTarget: Boolean; override;
   end;

   { TCowMonster类 - 牛魔
     功能: 普通牛魔怪物 }
   TCowMonster = class (TATMonster)
   public
      constructor Create;
   end;

   { TMagCowMonster类 - 魔法牛魔
     功能: 可以释放魔法的牛魔 }
   TMagCowMonster = class (TATMonster)
   private
   public
      constructor Create;
      procedure MagicAttack (dir: byte);
      function  AttackTarget: Boolean; override;
   end;

   { TCowKingMonster类 - 牛魔王
     功能: BOSS怪物，可瞁移、暴走模式 }
   TCowKingMonster = class (TAtMonster)
   private
      JumpTime: longword;  //순간이동을 한다.
      CrazyReadyMode: Boolean;
      CrazyKingMode: Boolean;
      CrazyCount: integer;
      crazyready: longword;
      crazytime: longword;
      oldhittime: integer;
      oldwalktime: integer;
   public
      constructor Create;
      procedure Attack (target: TCreature; dir: byte); override;
      procedure Initialize; override;
      procedure Run; override;
   end;

   { TLightingZombi类 - 闪电僵尸
     功能: 远程闪电攻击，不逃跑 }
   TLightingZombi = class (TMonster)
   private
   public
      constructor Create;
      procedure LightingAttack (dir: integer);
      procedure Run; override;
   end;


   { TDigOutZombi类 - 挖地僵尸
     功能: 从地下钻出的僵尸 }
   TDigOutZombi = class (TMonster)
   protected
      procedure ComeOut;
   public
      constructor Create;
      procedure Run; override;
   end;

   { TZilKinZombi类 - 复活僵尸
     功能: 死后可以复活的僵尸 }
   TZilKinZombi = class (TATMonster)
   private
      deathstart: longword;
      LifeCount: integer; //남은 재생
      RelifeTime: longword;
   public
      constructor Create;
      procedure Run; override;
      procedure Die; override;
   end;

   { TWhiteSkeleton类 - 白骨骷髅
     功能: 召唤兽，从地下钻出 }
   TWhiteSkeleton = class (TATMonster)
   private
      bofirst: Boolean;
   public
      constructor Create;
      procedure RecalcAbilitys; override;
      procedure ResetSkeleton;
      procedure Run; override;
   end;

   { TScultureMonster类 - 石像怪物
     功能: 初始为石像状态，接近后解除 }
   TScultureMonster = class (TMonster)
   private
   public
      constructor Create;
      procedure MeltStone;
      procedure MeltStoneAll;
      procedure Run; override;
   end;

   { TScultureKingMonster类 - 祖玛王
     功能: BOSS怪物，可召唤小怪 }
   TScultureKingMonster = class (TMonster)
   private
      DangerLevel: integer;
      childlist: TList;  //만들어 낸 부하의 리스트
   public
      BoCallFollower: Boolean;
      constructor Create;
      destructor Destroy; override;
      procedure CallFollower; dynamic;
      procedure MeltStone;
      procedure Attack (target: TCreature; dir: byte); override;
      procedure Run; override;
   end;

   { TGasMothMonster类 - 毒气飞蛾
     功能: 可以看到隐身玩家，解除隐身 }
   TGasMothMonster = class (TGasAttackMonster)
   public
      constructor Create;
      procedure Run; override;
      function  GasAttack (dir: byte): TCreature; override;
   end;

   { TGasDungMonster类 - 毒气怪物
     功能: 毒气攻击可使目标麻痹 }
   TGasDungMonster = class (TGasAttackMonster)
   public
      constructor Create;
   end;

   { TElfMonster类 - 神兽(变身前)
     功能: 召唤兽，遇敌会变身 }
   TElfMonster = class (TMonster)
   private
      bofirst: Boolean;
   public
      constructor Create;
      procedure RecalcAbilitys; override;
      procedure ResetElfMon;
      procedure AppearNow;
      procedure Run; override;
   end;

   { TElfWarriorMonster类 - 神兽(变身后)
     功能: 变身后的神兽，可攻击 }
   TElfWarriorMonster = class (TSpitSpider)
   private
      bofirst: Boolean;
      changefacetime: longword;
   public
      constructor Create;
      procedure RecalcAbilitys; override;
      procedure ResetElfMon;
      procedure AppearNow;
      procedure Run; override;
   end;

   { TCriticalMonster类 - 暴击怪物
     功能: 可以造成暴击伤害 }
   TCriticalMonster = class (TATMonster)
   public
      criticalpoint: integer;
      constructor Create;
      procedure Attack (target: TCreature; dir: byte); override;
   end;

   { TDoubleCriticalMonster类 - 双格暴击怪物
     功能: 可以造成双格暴击伤害 }
   TDoubleCriticalMonster = class (TATMonster)
   public
      criticalpoint: integer;
      constructor Create;
      procedure DoubleCriticalAttack (dam: integer; dir: byte);
      procedure Attack (target: TCreature; dir: byte); override;
   end;

   { TSkeletonKingMonster类 - 骷髅王
     功能: BOSS怪物，远程直接攻击，可召唤小怪 }
   TSkeletonKingMonster = class (TScultureKingMonster)
   public
      RunDone: Boolean;
      ChainShot: integer;
      ChainShotCount: integer;
      constructor Create;
      procedure CallFollower; override;
      procedure Attack (target: TCreature; dir: byte); override;
      procedure Run; override;
      procedure RangeAttack (targ: TCreature); dynamic;
      function  AttackTarget: Boolean; override;
   end;
   { TSkeletonSoldier类 - 骷髅士兵
     功能: 远程直接攻击 }
   TSkeletonSoldier = class (TATMonster)
   private
   public
      constructor Create;
      procedure RangeAttack (dir: byte);
      function  AttackTarget: Boolean; override;
   end;

   { TDeadCowKingMonster类 - 死亡牛魔王
     功能: BOSS怪物，近远程攻击，溩射伤害 }
   TDeadCowKingMonster = class (TSkeletonKingMonster)
   public
      constructor Create;
      procedure Attack (target: TCreature; dir: byte); override;
      procedure RangeAttack (targ: TCreature); override;
      function  AttackTarget: Boolean; override;
   end;
   { TBanyaGuardMonster类 - 般若护卫
     功能: 近远程魔法攻击 }
   TBanyaGuardMonster = class (TSkeletonKingMonster)
   public
      constructor Create;
      procedure Attack (target: TCreature; dir: byte); override;
      procedure RangeAttack (targ: TCreature); override;
      function  AttackTarget: Boolean; override;
   end;

implementation

uses
   svMain, M2Share;


{ Create - 构造函数
  功能: 初始化怪物对象 }
constructor TMonster.Create;
begin
   inherited Create;
   DupMode := FALSE;
   RunDone := FALSE;
   thinktime := GetTickCount;
   ViewRange := 5;
   RunNextTick := 250;
   SearchRate := 3000 + longword(Random(2000));
   SearchTime := GetTickCount;
   RaceServer := RC_MONSTER;
end;

{ Destroy - 析构函数
  功能: 释放怪物对象资源 }
destructor TMonster.Destroy;
begin
   inherited Destroy;
end;

{ MakeClone - 创建克隆
  功能: 创建怪物的克隆体
  参数:
    mname - 怪物名称
    src - 源怪物
  返回值: 克隆体对象 }
function  TMonster.MakeClone (mname: string; src: TCreature): TCreature;
var
   mon: TCreature;
begin
   Result := nil;
   mon := UserEngine.AddCreatureSysop (src.PEnvir.MapName, src.CX, src.CY, mname);
   if mon <> nil then begin
      mon.Master := src.Master;
      mon.MasterRoyaltyTime := src.MasterRoyaltyTime;
      mon.SlaveMakeLevel := src.SlaveMakeLevel;
      mon.SlaveExpLevel := src.SlaveExpLevel;
      mon.RecalcAbilitys; //ApplySlaveLevelAbilitys;
      mon.ChangeNameColor;
      if src.Master <> nil then begin
         src.Master.SlaveList.Add (mon);
      end;

      //능력치, 상태 복사
      mon.WAbil := src.WAbil;
      Move (src.StatusArr, mon.StatusArr, sizeof(word)*12);
      mon.TargetCret := src.TargetCret;
      mon.TargetFocusTime := src.TargetFocusTime;
      mon.LastHiter := src.LastHiter;
      mon.LastHitTime := src.LastHitTime;
      mon.Dir := src.Dir;

      Result := mon;
   end;
end;

{ RunMsg - 消息处理
  功能: 处理怪物接收到的消息 }
procedure TMonster.RunMsg (msg: TMessageInfo);
begin
   //case msg.Ident of
   //   RM_DELAYATTACK:
    //     begin
    //        attack (TCreature(msg.lparam1), msg.wparam);
    //     end;
    //  else 
   inherited RunMsg (msg);
   //end;
end;

{ Think - 思考处理
  功能: 怪物AI思考逻辑，处理位置重叠等情况
  返回值: 是否执行了移动 }
function  TMonster.Think: Boolean;
var
   oldx, oldy: integer;
begin
   Result := FALSE;
   if GetTickCount - ThinkTime > 3000 then begin
      ThinkTime := GetTickCount;
      if PEnvir.GetDupCount(CX, CY) >= 2 then begin
         DupMode := TRUE;
      end;
      if not IsProperTarget(TargetCret) then
         TargetCret := nil;
   end;
   if DupMode then begin //자리가 중복된 경우 자리를 피한다.
      oldx := self.CX;
      oldy := self.CY;
      WalkTo (Random(8), FALSE);
      if (oldx <> self.CX) or (oldy <> self.CY) then begin
         DupMode := FALSE;
         Result := TRUE;
      end;
   end;

end;

{ AttackTarget - 攻击目标
  功能: 执行对目标的攻击
  返回值: 是否成功攻击 }
function  TMonster.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if TargetInAttackRange (TargetCret, targdir) then begin
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;
            TargetFocusTime := GetTickCount;
            Attack (TargetCret, targdir);
            BreakHolySeize;
         end;
         Result := TRUE;
      end else begin
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)
         else
            LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
      end;
   end;
end;

{ Run - 运行处理
  功能: 怪物的主要运行逻辑，包括移动、攻击、跟随主人等 }
procedure TMonster.Run;
var
   rx, ry, bx, by: integer;
begin
   if (not BoGhost) and (not Death) and (not HideMode) and (not BoStoneMode) and (StatusArr[POISON_STONE] = 0) then begin
      if Think then begin //겹치지 않게 함
         inherited Run;
         exit;
      end;
      if BoWalkWaitMode then begin
         if GetTickCount - WalkWaitCurTime > WalkWaitTime then
            BoWalkWaitMode := FALSE;
      end;

      if not BoWalkWaitMode and (GetCurrentTime - WalkTime > NextWalkTime) then begin
         WalkTime := GetCurrentTime;
         Inc (WalkCurStep);
         if WalkCurStep > WalkStep then begin
            WalkCurStep := 0;
            BoWalkWaitMode := TRUE;
            WalkWaitCurTime := GetTickCount;
         end;

         if not BoRunAwayMode then begin
            if not NoAttackMode then begin
               if TargetCret <> nil then begin
                  if AttackTarget then begin
                     inherited Run;
                     exit;
                  end;
               end else begin
                  TargetX := -1;
                  if BoHasMission then begin
                     TargetX := Mission_X;
                     TargetY := Mission_Y;
                  end;
               end;
            end;
            if (Master <> nil) then begin
               if (TargetCret = nil) then begin //주인이 있으면 주인을 따라간다.
                  GetBackPosition (Master, bx, by);  //주인의 뒤로 감
                  if (abs(TargetX-bx) > 1) or (abs(TargetY-bx) > 1) then begin
                     TargetX := bx;
                     TargetY := by;
                     if (abs(CX-bx) <= 2) and (abs(CY-by) <= 2) then begin
                        if PEnvir.GetCreature (bx, by, TRUE) <> nil then begin
                           TargetX := CX;  //더 이상 움직이지 않는다.
                           TargetY := CY;
                        end;
                     end;
                  end;
               end;
               //주인과 너무 떨어져 있으면...
               if (not Master.BoSlaveRelax) and
                  ((PEnvir <> Master.PEnvir) or
                   (abs(CX-Master.CX) > 20) or
                   (abs(CY-Master.CY) > 20)
                  )
               then begin
                  SpaceMove (Master.PEnvir.MapName, TargetX, TargetY, 1);
               end;
            end;
         end else begin
            //도망가는 모드이면 TargetX, TargetY로 도망감...
            if RunAwayTime > 0 then begin  //시간 제한이 있음
               if GetTickCount - RunAwayStart > longword(RunAwayTime) then begin
                  BoRunAwayMode := FALSE;
                  RunAwayTime := 0;
               end;
            end;
         end;

         if Master <> nil then begin
            if Master.BoSlaveRelax then begin
               //주인이 휴식하라고 함...
               inherited Run;
               exit;
            end;
         end;

         if TargetX <> -1 then begin //가야할 곳이 있음
            GotoTargetXY;
         end else begin
            // 2003/03/18 시야내에 아무도 없으면 배회하지 않음
//          if (TargetCret = nil) and ((RefObjCount > 0) or (HideMode)) then
//          if (TargetCret = nil) then
               Wondering; //배회함
         end;
      end;
   end;

   inherited Run;
end;


{----------------------------------------------------------------------}
{ TChickenDeer类实现 - 鸡鹿 }

{ Create - 构造函数
  功能: 初始化鸡鹿对象 }
constructor TChickenDeer.Create;
begin
   inherited Create;
   ViewRange := 5;
end;

{ Run - 运行处理
  功能: 鸡鹿的运行逻辑，遇到敌人会逃跑 }
procedure TChickenDeer.Run;
var
   i, d, dis, ndir, runx, runy: integer;
   cret, nearcret: TCreature;
begin
   dis := 9999;
   nearcret := nil;
   if not Death and not RunDone and not BoGhost and (StatusArr[POISON_STONE] = 0) then begin
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         //상속받은 run 에서 WalkTime 재설정함.
         for i:=0 to VisibleActors.Count-1 do begin
            cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
            if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
               d := abs(CX-cret.CX) + abs(CY-cret.CY);
               if d < dis then begin
                  dis := d;
                  nearcret := cret;
               end;
            end;
         end;
         if nearcret <> nil then begin
            BoRunAwayMode := TRUE; //달아나는 모드
            TargetCret := nearcret;
         end else begin
            BoRunAwayMode := FALSE;
            TargetCret := nil;
         end;
      end;
      if BoRunAwayMode and (TargetCret <> nil) then begin
         if GetCurrentTime - WalkTime > NextWalkTime then begin
            //상속받은 run에서 WalkTime 재설정함
            if (abs(CX-TargetCret.CX) <= 6) and (abs(CY-TargetCret.CY) <= 6) then begin
               //도망감.
               ndir := GetNextDirection (TargetCret.CX, TargetCret.CY, CX, CY);
               GetNextPosition (PEnvir, TargetCret.CX, TargetCret.CY, ndir, 5, TargetX, TargetY);
            end;
         end;
      end;
   end;
   inherited Run;
end;

{------------------- TATMonster类实现 - 攻击型怪物 -------------------}

{ Create - 构造函数
  功能: 初始化攻击型怪物 }
constructor TATMonster.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
end;

{ Destroy - 析构函数
  功能: 释放攻击型怪物资源 }
destructor TATMonster.Destroy;
begin
   inherited Destroy;
end;

{ Run - 运行处理
  功能: 攻击型怪物的运行逻辑，主动搜索并攻击最近的敌人 }
procedure TATMonster.Run;
begin
   if not Death and not RunDone and not BoGhost and (StatusArr[POISON_STONE] = 0) then begin
      if (GetTickCount - SearchEnemyTime > 8000) or ((GetTickCount - SearchEnemyTime > 1000) and (TargetCret = nil)) then begin
         SearchEnemyTime := GetTickCount;
         MonsterNormalAttack;
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TSlowATMonster类实现 - 慢速攻击怪物 }

{ Create - 构造函数
  功能: 初始化慢速攻击怪物 }
constructor TSlowATMonster.Create;
begin
   inherited Create;
end;


{---------------------------------------------------------------------------}
{ TScorpion类实现 - 蝎子 }

{ Create - 构造函数
  功能: 初始化蝎子怪物 }
constructor TScorpion.Create;
begin
   inherited Create;
   BoAnimal := TRUE;  //썰면 전갈꼬리가 나옴
end;


{---------------------------------------------------------------------------}
{ TSpitSpider类实现 - 吐液蜘蛛 }

{ Create - 构造函数
  功能: 初始化吐液蜘蛛怪物 }
constructor TSpitSpider.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
   BoAnimal := TRUE;  //썰면 침거미이빨이 나옴
   BoUsePoison := TRUE;
end;

{ SpitAttack - 吐液攻击
  功能: 执行吐液远程攻击，可使目标中毒
  参数:
    dir - 攻击方向 }
procedure  TSpitSpider.SpitAttack (dir: byte);
var
   i, k,  mx, my, dam, armor: integer;
   cret: TCreature;
begin
   self.Dir := dir;
   with WAbil do
      dam := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
   if dam <= 0 then exit;

   SendRefMsg (RM_HIT, self.Dir, CX, CY, 0, '');

   for i:=0 to 4 do
      for k:=0 to 4 do begin
         if SpitMap[dir, i, k] = 1 then begin
            mx := CX - 2 + k;
            my := CY - 2 + i;
            cret := TCreature (PEnvir.GetCreature (mx, my, TRUE));
            if (cret <> nil) and (cret <> self) then begin
               if IsProperTarget(cret) then begin //cret.RaceServer = RC_USERHUMAN then begin
                  //맞는지 결정
                  if Random(cret.SpeedPoint) < AccuracyPoint then begin
                     //침거미 침은 마법방어력에 효과 있음.
                     //armor := (Lobyte(cret.WAbil.MAC) + Random(ShortInt(Hibyte(cret.WAbil.MAC)-Lobyte(cret.WAbil.MAC)) + 1));
                     //dam := dam - armor;
                     //if dam <= 0 then
                     //   if dam > -10 then dam := 1;
                     dam := cret.GetMagStruckDamage (self, dam);
                     if dam > 0 then begin
                        cret.StruckDamage (dam, self);
                        cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                                 cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '',
                                 300);

                        if BoUsePoison then begin
                           //체력이 감소하는 독에 중독 된다.
                           if Random(20 + cret.AntiPoison) = 0 then
                              cret.MakePoison (POISON_DECHEALTH, 30, 1);   //체력이 감소
                           //if Random(2) = 0 then
                           //   cret.MakePoison (POISON_STONE, 5);   //마비
                        end;
                     end;
                  end;

               end;
            end;
         end;
      end;

end;

{ AttackTarget - 攻击目标
  功能: 吐液蜘蛛的攻击目标逻辑
  返回值: 是否成功攻击 }
function  TSpitSpider.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if TargetInSpitRange (TargetCret, targdir) then begin
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;
            TargetFocusTime := GetTickCount;
            SpitAttack (targdir);
            BreakHolySeize;
         end;
         Result := TRUE;
      end else begin
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)
         else
            LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
      end;
   end;
end;


{---------------------------------------------------------------------------}
{ THighRiskSpider类实现 - 巨型蜘蛛 }

{ Create - 构造函数
  功能: 初始化巨型蜘蛛怪物 }
constructor THighRiskSpider.Create;
begin
   inherited Create;
   BoAnimal := FALSE;
   BoUsePoison := FALSE;
end;


{---------------------------------------------------------------------------}
{ TBigPoisionSpider类实现 - 巨型毒蜘蛛 }

{ Create - 构造函数
  功能: 初始化巨型毒蜘蛛怪物 }
constructor TBigPoisionSpider.Create;
begin
   inherited Create;
   BoAnimal := FALSE;
   BoUsePoison := TRUE;
end;


{---------------------------------------------------------------------------}
{ TGasAttackMonster类实现 - 毒气攻击怪物 }

{ Create - 构造函数
  功能: 初始化毒气攻击怪物 }
constructor TGasAttackMonster.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
   BoAnimal := TRUE;  //썰면 구룡환이 나옴
end;

{ GasAttack - 毒气攻击
  功能: 执行毒气攻击，可使目标麻痹或中毒
  参数:
    dir - 攻击方向
  返回值: 被攻击的目标 }
function  TGasAttackMonster.GasAttack (dir: byte): TCreature;
var
   i, k,  mx, my, dam, armor: integer;
   cret: TCreature;
begin
   Result := nil;
   self.Dir := dir;
   with WAbil do
      dam := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
   if dam <= 0 then exit;

   SendRefMsg (RM_HIT, self.Dir, CX, CY, 0, '');

   cret := GetFrontCret;
   if cret <> nil then begin
      if IsProperTarget (cret) then begin 
         //맞는지 결정
         if Random(cret.SpeedPoint) < AccuracyPoint then begin
            //구더기 가스는 마법방어력에 효과 있음.
            //armor := (Lobyte(cret.WAbil.MAC) + Random(ShortInt(Hibyte(cret.WAbil.MAC)-Lobyte(cret.WAbil.MAC)) + 1));
            //dam := dam - armor;
            //if dam <= 0 then
            //   if dam > -10 then dam := 1;
            dam := cret.GetMagStruckDamage (self, dam);
            if dam > 0 then begin
               cret.StruckDamage (dam, self);
               cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                        cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '',
                        300);

               //마비 되는 독에 중독 된다.
               if RaceServer = RC_TOXICGHOST then
               begin
                  if Random(20 + cret.AntiPoison) = 0 then
                     cret.MakePoison (POISON_DECHEALTH, 30, 1);   //체력감소
               end else begin
                  if Random(20 + cret.AntiPoison) = 0 then
                     cret.MakePoison (POISON_STONE, 5, 0);   //마비
               end;
               Result := cret;
            end;
         end;

      end;
   end;
end;

{ AttackTarget - 攻击目标
  功能: 毒气怪物的攻击目标逻辑
  返回值: 是否成功攻击 }
function  TGasAttackMonster.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if TargetInAttackRange (TargetCret, targdir) then begin
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;
            TargetFocusTime := GetTickCount;
            GasAttack (targdir);
            BreakHolySeize;
         end;
         Result := TRUE;
      end else begin
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)
         else
            LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
      end;
   end;
end;


{---------------------------------------------------------------------------}
{ TCowMonster类实现 - 牛魔 }

{ Create - 构造函数
  功能: 初始化牛魔怪物 }
constructor TCowMonster.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
end;


{ TMagCowMonster类实现 - 魔法牛魔 }

{ Create - 构造函数
  功能: 初始化魔法牛魔怪物 }
constructor TMagCowMonster.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
end;

{ MagicAttack - 魔法攻击
  功能: 执行魔法攻击
  参数:
    dir - 攻击方向 }
procedure TMagCowMonster.MagicAttack (dir: byte);
var
   i, k,  mx, my, dam, armor: integer;
   cret: TCreature;
begin
   self.Dir := dir;
   with WAbil do
      dam := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
   if dam <= 0 then exit;

   SendRefMsg (RM_HIT, self.Dir, CX, CY, 0, '');

   cret := GetFrontCret;
   if cret <> nil then begin
      if IsProperTarget (cret) then begin  //.RaceServer = RC_USERHUMAN then begin //사람만 공격함
         //맞는지 결정 (마법 회피로 결정)
         if cret.AntiMagic <= Random(10) then begin
            //마법방어력에 효과 있음.
            //armor := (Lobyte(cret.WAbil.MAC) + Random(ShortInt(Hibyte(cret.WAbil.MAC)-Lobyte(cret.WAbil.MAC)) + 1));
            //dam := dam - armor;
            //if dam <= 0 then
            //   if dam > -10 then dam := 1;
            dam := cret.GetMagStruckDamage (self, dam);
            if dam > 0 then begin
               cret.StruckDamage (dam, self);
               cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                        cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '',
                        300);
            end;
         end;

      end;
   end;
end;

{ AttackTarget - 攻击目标
  功能: 魔法牛魔的攻击目标逻辑
  返回值: 是否成功攻击 }
function  TMagCowMonster.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if TargetInAttackRange (TargetCret, targdir) then begin
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;
            TargetFocusTime := GetTickCount;
            MagicAttack (targdir);
            BreakHolySeize;
         end;
         Result := TRUE;
      end else begin
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)
         else
            LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
      end;
   end;
end;


{---------------------------------------------------------------------------}
{ TCowKingMonster类实现 - 牛魔王 }

{ Create - 构造函数
  功能: 初始化牛魔王BOSS }
constructor TCowKingMonster.Create;
begin
   inherited Create;
   SearchRate := 500 + longword(Random(1500));
   JumpTime := GetTickCount;
   RushMode := TRUE; //마법에 맞아도 돌진한다.
   CrazyCount := 0;
   CrazyReadyMode := FALSE;
   CrazyKingMode := FALSE;
end;

{ Attack - 攻击
  功能: 牛魔王的攻击方法，造成双重伤害 }
procedure TCowKingMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
begin
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   inherited HitHit2 (target, pwr div 2, pwr div 2, TRUE);
end;

{ Initialize - 初始化
  功能: 初始化牛魔王的攻击和移动速度 }
procedure TCowKingMonster.Initialize;
begin
   oldhittime := NextHitTime;
   oldwalktime := NextWalkTime;
   inherited Initialize;
end;

{ Run - 运行处理
  功能: 牛魔王的运行逻辑，包括瞁移和暴走模式 }
procedure TCowKingMonster.Run;
var
   nn, nx, ny, old: integer;
   ncret: TCreature;
begin
   if not Death and not RunDone and not BoGhost then begin
      if GetTickCount - JumpTime > 30 * 1000 then begin
         JumpTime := GetTickCount;
         if (TargetCret <> nil) and (SiegeLockCount >= 5) then begin  //4명에게 둘러 쌓임
            //nn := Random(VisibleActors.Count-2) + 1;
            //ncret := TCreature (PTVisibleActor(VisibleActors[nn]).cret);
            //if ncret <> nil then SelectTarget (ncret);
            GetBackPosition (TargetCret, nx, ny);
            if PEnvir.CanWalk (nx, ny, FALSE) then begin
               SpaceMove (PEnvir.MapName, nx, ny, 0);
            end else
               RandomSpaceMove (PEnvir.MapName, 0);
            exit;
         end;
      end;
      old := CrazyCount;
      CrazyCount := 7 - WAbil.HP div (WAbil.MaxHP div 7);

      if (CrazyCount >= 2) and (CrazyCount <> old) then begin
         CrazyReadyMode := TRUE;
         CrazyReady := GetTickCount;
      end;

      if CrazyReadyMode then begin  //맞고만 있음
         if GetTickCount - CrazyReady < 8 * 1000 then begin
            NextHitTime := 10000;
         end else begin
            CrazyReadyMode := FALSE;
            CrazyKingMode := TRUE;
            CrazyTime := GetTickCount;
         end;
      end;
      if CrazyKingMode then begin  //폭주
         if GetTickCount - CrazyTime < 8 * 1000 then begin
            NextHitTime := 500;
            NextWalkTime := 400;
         end else begin
            CrazyKingMode := FALSE;
            NextHitTime := oldhittime;
            NextWalkTime := oldwalktime;
         end;
      end;

   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TLightingZombi类实现 - 闪电僵尸 }

{ Create - 构造函数
  功能: 初始化闪电僵尸怪物 }
constructor TLightingZombi.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
end;

{ LightingAttack - 闪电攻击
  功能: 执行远程闪电攻击
  参数:
    dir - 攻击方向 }
procedure TLightingZombi.LightingAttack (dir: integer);
var
   i, k,  sx, sy, tx, ty, mx, my, pwr: integer;
begin
   self.Dir := dir;

   SendRefMsg (RM_LIGHTING, 1, CX, CY, Integer(TargetCret), '');

   if GetNextPosition (PEnvir, CX, CY, dir, 1, sx, sy) then begin
      GetNextPosition (PEnvir, CX, CY, dir, 9, tx, ty);
      with WAbil do
         pwr := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
      MagPassThroughMagic (sx, sy, tx, ty, dir, pwr, TRUE);
   end;

   BreakHolySeize;

end;

{ Run - 运行处理
  功能: 闪电僵尸的运行逻辑，保持距离并远程攻击 }
procedure TLightingZombi.Run;
var
   i, dis, d, targdir: integer;
   cret, nearcret: TCreature;
begin
   dis := 9999;
   nearcret := nil;
   if not Death and not RunDone and not BoGhost and (StatusArr[POISON_STONE] = 0) then begin
      if (GetTickCount - SearchEnemyTime > 8000) or ((GetTickCount - SearchEnemyTime > 1000) and (TargetCret = nil)) then begin
         SearchEnemyTime := GetTickCount;
         MonsterNormalAttack;
      end;
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         //상속받은 run에서 WalkTime 재설정함
         if TargetCret <> nil then
            if (abs(CX-TargetCret.CX) <= 4) and (abs(CY-TargetCret.CY) <= 4) then begin
               if (abs(CX-TargetCret.CX) <= 2) and (abs(CY-TargetCret.CY) <= 2) then
                  //너무 가까우면, 잘 도망 안감.
                  if Random(3) <> 0 then begin
                     inherited Run;
                     exit;
                  end;
                //도망감.
               GetBackPosition (self, TargetX, TargetY);
            end;
      end;
      if TargetCret <> nil then begin
         if (abs(CX-TargetCret.CX) < 6) and (abs(CY-TargetCret.CY) < 6) then begin
            if GetCurrentTime - HitTime > NextHitTime then begin
               HitTime := GetCurrentTime;
               targdir := GetNextDirection (CX, CY, TargetCret.CX, TargetCret.CY);
               LightingAttack (targdir);
            end;
         end;
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TDigOutZombi类实现 - 挖地僵尸 }

{ Create - 构造函数
  功能: 初始化挖地僵尸怪物 }
constructor TDigOutZombi.Create;
begin
   inherited Create;
   RunDone := FALSE;
   ViewRange := 7;
   SearchRate := 2500 + longword(Random(1500));
   SearchTime := GetTickCount;
   RaceServer := RC_DIGOUTZOMBI;
   HideMode := TRUE;
end;

{ ComeOut - 出现
  功能: 从地下钻出并显现 }
procedure TDigOutZombi.ComeOut;
var
   event: TEvent;
begin
   event := TEvent.Create (PEnvir, CX, CY, ET_DIGOUTZOMBI, 5 * 60 * 1000, TRUE);
   EventMan.AddEvent (event);
   HideMode := FALSE;
   SendRefMsg (RM_DIGUP, Dir, CX, CY, integer(event), '');
end;

{ Run - 运行处理
  功能: 挖地僵尸的运行逻辑，接近敌人时钻出 }
procedure TDigOutZombi.Run;
var
   i, dis, d, targdir: integer;
   cret, nearcret: TCreature;
begin
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         nearcret := nil;
         //WalkTime := GetTickCount;  상속받은 run에서 재설정함
         if HideMode then begin //아직 모습을 나타내지 않았음.
            for i:=0 to VisibleActors.Count-1 do begin
               cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
               if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
                  if (abs(CX-cret.CX) <= 3) and (abs(CY-cret.CY) <= 3) then begin
                     ComeOut; //밖으로 나오다. 보인다.
                     WalkTime := GetCurrentTime + 1000;
                     break;
                  end;
               end;
            end;
         end else begin
            if (GetTickCount - SearchEnemyTime > 8000) or ((GetTickCount - SearchEnemyTime > 1000) and (TargetCret = nil)) then begin
               SearchEnemyTime := GetTickCount;
               MonsterNormalAttack;
            end;
         end;
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TZilKinZombi类实现 - 复活僵尸 }

{ Create - 构造函数
  功能: 初始化复活僵尸怪物 }
constructor TZilKinZombi.Create;
begin
   inherited Create;
   ViewRange := 6;
   SearchRate := 2500 + longword(Random(1500));
   SearchTime := GetTickCount;
   RaceServer := RC_ZILKINZOMBI;
   LifeCount := 0;
   if Random(3) = 0 then
      LifeCount := 1 + Random(3);
end;

{ Die - 死亡
  功能: 复活僵尸的死亡处理，记录复活时间 }
procedure TZilKinZombi.Die;
begin
   inherited Die;
   if LifeCount > 0 then begin
      deathstart := GetTickCount;
      RelifeTime := (4 + Random (20)) * 1000;
   end;
   Dec (LifeCount);
end;

{ Run - 运行处理
  功能: 复活僵尸的运行逻辑，死后可复活 }
procedure TZilKinZombi.Run;
begin
   if Death and (not BoGhost) and (LifeCount >= 0) and (StatusArr[POISON_STONE] = 0) then begin  //죽었음, 고스트상태는 아님
      if VisibleActors.Count > 0 then begin
         if GetTickCount - deathstart >= RelifeTime then begin
            Abil.MaxHP := Abil.MaxHP div 2;
            FightExp := FightExp div 2;
            Abil.HP  := Abil.MaxHP;
            WAbil.HP := Abil.MaxHP;
            Alive;
            WalkTime := GetCurrentTime + 1000;
         end;
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TWhiteSkeleton类实现 - 白骨骷髅(召唤兽) }

{ Create - 构造函数
  功能: 初始化白骨骷髅召唤兽 }
constructor TWhiteSkeleton.Create;
begin
   inherited Create;
   bofirst := TRUE;
   HideMode := TRUE;
   RaceServer := RC_WHITESKELETON;
   ViewRange := 6;
end;

{ RecalcAbilitys - 重算能力值
  功能: 重新计算白骨骷髅的能力值 }
procedure TWhiteSkeleton.RecalcAbilitys;
begin
   inherited RecalcAbilitys;
   ResetSkeleton;
//   ApplySlaveLevelAbilitys;
end;

{ ResetSkeleton - 重置骷髅
  功能: 根据召唤等级重置攻击和移动速度 }
procedure TWhiteSkeleton.ResetSkeleton;
begin
   NextHitTime := 3000 - (SlaveMakeLevel * 600);
   NextWalkTime := 1200 - (SlaveMakeLevel * 250);
   //WAbil.DC := MakeWord(Lobyte(WAbil.DC), Hibyte(WAbil.DC) + SlaveMakeLevel);
   //WAbil.MaxHP := WAbil.MaxHP + SlaveMakeLevel * 5;
   //WAbil.HP := WAbil.MaxHP;
   //AccuracyPoint := 11 + SlaveMakeLevel;
   WalkTime := GetCurrentTime + 2000;
end;

{ Run - 运行处理
  功能: 白骨骷髅的运行逻辑，首次运行时钻出 }
procedure TWhiteSkeleton.Run;
var
   i: integer;
begin
   if bofirst then begin
      bofirst := FALSE;
      Dir := 5;
      HideMode := FALSE;
      SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');
      ResetSkeleton;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TScultureMonster类实现 - 石像怪物 }

{ Create - 构造函数
  功能: 初始化石像怪物 }
constructor TScultureMonster.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
   ViewRange := 7;
   BoStoneMode := TRUE; //처음에는 돌로 굳어져 있음...
   CharStatusEx := STATE_STONE_MODE;
end;

{ MeltStone - 解除石化
  功能: 解除石像状态 }
procedure TScultureMonster.MeltStone;
begin
   CharStatusEx := 0;
   CharStatus := GetCharStatus;
   SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');  //녹는 애니메이션
   BoStoneMode := FALSE;
end;

{ MeltStoneAll - 解除所有石化
  功能: 解除自己和周围石像怪物的石化状态 }
procedure TScultureMonster.MeltStoneAll;
var
   i: integer;
   cret: TCreature;
   rlist: TList;
begin
   MeltStone;
   rlist := TList.Create;
   GetMapCreatures (PEnvir, CX, CY, 7, rlist);
   for i:=0 to rlist.Count-1 do begin
      cret := TCreature (rlist[i]);
      if cret.BoStoneMode then begin
         if cret is TScultureMonster then
            TScultureMonster(cret).MeltStone;
      end;
   end;
   rlist.Free;
end;

{ Run - 运行处理
  功能: 石像怪物的运行逻辑，接近敌人时解除石化 }
procedure TScultureMonster.Run;
var
   i, dis, d, targdir: integer;
   cret, nearcret: TCreature;
begin
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         nearcret := nil;
         //WalkTime := GetTickCount;  상속받은 run에서 재설정함
         if BoStoneMode then begin //아직 모습을 나타내지 않았음.
            for i:=0 to VisibleActors.Count-1 do begin
               cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
               if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
                  if (abs(CX-cret.CX) <= 2) and (abs(CY-cret.CY) <= 2) then begin
                     MeltStoneAll; //석상상태에서 녹는다, 주의동료들도 함께 녹는다.
                     WalkTime := GetCurrentTime + 1000;
                     break;
                  end;
               end;
            end;
         end else begin
            if (GetTickCount - SearchEnemyTime > 8000) or ((GetTickCount - SearchEnemyTime > 1000) and (TargetCret = nil)) then begin
               SearchEnemyTime := GetTickCount;
               MonsterNormalAttack;
            end;
         end;
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TScultureKingMonster类实现 - 祖玛王 }

{ Create - 构造函数
  功能: 初始化祖玛王BOSS }
constructor TScultureKingMonster.Create;
begin
   inherited Create;
   SearchRate := 1500 + longword(Random(1500));
   ViewRange := 8;
   BoStoneMode := TRUE; //처음에는 돌로 굳어져 있음...
   CharStatusEx := STATE_STONE_MODE;
   Dir := 5;
   DangerLevel := 5; //5번의 위기..
   childlist := TList.Create;
   BoCallFollower := TRUE;
end;

{ Destroy - 析构函数
  功能: 释放祖玛王资源 }
destructor TScultureKingMonster.Destroy;
begin
   childlist.Free;
   inherited Destroy;
end;

{ MeltStone - 解除石化
  功能: 解除祖玛王的石像状态 }
procedure TScultureKingMonster.MeltStone;
var
   i: integer;
   cret: TCreature;
   event: TEvent;
begin
   CharStatusEx := 0;
   CharStatus := GetCharStatus;
   SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');
   BoStoneMode := FALSE;
   event := TEvent.Create (PEnvir, CX, CY, ET_SCULPEICE, 5 * 60 * 1000, TRUE);
   EventMan.AddEvent (event);
end;

{ CallFollower - 召唤小怪
  功能: 召唤祖玛系列小怪 }
procedure TScultureKingMonster.CallFollower;
const
   MAX_FOLLOWERS = 4;
var
   i, count, nx, ny: integer;
   monname: string;
   mon: TCreature;
   followers: array[0..MAX_FOLLOWERS-1] of string; // = (주마호법', �봇�켔', 마궁사', 랠맙');
begin
   count := 6 + Random (6);
   GetFrontPosition (self, nx, ny);

   followers[0] := __ZumaMonster1;
   followers[1] := __ZumaMonster2;
   followers[2] := __ZumaMonster3;
   followers[3] := __ZumaMonster4;

   for i:=1 to count do begin
      if childlist.Count < 30 then begin
         monname := followers[Random(MAX_FOLLOWERS)];
         mon := UserEngine.AddCreatureSysop (MapName, nx, ny, monname);
         if mon <> nil then
            childlist.Add (mon);
      end;
   end;
end;

{ Attack - 攻击
  功能: 祖玛王的攻击方法 }
procedure TScultureKingMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
begin
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   inherited HitHit2 (target, 0, pwr, TRUE);
end;

{ Run - 运行处理
  功能: 祖玛王的运行逻辑，包括解除石化和召唤小怪 }
procedure TScultureKingMonster.Run;
var
   i, dis, d, targdir: integer;
   cret, nearcret: TCreature;
begin
   if (not BoGhost) and (not Death) and (StatusArr[POISON_STONE] = 0) then begin
      if GetCurrentTime - WalkTime > NextWalkTime then begin
         nearcret := nil;
         //WalkTime := GetTickCount;  상속받은 run에서 재설정함
         if BoStoneMode then begin //아직 모습을 나타내지 않았음.
            for i:=0 to VisibleActors.Count-1 do begin
               cret := TCreature (PTVisibleActor(VisibleActors[i]).cret);
               if (not cret.Death) and (IsProperTarget(cret)) and (not cret.BoHumHideMode or BoViewFixedHide) then begin
                  if (abs(CX-cret.CX) <= 2) and (abs(CY-cret.CY) <= 2) then begin
                     MeltStone; //석상상태에서 녹는다
                     WalkTime := GetCurrentTime + 2000;
                     break;
                  end;
               end;
            end;
         end else begin
            if (GetTickCount - SearchEnemyTime > 8000) or ((GetTickCount - SearchEnemyTime > 1000) and (TargetCret = nil)) then begin
               SearchEnemyTime := GetTickCount;
               MonsterNormalAttack;
            end;

            if BoCallFollower then begin
               //5번의 시련
               if ((WAbil.HP / WAbil.MaxHP * 5) < DangerLevel) and (DangerLevel > 0) then begin
                  Dec (DangerLevel);
                  CallFollower;
               end;
               if WAbil.HP = WAbil.MaxHP then DangerLevel := 5;  //초기화
            end;

         end;

         for i:=childlist.Count-1 downto 0 do begin
            if (TCreature(childlist[i]).Death) or (TCreature(childlist[i]).BoGhost) then begin
               childlist.Delete(i);
            end;
         end;
      end;
   end;
   inherited Run;
end;

{ GasAttack - 毒气攻击
  功能: 毒气攻击，可解除目标隐身状态 }
function  TGasMothMonster.GasAttack (dir: byte): TCreature;
var
   cret: TCreature;
begin
   cret := inherited GasAttack (dir);
   if cret <> nil then begin  //이 가스는 은신이 풀린다.
      if Random(3) = 0 then begin
         //if cret.BoFixedHideMode then begin //고정 은신술, 투명반지도 풀림
            if cret.BoHumHideMode then begin
               cret.StatusArr[STATE_TRANSPARENT] := 1;
            end;
         //end;
      end;
   end;
   Result := cret;
end;

{ Run - 运行处理
  功能: 毒气飞蛾的运行逻辑，可以看到隐身玩家 }
procedure TGasMothMonster.Run;
var
   i, dis, d: integer;
   cret, nearcret: TCreature;
begin
   dis := 9999;
   nearcret := nil;
   if not Death and not RunDone and not BoGhost and (StatusArr[POISON_STONE] = 0) then begin
      if (GetTickCount - SearchEnemyTime > 8000) or ((GetTickCount - SearchEnemyTime > 1000) and (TargetCret = nil)) then begin
         SearchEnemyTime := GetTickCount;
         MonsterDetecterAttack;   //숨어있는 몹을 볼 수 있다.
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TGasDungMonster类实现 - 毒气怪物 }

{ Create - 构造函数
  功能: 初始化毒气怪物 }
constructor TGasDungMonster.Create;
begin
   inherited Create;
   ViewRange := 7;
end;


{---------------------------------------------------------------------------}
{ TElfMonster类实现 - 神兽(变身前) }

{ Create - 构造函数
  功能: 初始化神兽(变身前) }
constructor TElfMonster.Create;
begin
   inherited Create;
   ViewRange := 6;
   HideMode := TRUE;
   NoAttackMode := TRUE;
   bofirst := TRUE;
end;

{ RecalcAbilitys - 重算能力值
  功能: 重新计算神兽的能力值 }
procedure TElfMonster.RecalcAbilitys;
begin
   inherited RecalcAbilitys;
   ResetElfMon;
end;

{ ResetElfMon - 重置神兽
  功能: 根据召唤等级重置移动速度 }
procedure TElfMonster.ResetElfMon;
begin
   //NextHitTime := 3000 - (SlaveMakeLevel * 600);  //공격 안함
   NextWalkTime := 500 - (SlaveMakeLevel * 50);
   WalkTime := GetCurrentTime + 2000;
end;

{ AppearNow - 立即出现
  功能: 神兽立即显现 }
procedure TElfMonster.AppearNow;
begin
   bofirst := FALSE;
   HideMode := FALSE;
   //SendRefMsg (RM_TURN, Dir, CX, CY, 0, '');
   //Appear;
   //ResetElfMon;
   RecalcAbilitys;
   WalkTime := WalkTime + 800; //변신후 약간 딜레이 있음
end;

{ Run - 运行处理
  功能: 神兽(变身前)的运行逻辑，遇敌变身 }
procedure TElfMonster.Run;
var
   cret: TCreature;
   bochangeface: Boolean;
begin
   if bofirst then begin
      bofirst := FALSE;
      HideMode := FALSE;
      SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');
      ResetElfMon;
   end;
   if Death then begin  //신수는 시체가 없다.
      if GetTickCount - DeathTime > 2 * 1000 then begin
         MakeGhost;
      end;
   end else begin
      bochangeface := FALSE;
      if TargetCret <> nil then bochangeface := TRUE;
      if Master <> nil then
         if (Master.TargetCret <> nil) or (Master.LastHiter <> nil) then
            bochangeface := TRUE;

      if bochangeface then begin  //공격 대상이 있는 경우->변신
         cret := MakeClone (__ShinSu1, self);
         if cret <> nil then begin
            //SendRefMsg (RM_CHANGEFACE, 0, integer(self), integer(cret), 0, '');
            if cret is TElfWarriorMonster then
               TElfWarriorMonster(cret).AppearNow;
            Master := nil;
            KickException;
         end;
      end;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TElfWarriorMonster类实现 - 神兽(变身后) }

{ Create - 构造函数
  功能: 初始化神兽(变身后) }
constructor TElfWarriorMonster.Create;
begin
   inherited Create;
   ViewRange := 6;
   HideMode := TRUE;
   //NoAttackMode := TRUE;
   bofirst := TRUE;
   BoUsePoison := FALSE;
end;

{ RecalcAbilitys - 重算能力值
  功能: 重新计算神兽(变身后)的能力值 }
procedure TElfWarriorMonster.RecalcAbilitys;
begin
   inherited RecalcAbilitys;
   ResetElfMon;
end;

{ ResetElfMon - 重置神兽
  功能: 根据召唤等级重置攻击和移动速度 }
procedure TElfWarriorMonster.ResetElfMon;
begin
   //NextHitTime := 3000 - (SlaveMakeLevel * 600);
   //NextWalkTime := 1200 - (SlaveMakeLevel * 250);
   NextHitTime := 1500 - (SlaveMakeLevel * 100);
   NextWalkTime := 500 - (SlaveMakeLevel * 50);
   WalkTime := GetCurrentTime + 2000;
end;

{ AppearNow - 立即出现
  功能: 神兽(变身后)立即显现 }
procedure TElfWarriorMonster.AppearNow;
begin
   bofirst := FALSE;
   HideMode := FALSE;
   SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');
   RecalcAbilitys;
   //ResetElfMon;
   WalkTime := WalkTime + 800; //변신후 약간 딜레이 있음
   changefacetime := GetTickCount;
end;

{ Run - 运行处理
  功能: 神兽(变身后)的运行逻辑，无敌时变回 }
procedure TElfWarriorMonster.Run;
var
   cret: TCreature;
   bochangeface: Boolean;
begin
   if bofirst then begin
      bofirst := FALSE;
      HideMode := FALSE;
      SendRefMsg (RM_DIGUP, Dir, CX, CY, 0, '');
      ResetElfMon;
   end;
   if Death then begin  //신수는 시체가 없다.
      if GetTickCount - DeathTime > 2 * 1000 then begin
         MakeGhost;
      end;
   end else begin
      bochangeface := TRUE;
      if TargetCret <> nil then bochangeface := FALSE;
      if Master <> nil then
         if (Master.TargetCret <> nil) or (Master.LastHiter <> nil) then
            bochangeface := FALSE;

      if bochangeface then begin  //공격 대상이 있는 경우->변신
         if GetTickCount - changefacetime > 60 * 1000 then begin
            cret := MakeClone (__ShinSu, self);
            if cret <> nil then begin
               SendRefMsg (RM_DIGDOWN, Dir, CX, CY, 0, ''); //변신이 끝난 후에 사라진다.
               SendRefMsg (RM_CHANGEFACE, 0, integer(self), integer(cret), 0, '');
               if cret is TElfMonster then begin
                  TElfMonster(cret).AppearNow;
               end;
               Master := nil;
               KickException;
            end;
         end;
      end else
         changefacetime := GetTickCount;
   end;
   inherited Run;
end;


{---------------------------------------------------------------------------}
{ TCriticalMonster类实现 - 暴击怪物 }

{ Create - 构造函数
  功能: 初始化暴击怪物 }
constructor TCriticalMonster.Create;
begin
   inherited Create;
   criticalpoint := 0;
end;

{ Attack - 攻击
  功能: 暴击怪物的攻击方法，可造成暴击伤害 }
procedure TCriticalMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
begin
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   Inc (criticalpoint);

   if (criticalpoint > 5) or (Random(10) = 0) then begin
      criticalpoint := 0;
      pwr := Round (pwr * (Abil.MaxMP / 10));
      inherited HitHitEx2 (target, RM_LIGHTING, 0, pwr, TRUE);
   end else
      inherited HitHit2 (target, 0, pwr, TRUE);
end;


{---------------------------------------------------------------------------}
{ TDoubleCriticalMonster类实现 - 双格暴击怪物 }

{ Create - 构造函数
  功能: 初始化双格暴击怪物 }
constructor TDoubleCriticalMonster.Create;
begin
   inherited Create;
   criticalpoint := 0;
end;

{ DoubleCriticalAttack - 双格暴击攻击
  功能: 执行双格范围的暴击攻击 }
procedure  TDoubleCriticalMonster.DoubleCriticalAttack (dam: integer; dir: byte);
var
   i, k,  mx, my, armor: integer;
   cret: TCreature;
begin
   self.Dir := dir;
   if dam <= 0 then exit;

   SendRefMsg (RM_LIGHTING, self.Dir, CX, CY, 0, '');

   for i:=0 to 4 do
      for k:=0 to 4 do begin
         if SpitMap[dir, i, k] = 1 then begin
            mx := CX - 2 + k;
            my := CY - 2 + i;
            cret := TCreature (PEnvir.GetCreature (mx, my, TRUE));
            if (cret <> nil) and (cret <> self) then begin
               if IsProperTarget(cret) then begin //cret.RaceServer = RC_USERHUMAN then begin
                  //맞는지 결정
                  if Random(cret.SpeedPoint) < AccuracyPoint then begin
                     //침거미 침은 마법방어력에 효과 있음.
                     //armor := (Lobyte(cret.WAbil.MAC) + Random(ShortInt(Hibyte(cret.WAbil.MAC)-Lobyte(cret.WAbil.MAC)) + 1));
                     //dam := dam - armor;
                     //if dam <= 0 then
                     //   if dam > -10 then dam := 1;
                     dam := cret.GetMagStruckDamage (self, dam);
                     if dam > 0 then begin
                        cret.StruckDamage (dam, self);
                        cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                                 cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '',
                                 300);

                     end;
                  end;

               end;
            end;
         end;
      end;
end;

{ Attack - 攻击
  功能: 双格暴击怪物的攻击方法 }
procedure TDoubleCriticalMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
begin
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   Inc (criticalpoint);

   if (criticalpoint > 5) or (Random(10) = 0) then begin
      criticalpoint := 0;
      pwr := Round (pwr * (Abil.MaxMP / 10));
      DoubleCriticalAttack (pwr, Dir);
   end else
      inherited HitHit2 (target, 0, pwr, TRUE);
end;

{ TSkeletonSoldier类实现 - 骷髅士兵 }

{ Create - 构造函数
  功能: 初始化骷髅士兵 }
constructor TSkeletonSoldier.Create;
begin
   inherited Create;
end;

{ RangeAttack - 远程攻击
  功能: 执行远程范围攻击 }
procedure  TSkeletonSoldier.RangeAttack (dir: byte);
var
   i, k,  mx, my, dam, armor: integer;
   cret: TCreature;
   pwr: integer;
begin
   self.Dir := dir;
   with WAbil do
      dam := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
   if dam <= 0 then exit;

   SendRefMsg (RM_HIT, self.Dir, CX, CY, 0, '');

   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   for i:=0 to 4 do
      for k:=0 to 4 do begin
         if SpitMap[dir, i, k] = 1 then begin
            mx := CX - 2 + k;
            my := CY - 2 + i;
            cret := TCreature (PEnvir.GetCreature (mx, my, TRUE));
            if (cret <> nil) and (cret <> self) then begin
               if IsProperTarget(cret) then begin //cret.RaceServer = RC_USERHUMAN then begin
                  //맞는지 결정
                  if Random(cret.SpeedPoint) < AccuracyPoint then begin
                     inherited HitHit2 (cret, 0, pwr, TRUE);
                  end;
               end;
            end;
         end;
      end;
end;

{ AttackTarget - 攻击目标
  功能: 骷髅士兵的攻击目标逻辑
  返回值: 是否成功攻击 }
function  TSkeletonSoldier.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if TargetInSpitRange (TargetCret, targdir) then begin
         if GetCurrentTime - HitTime > NextHitTime then begin
            HitTime := GetCurrentTime;
            TargetFocusTime := GetTickCount;
            RangeAttack (targdir);
            BreakHolySeize;
         end;
         Result := TRUE;
      end else begin
         if TargetCret.MapName = self.MapName then
            SetTargetXY (TargetCret.CX, TargetCret.CY)
         else
            LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
      end;
   end;
end;

{ TSkeletonKingMonster类实现 - 骷髅王 }

{ Create - 构造函数
  功能: 初始化骷髅王BOSS }
constructor TSkeletonKingMonster.Create;
begin
   inherited Create;
   ChainShotCount := 6;
   BoStoneMode := FALSE;
   CharStatusEx := 0;
   CharStatus := GetCharStatus;
end;

{ CallFollower - 召唤小怪
  功能: 召唤骷髅系列小怪 }
procedure TSkeletonKingMonster.CallFollower;
const
   MAX_SKELFOLLOWERS = 3;
var
   i, count, nx, ny: integer;
   monname: string;
   mon: TCreature;
   followers: array[0..MAX_SKELFOLLOWERS-1] of string; // = (해골무장, 해골궁수, 해골병졸);
begin
   SendRefMsg (RM_LIGHTING, self.Dir, CX, CY, 0, '');
   count := 4 + Random (4);
   GetFrontPosition (self, nx, ny);

   followers[0] := 'Φ푫챎켔';
   followers[1] := 'Φ푫폹ㅲ';
   followers[2] := 'Φ푫쬙⑫';

   for i:=1 to count do begin
      if childlist.Count < 20 then begin
         monname := followers[Random(MAX_SKELFOLLOWERS)];
         mon := UserEngine.AddCreatureSysop (MapName, nx, ny, monname);
         if mon <> nil then
            childlist.Add (mon);
      end;
   end;
end;

{ Attack - 攻击
  功能: 骷髅王的攻击方法 }
procedure TSkeletonKingMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
begin
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   inherited HitHit2 (target, 0, pwr, TRUE);
end;

{ Run - 运行处理
  功能: 骷髅王的运行逻辑 }
procedure TSkeletonKingMonster.Run;
var
   i, dis, d, targdir: integer;
   cret : TCreature;
begin
   inherited Run;
end;

{ RangeAttack - 远程攻击
  功能: 执行远程飞斧攻击 }
procedure TSkeletonKingMonster.RangeAttack (targ: TCreature);
var
   dam, armor: integer;
begin
   if PEnvir.CanFly (CX, CY, targ.CX, targ.CY) then begin //도끼가 날아갈수 있는지.
      Dir := GetNextDirection (CX, CY, targ.CX, targ.CY);
      with WAbil do
         dam := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);
      if dam > 0 then begin
         dam := targ.GetHitStruckDamage (self, dam);
      end;
      if dam > 0 then begin
         targ.StruckDamage (dam, self);
         targ.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                  targ.WAbil.HP{lparam1}, targ.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '', 600 + _MAX(Abs(CX-targ.CX),Abs(CY-targ.CY)) * 50);
      end;
      SendRefMsg (RM_FLYAXE, Dir, CX, CY, Integer(targ), '');
   end;
end;

{ AttackTarget - 攻击目标
  功能: 骷髅王的攻击目标逻辑，近远程结合
  返回值: 是否成功攻击 }
function  TSkeletonKingMonster.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if GetCurrentTime - HitTime > NextHitTime then begin
         HitTime := GetCurrentTime;
         if (abs(CX-TargetCret.CX) <= 7) and (abs(CY-TargetCret.CY) <= 7) then begin
            if TargetInAttackRange (TargetCret, targdir) then begin
               TargetFocusTime := GetTickCount;
               Attack (TargetCret, targdir);
               Result := TRUE;
            end else begin
               if ChainShot < ChainShotCount-1 then begin
                  Inc (ChainShot);
                  TargetFocusTime := GetTickCount;
                  RangeAttack (TargetCret);
               end else begin
                  if Random(5) = 0 then
                     ChainShot := 0;
               end;
               Result := TRUE;
            end;
         end else begin
            if TargetCret.MapName = self.MapName then begin
               if (abs(CX-TargetCret.CX) <= 11) and (abs(CY-TargetCret.CY) <= 11) then begin
                  SetTargetXY (TargetCret.CX, TargetCret.CY)
               end;
            end else begin
               LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
            end;
         end;
      end;
   end;
end;

{ TBanyaGuardMonster类实现 - 般若护卫 }

{ Create - 构造函数
  功能: 初始化般若护卫 }
constructor TBanyaGuardMonster.Create;
begin
   inherited Create;
   ChainShotCount := 6;
   BoCallFollower := FALSE;
end;

{ Attack - 攻击
  功能: 般若护卫的攻击方法 }
procedure TBanyaGuardMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
begin
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));
   inherited HitHit2 (target, 0, pwr, TRUE);
end;

{ RangeAttack - 远程攻击
  功能: 执行远程魔法攻击 }
procedure TBanyaGuardMonster.RangeAttack (targ: TCreature);
var
   i, pwr, dam: integer;
   sx, sy, tx, ty : integer;
   list: TList;
   cret: TCreature;
begin
   Self.Dir := GetNextDirection (CX, CY, targ.CX, targ.CY);
   SendRefMsg (RM_LIGHTING, self.Dir, CX, CY, Integer(targ), '');
   if GetNextPosition (PEnvir, CX, CY, dir, 1, sx, sy) then begin
      GetNextPosition (PEnvir, CX, CY, dir, 9, tx, ty);
      with WAbil do
         pwr := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);

      list := TList.Create;
      PEnvir.GetAllCreature (targ.CX, targ.CY, TRUE, list);
      for i:=0 to list.Count-1 do begin
         cret := TCreature(list[i]);
         if IsProperTarget (cret) then begin
            dam := cret.GetMagStruckDamage (self, pwr);
            if dam > 0 then begin
               cret.StruckDamage (dam, self);
               cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                                  cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '', 800);
            end;
         end;
      end;
      list.Free;
   end;
end;

{ AttackTarget - 攻击目标
  功能: 般若护卫的攻击目标逻辑
  返回值: 是否成功攻击 }
function  TBanyaGuardMonster.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if GetCurrentTime - HitTime > NextHitTime then begin
         HitTime := GetCurrentTime;
         if (abs(CX-TargetCret.CX) <= 7) and (abs(CY-TargetCret.CY) <= 7) then begin
            if (TargetInAttackRange (TargetCret, targdir)) and (Random(3)<>0) then begin
               TargetFocusTime := GetTickCount;
               Attack (TargetCret, targdir);
               Result := TRUE;
            end else begin
               if ChainShot < ChainShotCount-1 then begin
                  Inc (ChainShot);
                  TargetFocusTime := GetTickCount;
                  RangeAttack (TargetCret);
               end else begin
                  if Random(5) = 0 then
                     ChainShot := 0;
               end;
               Result := TRUE;
            end;
         end else begin
            if TargetCret.MapName = self.MapName then begin
               if (abs(CX-TargetCret.CX) <= 11) and (abs(CY-TargetCret.CY) <= 11) then begin
                  SetTargetXY (TargetCret.CX, TargetCret.CY)
               end;
            end else begin
               LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
            end;
         end;
      end;
   end;
end;

{ TDeadCowKingMonster类实现 - 死亡牛魔王 }

{ Create - 构造函数
  功能: 初始化死亡牛魔王BOSS }
constructor TDeadCowKingMonster.Create;
begin
   inherited Create;
   ChainShotCount := 6;
   BoCallFollower := FALSE;
end;

{ Attack - 攻击
  功能: 死亡牛魔王的攻击方法，范围伤害 }
procedure TDeadCowKingMonster.Attack (target: TCreature; dir: byte);
var
   pwr: integer;
   i, ix, iy, ixf, ixt, iyf, iyt, dam: integer;
   list: TList;
   cret: TCreature;
begin
   Self.Dir := GetNextDirection (CX, CY, target.CX, target.CY);
   with WAbil do
      pwr := GetAttackPower (Lobyte(DC), ShortInt(Hibyte(DC)-Lobyte(DC)));

      ixf := _MAX(0, CX - 1); ixt := _MIN(pEnvir.MapWidth-1,  CX + 1);
      iyf := _MAX(0, CY - 1); iyt := _MIN(pEnvir.MapHeight-1, CY + 1);

   for ix := ixf to ixt do begin
      for iy := iyf to iyt do begin
         list := TList.Create;
         PEnvir.GetAllCreature (ix, iy, TRUE, list);
         for i:=0 to list.Count-1 do begin
            cret := TCreature(list[i]);
            if IsProperTarget (cret) then begin
               dam := cret.GetMagStruckDamage (self, pwr);
               if dam > 0 then begin
                  cret.StruckDamage (dam, self);
                  cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                                     cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '', 200);
               end;
            end;
         end;
         list.Free;
      end;
   end;
   SendRefMsg (RM_HIT, self.Dir, CX, CY, Integer(target), '');
// inherited HitHit2 (target, 0, pwr, TRUE);
end;

{ RangeAttack - 远程攻击
  功能: 执行远程范围魔法攻击 }
procedure TDeadCowKingMonster.RangeAttack (targ: TCreature);
var
   i, ix, iy, ixf, ixt, iyf, iyt, pwr, dam: integer;
   sx, sy, tx, ty : integer;
   list: TList;
   cret: TCreature;
begin
   Self.Dir := GetNextDirection (CX, CY, targ.CX, targ.CY);
   SendRefMsg (RM_LIGHTING, self.Dir, CX, CY, Integer(targ), '');
   if GetNextPosition (PEnvir, CX, CY, dir, 1, sx, sy) then begin
      GetNextPosition (PEnvir, CX, CY, dir, 9, tx, ty);
      with WAbil do
         pwr := Lobyte(DC) + Random(ShortInt(Hibyte(DC)-Lobyte(DC)) + 1);

      ixf := _MAX(0, targ.CX - 2); ixt := _MIN(pEnvir.MapWidth-1,  targ.CX + 2);
      iyf := _MAX(0, targ.CY - 2); iyt := _MIN(pEnvir.MapHeight-1, targ.CY + 2);

      for ix := ixf to ixt do begin
         for iy := iyf to iyt do begin
            list := TList.Create;
            PEnvir.GetAllCreature (ix, iy, TRUE, list);
            for i:=0 to list.Count-1 do begin
               cret := TCreature(list[i]);
               if IsProperTarget (cret) then begin
                  dam := cret.GetMagStruckDamage (self, pwr);
                  if dam > 0 then begin
                     cret.StruckDamage (dam, self);
                     cret.SendDelayMsg (TCreature(RM_STRUCK), RM_REFMESSAGE, dam{wparam},
                                        cret.WAbil.HP{lparam1}, cret.WAbil.MaxHP{lparam2}, Longint(self){hiter}, '', 800);
                  end;
               end;
            end;
            list.Free;
         end;
      end;
   end;
end;

{ AttackTarget - 攻击目标
  功能: 死亡牛魔王的攻击目标逻辑
  返回值: 是否成功攻击 }
function  TDeadCowKingMonster.AttackTarget: Boolean;
var
   targdir: byte;
begin
   Result := FALSE;
   if TargetCret <> nil then begin
      if GetCurrentTime - HitTime > NextHitTime then begin
         HitTime := GetCurrentTime;
         if (abs(CX-TargetCret.CX) <= 7) and (abs(CY-TargetCret.CY) <= 7) then begin
            if (TargetInAttackRange (TargetCret, targdir)) and (Random(3)<>0) then begin
               TargetFocusTime := GetTickCount;
               Attack (TargetCret, targdir);
               Result := TRUE;
            end else begin
               if ChainShot < ChainShotCount-1 then begin
                  Inc (ChainShot);
                  TargetFocusTime := GetTickCount;
                  RangeAttack (TargetCret);
               end else begin
                  if Random(5) = 0 then
                     ChainShot := 0;
               end;
               Result := TRUE;
            end;
         end else begin
            if TargetCret.MapName = self.MapName then begin
               if (abs(CX-TargetCret.CX) <= 11) and (abs(CY-TargetCret.CY) <= 11) then begin
                  SetTargetXY (TargetCret.CX, TargetCret.CY)
               end;
            end else begin
               LoseTarget;  //<!!주의> TargetCret := nil로 바뀜
            end;
         end;
      end;
   end;
end;

end.
