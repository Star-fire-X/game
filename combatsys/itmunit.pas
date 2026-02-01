{ ============================================================================
  单元名称: itmunit
  功能描述: 物品升级单元，处理装备的随机属性生成和升级
  
  主要功能:
  - 装备随机属性生成（武器、衣服、项链、手镯、戒指、头盔）
  - 未知属性装备生成（特殊属性装备）
  - 装备属性升级计算
  - 将用户物品的升级属性应用到标准物品
  
  属性说明:
  - Desc[0]: AC/DC（防御/破坏）
  - Desc[1]: MAC/MC（魔防/魔力）
  - Desc[2]: DC/SC（破坏/道力）
  - Desc[3]: MC（魔力）
  - Desc[4]: SC（道力）
  - Desc[5]: 需求类型（1:破坏, 2:魔力, 3:道力）
  - Desc[6]: 需求值/攻击速度
  - Desc[7]: 特殊属性（武器强度/不掉落）
  - Desc[8]: 未知属性标记
  
  作者: 传奇服务端开发组
  创建日期: 原始版本
  修改日期: 当前版本
============================================================================ }
unit itmunit;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Dialogs,
  ScktComp, syncobjs, MudUtil, HUtil32, Grobal2;

type
   { TItemUnit类
     功能: 物品升级处理类
     用途: 处理装备的随机属性生成和升级计算 }
   TItemUnit = class
   private
   public
      { GetUpgrade - 获取升级值 }
      function  GetUpgrade (count, ran: integer): integer;
      { UpgradeRandomWeapon - 随机升级武器属性 }
      procedure UpgradeRandomWeapon (pu: PTUserItem);
      { UpgradeRandomDress - 随机升级衣服属性 }
      procedure UpgradeRandomDress (pu: PTUserItem);
      { UpgradeRandomNecklace - 随机升级项链属性 }
      procedure UpgradeRandomNecklace (pu: PTUserItem);
      { UpgradeRandomBarcelet - 随机升级手镯属性 }
      procedure UpgradeRandomBarcelet (pu: PTUserItem);
      { UpgradeRandomNecklace19 - 随机升级19类项链属性（魔法回避/幸运） }
      procedure UpgradeRandomNecklace19 (pu: PTUserItem);
      { UpgradeRandomRings - 随机升级戒指属性 }
      procedure UpgradeRandomRings (pu: PTUserItem);
      { UpgradeRandomRings23 - 随机升级23类戒指属性（中毒抵抗/恢复） }
      procedure UpgradeRandomRings23 (pu: PTUserItem);
      { UpgradeRandomHelmet - 随机升级头盔属性 }
      procedure UpgradeRandomHelmet (pu: PTUserItem);

      { RandomSetUnknownHelmet - 设置未知属性头盔 }
      procedure RandomSetUnknownHelmet (pu: PTUserItem);
      { RandomSetUnknownRing - 设置未知属性戒指 }
      procedure RandomSetUnknownRing (pu: PTUserItem);
      { RandomSetUnknownBracelet - 设置未知属性手镯 }
      procedure RandomSetUnknownBracelet (pu: PTUserItem);

      { GetUpgradeStdItem - 获取升级后的标准物品属性 }
      procedure GetUpgradeStdItem (ui: TUserItem; var std: TStdItem);
   end;

implementation

uses
   svMain;


{ ========================================================================
  TItemUnit类实现
  ======================================================================== }

{ TItemUnit.GetUpgrade
  功能: 获取升级值
  参数:
    count - 最大升级次数
    ran - 随机范围（1/ran的概率成功）
  返回值: 升级值
  实现原理: 连续随机，每次成功则+1，失败则停止 }
function  TItemUnit.GetUpgrade (count, ran: integer): integer;
var
   i: integer;  // 循环计数器
begin
   Result := 0;
   for i:=0 to count-1 do begin
      if Random(ran) = 0 then Result := Result + 1
      else break;  // 失败则停止
   end;
end;

{ TItemUnit.UpgradeRandomWeapon
  功能: 随机升级武器属性
  参数:
    pu - 用户物品指针（必须是武器）
  实现原理: 随机生成武器的各项属性
    - Desc[0]: DC（破坏）
    - Desc[1]: MC（魔力）
    - Desc[2]: SC（道力）
    - Desc[5]: 准确
    - Desc[6]: 攻击速度（1-9减速，11-19加速）
    - Desc[7]: 武器强度 }
procedure TItemUnit.UpgradeRandomWeapon (pu: PTUserItem);
var
   up, n, i, incp: integer;  // 升级值和临时变量
begin
   // 破坏属性
   up := GetUpgrade (12, 15);
   if Random(15) = 0 then pu.Desc[0] := 1+up; // DC

   // 攻击速度
   up := GetUpgrade (12, 15);
   if Random(20) = 0 then begin
      incp := (1+up) div 3;  // 降低附加概率
      if incp > 0 then begin
         if Random(3) <> 0 then  pu.Desc[6] := incp  // 攻击速度(-)
         else pu.Desc[6] := 10 + incp;  // 攻击速度(+)
      end;
   end;

   // 魔力属性
   up := GetUpgrade (12, 15);
   if Random(15) = 0 then pu.Desc[1] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (12, 15);
   if Random(15) = 0 then pu.Desc[2] := 1+up; // SC

   // 准确属性
   up := GetUpgrade (12, 15);
   if Random(24) = 0 then pu.Desc[5] := 1 + (up div 2); // 准确(+)

   // 耐久度
   up := GetUpgrade (12, 12);
   if Random(3) < 2 then begin
      n := (1+up)*2000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;

   // 武器强度
   up := GetUpgrade (12, 15);
   if Random(10) = 0 then
      pu.Desc[7] := 1 + (up div 2); // 武器坚固程度

end;

{ TItemUnit.UpgradeRandomDress
  功能: 随机升级衣服属性
  参数:
    pu - 用户物品指针
  实现原理: 随机生成衣服的各项属性
    - Desc[0]: AC（防御）
    - Desc[1]: MAC（魔防）
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomDress (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 防御属性
   up := GetUpgrade (6, 15);
   if Random(30) = 0 then pu.Desc[0] := 1+up; // AC

   // 魔防属性
   up := GetUpgrade (6, 15);
   if Random(30) = 0 then pu.Desc[1] := 1+up; // MAC

   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 10);
   if Random(8) < 6 then begin
      n := (1+up)*2000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;


{ TItemUnit.UpgradeRandomNecklace
  功能: 随机升级项链属性
  参数:
    pu - 用户物品指针
  实现原理: 随机生成项链的各项属性
    - Desc[0]: 准确
    - Desc[1]: 敏捷
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomNecklace (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 准确属性
   up := GetUpgrade (6, 30);
   if Random(60) = 0 then pu.Desc[0] := 1+up; // AC(HIT)

   // 敏捷属性
   up := GetUpgrade (6, 30);
   if Random(60) = 0 then pu.Desc[1] := 1+up; // MAC(SPEED)

   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 12);
   if Random(20) < 15 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;

{ TItemUnit.UpgradeRandomBarcelet
  功能: 随机升级手镯属性
  参数:
    pu - 用户物品指针
  实现原理: 随机生成手镯的各项属性
    - Desc[0]: AC（防御）
    - Desc[1]: MAC（魔防）
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomBarcelet (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 防御属性
   up := GetUpgrade (6, 20);
   if Random(20) = 0 then pu.Desc[0] := 1+up; // AC

   // 魔防属性
   up := GetUpgrade (6, 20);
   if Random(20) = 0 then pu.Desc[1] := 1+up; // MAC

   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 12);
   if Random(20) < 15 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;

{ TItemUnit.UpgradeRandomNecklace19
  功能: 随机升级19类项链属性（魔法回避/幸运类）
  参数:
    pu - 用户物品指针
  实现原理: 随机生成特殊项链的各项属性
    - Desc[0]: 魔法回避
    - Desc[1]: 幸运
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomNecklace19 (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 魔法回避
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[0] := 1+up;

   // 幸运
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[1] := 1+up;

   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 10);
   if Random(4) < 3 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;

{ TItemUnit.UpgradeRandomRings
  功能: 随机升级戒指属性
  参数:
    pu - 用户物品指针
  实现原理: 随机生成戒指的各项属性
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomRings (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 12);
   if Random(4) < 3 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;

{ TItemUnit.UpgradeRandomRings23
  功能: 随机升级23类戒指属性（中毒抵抗/恢复类）
  参数:
    pu - 用户物品指针
  实现原理: 随机生成特殊戒指的各项属性
    - Desc[0]: 中毒抵抗
    - Desc[1]: 中毒恢复
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomRings23 (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 中毒抵抗
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[0] := 1+up;

   // 中毒恢复
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[1] := 1+up;

   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 12);
   if Random(4) < 3 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;

{ TItemUnit.UpgradeRandomHelmet
  功能: 随机升级头盔属性
  参数:
    pu - 用户物品指针
  实现原理: 随机生成头盔的各项属性
    - Desc[0]: AC（防御）
    - Desc[1]: MAC（魔防）
    - Desc[2]: DC（破坏）
    - Desc[3]: MC（魔力）
    - Desc[4]: SC（道力） }
procedure TItemUnit.UpgradeRandomHelmet (pu: PTUserItem);
var
   i, n, up: integer;  // 升级值和临时变量
begin
   // 防御属性
   up := GetUpgrade (6, 20);
   if Random(40) = 0 then pu.Desc[0] := 1+up; // AC

   // 魔防属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[1] := 1+up; // MAC

   // 破坏属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[2] := 1+up; // DC

   // 魔力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[3] := 1+up; // MC

   // 道力属性
   up := GetUpgrade (6, 20);
   if Random(30) = 0 then pu.Desc[4] := 1+up; // SC

   // 耐久度
   up := GetUpgrade (6, 12);
   if Random(4) < 3 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;
end;

{ ========================================================================
  未知属性装备生成
  ======================================================================== }

{ TItemUnit.RandomSetUnknownHelmet
  功能: 设置未知属性头盔
  参数:
    pu - 用户物品指针
  实现原理:
    1. 随机生成各项属性（多次叠加获取更高属性）
    2. 设置不掉落属性（1/30概率）
    3. 设置未知属性标记
    4. 根据属性总和设置装备需求 }
procedure TItemUnit.RandomSetUnknownHelmet (pu: PTUserItem);
var
   i, n, up, sum: integer;  // 升级值和属性总和
begin
   // 防御属性（多次叠加）
   up := GetUpgrade (4, 3) + GetUpgrade (4, 8) + GetUpgrade (4, 20);
   if up > 0 then pu.Desc[0] := up; // AC
   sum := up;

   // 魔防属性（多次叠加）
   up := GetUpgrade (4, 3) + GetUpgrade (4, 8) + GetUpgrade (4, 20);
   if up > 0 then pu.Desc[1] := up; // MAC
   sum := sum + up;

   // 破坏属性
   up := GetUpgrade (3, 15) + GetUpgrade (3, 30);
   if up > 0 then pu.Desc[2] := up; // DC
   sum := sum + up;

   // 魔力属性
   up := GetUpgrade (3, 15) + GetUpgrade (3, 30);
   if up > 0 then pu.Desc[3] := up; // MC
   sum := sum + up;

   // 道力属性
   up := GetUpgrade (3, 15) + GetUpgrade (3, 30);
   if up > 0 then pu.Desc[4] := up; // SC
   sum := sum + up;

   // 耐久度
   up := GetUpgrade (6, 30);
   if up > 0 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;

   // 不掉落属性（1/30概率）
   if Random(30) = 0 then
      pu.Desc[7] := 1;  // 不掉落属性
   pu.Desc[8] := 1;  // 未知属性标记

   // 根据属性总和设置装备需求
   if sum >= 3 then begin
      if (pu.Desc[0] >= 5) then begin // 防御较高
         pu.Desc[5] := 1; // 需求破坏
         pu.Desc[6] := 25 + pu.Desc[0] * 3;
         exit;
      end;
      if (pu.Desc[2] >= 2) then begin // 破坏较高
         pu.Desc[5] := 1; // 需求破坏
         pu.Desc[6] := 35 + pu.Desc[2] * 4;
         exit;
      end;
      if (pu.Desc[3] >= 2) then begin // 魔力较高
         pu.Desc[5] := 2; // 需求魔力
         pu.Desc[6] := 18 + pu.Desc[3] * 2;
         exit;
      end;
      if (pu.Desc[4] >= 2) then begin // 道力较高
         pu.Desc[5] := 3; // 需求道力
         pu.Desc[6] := 18 + pu.Desc[4] * 2;
         exit;
      end;
      pu.Desc[6] := 18 + sum * 2;
   end;
end;

{ TItemUnit.RandomSetUnknownRing
  功能: 设置未知属性戒指
  参数:
    pu - 用户物品指针
  实现原理:
    1. 随机生成攻击属性（DC/MC/SC）
    2. 设置不掉落属性和未知属性标记
    3. 根据属性总和设置装备需求 }
procedure TItemUnit.RandomSetUnknownRing (pu: PTUserItem);
var
   i, n, up, sum: integer;  // 升级值和属性总和
begin
   // 破坏属性（多次叠加）
   up := GetUpgrade (3, 4) + GetUpgrade (3, 8) + GetUpgrade (6, 20);
   if up > 0 then pu.Desc[2] := up; // DC
   sum := up;

   // 魔力属性（多次叠加）
   up := GetUpgrade (3, 4) + GetUpgrade (3, 8) + GetUpgrade (6, 20);
   if up > 0 then pu.Desc[3] := up; // MC
   sum := sum + up;

   // 道力属性（多次叠加）
   up := GetUpgrade (3, 4) + GetUpgrade (3, 8) + GetUpgrade (6, 20);
   if up > 0 then pu.Desc[4] := up; // SC
   sum := sum + up;

   // 耐久度
   up := GetUpgrade (6, 30);
   if up > 0 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;

   // 不掉落属性（1/30概率）
   if Random(30) = 0 then
      pu.Desc[7] := 1;  // 不掉落属性
   pu.Desc[8] := 1;  // 未知属性标记

   // 根据属性总和设置装备需求
   if sum >= 3 then begin
      if (pu.Desc[2] >= 3) then begin // 破坏较高
         pu.Desc[5] := 1; // 需求破坏
         pu.Desc[6] := 25 + pu.Desc[2] * 3;
         exit;
      end;
      if (pu.Desc[3] >= 3) then begin // 魔力较高
         pu.Desc[5] := 2; // 需求魔力
         pu.Desc[6] := 18 + pu.Desc[3] * 2;
         exit;
      end;
      if (pu.Desc[4] >= 3) then begin // 道力较高
         pu.Desc[5] := 3; // 需求道力
         pu.Desc[6] := 18 + pu.Desc[4] * 2;
         exit;
      end;
      pu.Desc[6] := 18 + sum * 2;
   end;
end;

{ TItemUnit.RandomSetUnknownBracelet
  功能: 设置未知属性手镯
  参数:
    pu - 用户物品指针
  实现原理:
    1. 随机生成防御和攻击属性
    2. 设置不掉落属性和未知属性标记
    3. 根据属性总和设置装备需求 }
procedure TItemUnit.RandomSetUnknownBracelet (pu: PTUserItem);
var
   i, n, up, sum: integer;  // 升级值和属性总和
begin
   // 防御属性
   up := GetUpgrade (3, 5) + GetUpgrade (5, 20);
   if up > 0 then pu.Desc[0] := up; // AC
   sum := up;

   // 魔防属性
   up := GetUpgrade (3, 5) + GetUpgrade (5, 20);
   if up > 0 then pu.Desc[1] := up; // MAC
   sum := sum + up;

   // 破坏属性
   up := GetUpgrade (3, 15) + GetUpgrade (5, 30);
   if up > 0 then pu.Desc[2] := up; // DC
   sum := sum + up;

   // 魔力属性
   up := GetUpgrade (3, 15) + GetUpgrade (5, 30);
   if up > 0 then pu.Desc[3] := up; // MC
   sum := sum + up;

   // 道力属性
   up := GetUpgrade (3, 15) + GetUpgrade (5, 30);
   if up > 0 then pu.Desc[4] := up; // SC
   sum := sum + up;

   // 耐久度
   up := GetUpgrade (6, 30);
   if up > 0 then begin
      n := (1+up)*1000;
      pu.DuraMax := _MIN(65000, integer(pu.DuraMax) + n);
      pu.Dura := _MIN(65000, integer(pu.Dura) + n);
   end;

   // 不掉落属性（1/30概率）
   if Random(30) = 0 then
      pu.Desc[7] := 1;  // 不掉落属性
   pu.Desc[8] := 1;  // 未知属性标记

   // 根据属性总和设置装备需求
   if sum >= 2 then begin
      if (pu.Desc[0] >= 3) then begin // 防御较高
         pu.Desc[5] := 1; // 需求破坏
         pu.Desc[6] := 25 + pu.Desc[0] * 3;
         exit;
      end;
      if (pu.Desc[2] >= 2) then begin // 破坏较高
         pu.Desc[5] := 1; // 需求破坏
         pu.Desc[6] := 30 + pu.Desc[2] * 3;
         exit;
      end;
      if (pu.Desc[3] >= 2) then begin // 魔力较高
         pu.Desc[5] := 2; // 需求魔力
         pu.Desc[6] := 20 + pu.Desc[3] * 2;
         exit;
      end;
      if (pu.Desc[4] >= 2) then begin // 道力较高
         pu.Desc[5] := 3; // 需求道力
         pu.Desc[6] := 20 + pu.Desc[4] * 2;
         exit;
      end;
      pu.Desc[6] := 18 + sum * 2;
   end;
end;


{ TItemUnit.GetUpgradeStdItem
  功能: 获取升级后的标准物品属性
  参数:
    ui - 用户物品（包含升级属性）
    std - 标准物品（输出参数，将被修改）
  实现原理: 根据物品类型将用户物品的升级属性应用到标准物品
    - StdMode 5,6: 武器
    - StdMode 10,11: 衣服
    - StdMode 15,19-26: 头盔、项链、戒指、手镯等 }
procedure TItemUnit.GetUpgradeStdItem (ui: TUserItem; var std: TStdItem);
var
   pw: PWord;  // 字指针（未使用）
begin
   case std.StdMode of
      5,6: // 武器
      begin
         // 将升级属性加到标准物品的高位字节
         std.DC := MakeWord (Lobyte(std.DC), Hibyte(std.DC) + ui.Desc[0]);   // 破坏
         std.MC := MakeWord (Lobyte(std.MC), Hibyte(std.MC) + ui.Desc[1]);   // 魔力
         std.SC := MakeWord (Lobyte(std.SC), Hibyte(std.SC) + ui.Desc[2]);   // 道力（3:幸运, 4:诅咒）
         std.Ac := MakeWord (Lobyte(std.AC) + ui.Desc[3], Hibyte(std.AC) + ui.Desc[5]);  // 准确
         std.Mac:= MakeWord (Lobyte(std.MAC) + ui.Desc[4], Hibyte(std.MAC) + ui.Desc[6]);  // 攻击速度(-/+)
         // 武器强度
         if ui.Desc[7] in [1..10] then
            std.SpecialPwr := ui.Desc[7];
         // 特殊描述标记
         if ui.Desc[10] <> 0 then
            std.ItemDesc := std.ItemDesc or $01;
      end;
      10,11: // 衣服
      begin
         std.AC := MakeWord (Lobyte(std.AC), Hibyte(std.AC) + ui.Desc[0]);   // 防御
         std.MAC := MakeWord (Lobyte(std.MAC), Hibyte(std.MAC) + ui.Desc[1]); // 魔防
         std.DC  := MakeWord (Lobyte(std.DC), Hibyte(std.DC) + ui.Desc[2]);  // 破坏
         std.MC  := MakeWord (Lobyte(std.MC), Hibyte(std.MC) + ui.Desc[3]);  // 魔力
         std.SC  := MakeWord (Lobyte(std.SC), Hibyte(std.SC) + ui.Desc[4]);  // 道力
      end;
      15, 19,20,21,22,23,24,26: // 头盔、项链、戒指、手镯
      begin
         std.AC  := MakeWord (Lobyte(std.AC), Hibyte(std.AC) + ui.Desc[0]);   // 防御
         std.MAC := MakeWord (Lobyte(std.MAC),Hibyte(std.MAC)+ ui.Desc[1]);   // 魔防
         std.DC  := MakeWord (Lobyte(std.DC), Hibyte(std.DC) + ui.Desc[2]);   // 破坏
         std.MC  := MakeWord (Lobyte(std.MC), Hibyte(std.MC) + ui.Desc[3]);   // 魔力
         std.SC  := MakeWord (Lobyte(std.SC), Hibyte(std.SC) + ui.Desc[4]);   // 道力
         // 装备需求类型（等级、破坏、魔力、道力）
         if ui.Desc[5] > 0 then
            std.Need := ui.Desc[5];
         // 装备需求值
         if ui.Desc[6] > 0 then
            std.NeedLevel := ui.Desc[6];
         // 注：不掉落属性和未知属性标记不需要在此处理
      end;
   end;
end;


end.
