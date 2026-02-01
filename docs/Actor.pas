{ ============================================================================
  单元名称: Actor
  功能描述: 游戏角色(Actor)基础类单元
  
  主要功能:
  - 定义角色动作帧信息结构(TActionInfo)
  - 定义人类角色动作结构(THumanAction)
  - 定义怪物角色动作结构(TMonsterAction)
  - 实现基础角色类TActor及其派生类TNpcActor、THumActor
  - 处理角色的移动、攻击、施法、受击、死亡等动作
  - 管理角色的图像资源加载和绘制
  - 处理角色的声音效果
  
  作者: 传奇开发团队
  创建日期: 2003
============================================================================ }
unit Actor;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Forms, Dialogs,
  Grobal2, CliUtil, magiceff, GameImages, ClFunc,Textures;

const
   MAXACTORSOUND = 3;       // 角色最大声音数量
   CMMX     = 150;          // 角色消息X坐标偏移
   CMMY     = 200;          // 角色消息Y坐标偏移

   HUMANFRAME = 600;        // 人类角色每套动作的帧数
   MONFRAME  = 280;         // 普通怪物每套动作的帧数
   EXPMONFRAME = 360;       // 扩展怪物每套动作的帧数(10帧基础)
   SCULMONFRAME = 440;      // 雕塑类怪物每套动作的帧数
   ZOMBIFRAME = 430;        // 僵尸类怪物每套动作的帧数
   MERCHANTFRAME = 60;      // 商人NPC每套动作的帧数
   MAXSAY = 5;              // 角色说话最大行数
//   MON1_FRAME =
//   MON2_FRAME =

   RUN_MINHEALTH = 10;      // 奔跑所需最低体力值
   DEFSPELLFRAME = 10;      // 默认施法帧数
  //FIREHIT_READYFRAME = 6;  // 烈火剑法施展帧数
   MAGBUBBLEBASE = 3890;    // 魔法护盾基础图像索引
   MAGBUBBLESTRUCKBASE = 3900;  // 魔法护盾受击图像基础索引
   MAXWPEFFECTFRAME = 5;    // 武器特效最大帧数
   WPEFFECTBASE = 3750;     // 武器特效基础图像索引
   EFFECTBASE = 0;          // 特效基础索引


type
   { TActionInfo记录
     功能: 定义单个动作的帧信息
     用途: 存储动作的起始帧、帧数、跳过帧数、帧时间等信息 }
   TActionInfo = record
      start   : word;              // 起始帧索引
      frame   : word;              // 动作帧数量
      skip    : word;              // 跳过的帧数(用于对齐)
      ftime   : word;              // 每帧持续时间(毫秒)
      usetick : byte;              // 使用的tick数，仅用于移动动作
   end;
   PTActionInfo = ^TActionInfo;    // TActionInfo指针类型

   { THumanAction记录
     功能: 定义人类角色的所有动作帧信息
     用途: 存储玩家角色的站立、行走、奔跑、攻击、施法等动作数据 }
   THumanAction = record
      ActStand:      TActionInfo;   // 站立动作(1方向)
      ActWalk:       TActionInfo;   // 行走动作(8方向)
      ActRun:        TActionInfo;   // 奔跑动作(8方向)
      ActRushLeft:   TActionInfo;   // 左冲刺动作
      ActRushRight:  TActionInfo;   // 右冲刺动作
      ActWarMode:    TActionInfo;   // 战斗姿态(1方向)
      ActHit:        TActionInfo;   // 普通攻击动作(6帧)
      ActHeavyHit:   TActionInfo;   // 重击动作(6帧)
      ActBigHit:     TActionInfo;   // 大招动作(6帧)
      ActFireHitReady: TActionInfo; // 烈火准备动作(6帧)
      ActSpell:      TActionInfo;   // 施法动作(6帧)
      ActSitdown:    TActionInfo;   // 坐下动作(1方向)
      ActStruck:     TActionInfo;   // 受击动作(3帧)
      ActDie:        TActionInfo;   // 死亡动作(4帧)
   end;
   PTHumanAction = ^THumanAction;   // THumanAction指针类型

   { TMonsterAction记录
     功能: 定义怪物角色的所有动作帧信息
     用途: 存储怪物的站立、行走、攻击、受击、死亡等动作数据 }
   TMonsterAction = record
      ActStand:      TActionInfo;   // 站立动作
      ActWalk:       TActionInfo;   // 行走动作(8方向)
      ActAttack:     TActionInfo;   // 普通攻击动作(6帧)
      ActCritical:   TActionInfo;   // 暴击/特殊攻击动作(6帧)
      ActStruck:     TActionInfo;   // 受击动作(3帧)
      ActDie:        TActionInfo;   // 死亡动作(4帧)
      ActDeath:      TActionInfo;   // 尸体/骷髅状态动作
   end;
   PTMonsterAction = ^TMonsterAction;  // TMonsterAction指针类型

const
   { HA: 人类角色动作帧配置常量
     功能: 定义玩家角色的所有动作帧参数
     注意: 每个动作包含8个方向，每方向占用(frame+skip)帧 }
   HA: THumanAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 4;  ftime: 200;  usetick: 0);   // 站立: 从第0帧开始，4帧动画
        ActWalk:   (start: 64;     frame: 6;  skip: 2;  ftime: 90;   usetick: 2);   // 行走: 从第64帧开始，6帧动画
        ActRun:    (start: 128;    frame: 6;  skip: 2;  ftime: 120;  usetick: 3);   // 奔跑: 从第128帧开始
        ActRushLeft: (start: 128;    frame: 3;  skip: 5;  ftime: 120;  usetick: 3); // 左冲刺
        ActRushRight:(start: 131;    frame: 3;  skip: 5;  ftime: 120;  usetick: 3); // 右冲刺
        ActWarMode:(start: 192;    frame: 1;  skip: 0;  ftime: 200;  usetick: 0);   // 战斗姿态
        //ActHit:    (start: 200;    frame: 5;  skip: 3;  ftime: 140;  usetick: 0);
        ActHit:    (start: 200;    frame: 6;  skip: 2;  ftime: 85;   usetick: 0);   // 普通攻击
        ActHeavyHit:(start: 264;   frame: 6;  skip: 2;  ftime: 90;   usetick: 0);   // 重击(挖矿)
        ActBigHit: (start: 328;    frame: 8;  skip: 0;  ftime: 70;   usetick: 0);   // 大招攻击
        ActFireHitReady: (start: 192; frame: 6;  skip: 4;  ftime: 70;   usetick: 0); // 烈火准备
        ActSpell:  (start: 392;    frame: 6;  skip: 2;  ftime: 60;   usetick: 0);   // 施法动作
        ActSitdown:(start: 456;    frame: 2;  skip: 0;  ftime: 300;  usetick: 0);   // 坐下
        ActStruck: (start: 472;    frame: 3;  skip: 5;  ftime: 70;  usetick: 0);    // 受击
        ActDie:    (start: 536;    frame: 4;  skip: 4;  ftime: 120;  usetick: 0)    // 死亡
      );

   { MA9: 足球怪物动作配置 }
   MA9: TMonsterAction = (
        ActStand:  (start: 0;      frame: 1;  skip: 7;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 64;     frame: 6;  skip: 2;  ftime: 120;  usetick: 3);   // 行走
        ActAttack: (start: 64;     frame: 6;  skip: 2;  ftime: 150;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 64;     frame: 6;  skip: 2;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 0;      frame: 1;  skip: 7;  ftime: 140;  usetick: 0);   // 死亡
        ActDeath:  (start: 0;      frame: 1;  skip: 7;  ftime: 0;    usetick: 0);   // 尸体
      );
   { MA10: 鸡/狗类怪物动作配置(8帧基础) }
   MA10: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 4;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 64;     frame: 6;  skip: 2;  ftime: 120;  usetick: 3);   // 行走
        ActAttack: (start: 128;    frame: 4;  skip: 4;  ftime: 150;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 192;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 208;    frame: 4;  skip: 4;  ftime: 140;  usetick: 0);   // 死亡
        ActDeath:  (start: 272;    frame: 1;  skip: 0;  ftime: 0;    usetick: 0);   // 尸体
      );
   { MA11: 鹿类怪物动作配置(10帧基础) }
   MA11: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 120;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 140;  usetick: 0);   // 死亡
        ActDeath:  (start: 340;    frame: 1;  skip: 0;  ftime: 0;    usetick: 0);   // 尸体
      );
   { MA12: 警卫兵类怪物动作配置(攻击速度较快) }
   MA12: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 4;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 64;     frame: 6;  skip: 2;  ftime: 120;  usetick: 3);   // 行走
        ActAttack: (start: 128;    frame: 6;  skip: 2;  ftime: 150;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 192;    frame: 2;  skip: 0;  ftime: 150;  usetick: 0);   // 受击
        ActDie:    (start: 208;    frame: 4;  skip: 4;  ftime: 160;  usetick: 0);   // 死亡
        ActDeath:  (start: 272;    frame: 1;  skip: 0;  ftime: 0;    usetick: 0);   // 尸体
      );
   { MA13: 食人花怪物动作配置 }
   MA13: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 10;     frame: 8;  skip: 2;  ftime: 160;  usetick: 0);   // 出场动作
        ActAttack: (start: 30;     frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 110;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 130;    frame: 10; skip: 0;  ftime: 120;  usetick: 0);   // 死亡
        ActDeath:  (start: 20;     frame: 9;  skip: 0;  ftime: 150;  usetick: 0);   // 隐藏动作
      );
   { MA14: 骷髅类怪物动作配置(骷髅武士/骷髅精灵等) }
   MA14: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 120;  usetick: 0);   // 死亡
        ActDeath:  (start: 340;    frame: 10; skip: 0;  ftime: 100;  usetick: 0);   // 白骨状态(召唤用)
      );
   { MA15: 投掷斧头的怪物动作配置 }
   MA15: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 120;  usetick: 0);   // 死亡
        ActDeath:  (start: 1;      frame: 1;  skip: 0;  ftime: 100;  usetick: 0);   // 尸体
      );
   { MA16: 喷毒气的怪物动作配置(蛆蛇) }
   MA16: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 160;  usetick: 0);   // 攻击(喷毒)
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 4;  skip: 6;  ftime: 160;  usetick: 0);   // 死亡
        ActDeath:  (start: 0;      frame: 1;  skip: 0;  ftime: 160;  usetick: 0);   // 尸体
      );
   { MA17: 抖动类怪物动作配置(站立动作较快) }
   MA17: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 60;  usetick: 0);    // 站立(快速抖动)
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 100;  usetick: 0);   // 死亡
        ActDeath:  (start: 340;    frame: 1;  skip: 0;  ftime: 140;  usetick: 0);   // 尸体
      );
   { MA19: 牛面鬼类怪物动作配置(死亡动作较快) }
   MA19: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 140;  usetick: 0);   // 死亡
        ActDeath:  (start: 340;    frame: 1;  skip: 0;  ftime: 140;  usetick: 0);   // 尸体
      );
   { MA20: 僵尸类怪物动作配置(可复活) }
   MA20: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 100;  usetick: 0);   // 死亡
        ActDeath:  (start: 340;    frame: 10; skip: 0;  ftime: 170;  usetick: 0);   // 复活动作
      );
   { MA21: 蜂巢怪物动作配置(不能移动) }
   MA21: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 0;      frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 行走(无)
        ActAttack: (start: 10;     frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击(发射蜂群)
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 20;     frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 30;     frame: 10; skip: 0;  ftime: 160;  usetick: 0);   // 死亡
        ActDeath:  (start: 0;      frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 尸体(无)
      );
   { MA22: 石像怪物动作配置(羊大将/羊将军) }
   MA22: TMonsterAction = (
        ActStand:  (start: 80;     frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 160;    frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 240;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActStruck: (start: 320;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 340;    frame: 10; skip: 0;  ftime: 160;  usetick: 0);   // 死亡
        ActDeath:  (start: 0;      frame: 6;  skip: 4;  ftime: 170;  usetick: 0);   // 石化动作
      );
   { MA23: 祝马王怪物动作配置 }
   MA23: TMonsterAction = (
        ActStand:  (start: 20;     frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 100;    frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 180;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 普通攻击
        ActCritical:(start:240;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 特殊攻击
        ActStruck: (start: 320;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 340;    frame: 10; skip: 0;  ftime: 140;  usetick: 0);   // 死亡
        ActDeath:  (start: 420;    frame: 1;  skip: 0;  ftime: 140;  usetick: 0);   // 尸体
      );
   { MA25: 蜘蛛王/触龙神怪物动作配置 }
   MA25: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 70;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);   // 出场动作
        ActAttack: (start: 20;     frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 近身攻击
        ActCritical:(start: 10;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 毒刺攻击(远程)
        ActStruck: (start: 50;     frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 60;     frame: 10; skip: 0;  ftime: 200;  usetick: 0);   // 死亡
        ActDeath:  (start: 80;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);   // 尸体
      );
   { MA26: 城门动作配置 }
   MA26: TMonsterAction = (
        ActStand:  (start: 0;      frame: 1;  skip: 7;  ftime: 200;  usetick: 0);   // 站立(关闭状态)
        ActWalk:   (start: 0;      frame: 0;  skip: 0;  ftime: 160;  usetick: 0);   // 行走(无)
        ActAttack: (start: 56;     frame: 6;  skip: 2;  ftime: 500;  usetick: 0);   // 开门动作
        ActCritical:(start: 64;    frame: 6;  skip: 2;  ftime: 500;  usetick: 0);   // 关门动作
        ActStruck: (start: 0;      frame: 4;  skip: 4;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 24;     frame: 10; skip: 0;  ftime: 120;  usetick: 0);   // 死亡(破坏)
        ActDeath:  (start: 0;      frame: 0;  skip: 0;  ftime: 150;  usetick: 0);   // 尸体(无)
      );
   { MA27: 城墙动作配置 }
   MA27: TMonsterAction = (
        ActStand:  (start: 0;     frame: 1;  skip: 7;  ftime: 200;  usetick: 0);    // 站立
        ActWalk:   (start: 0;     frame: 0;  skip: 0;  ftime: 160;  usetick: 0);    // 行走(无)
        ActAttack: (start: 0;     frame: 0;  skip: 0;  ftime: 250;  usetick: 0);    // 攻击(无)
        ActCritical:(start: 0;    frame: 0;  skip: 0;  ftime: 250;  usetick: 0);    // 暴击(无)
        ActStruck: (start: 0;     frame: 0;  skip: 0;  ftime: 100;  usetick: 0);    // 受击(无)
        ActDie:    (start: 0;     frame: 10; skip: 0;  ftime: 120;  usetick: 0);    // 死亡(破坏)
        ActDeath:  (start: 0;     frame: 0;  skip: 0;  ftime: 150;  usetick: 0);    // 尸体(无)
      );
   { MA28: 神兽怪物动作配置(变身前) }
   MA28: TMonsterAction = (
        ActStand:  (start: 80;     frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 160;    frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start:  0;     frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 120;  usetick: 0);   // 死亡
        ActDeath:  (start:  0;     frame: 10; skip: 0;  ftime: 100;  usetick: 0);   // 出场动作
      );
   { MA29: 神兽怪物动作配置(变身后) }
   MA29: TMonsterAction = (
        ActStand:  (start: 80;     frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 160;    frame: 6;  skip: 4;  ftime: 160;  usetick: 3);   // 行走
        ActAttack: (start: 240;    frame: 6;  skip: 4;  ftime: 100;  usetick: 0);   // 攻击
        ActCritical:(start: 0;     frame: 10; skip: 0;  ftime: 100;  usetick: 0);   // 特殊攻击
        ActStruck: (start: 320;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 340;    frame: 10; skip: 0;  ftime: 120;  usetick: 0);   // 死亡
        ActDeath:  (start:  0;     frame: 10; skip: 0;  ftime: 100;  usetick: 0);   // 出场动作
      );

   { MA30: 血巨人王/心脏/赤月魔怪物动作配置 }
   MA30: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 0;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);    // 行走
        ActAttack: (start: 10;     frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击
        ActCritical:(start: 10;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 特殊攻击
        ActStruck: (start: 20;     frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 30;     frame: 20; skip: 0;  ftime: 150;  usetick: 0);   // 死亡
        ActDeath:  (start: 0;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);    // 尸体
      );
   { MA31: 爆眼蜘蛛怪物动作配置 }
   MA31: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 0;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);    // 行走
        ActAttack: (start: 10;     frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击
        ActCritical:(start: 0;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);    // 特殊攻击
        ActStruck: (start: 0;     frame: 2;  skip: 8;  ftime: 100;  usetick: 0);    // 受击
        ActDie:    (start: 20;     frame: 10; skip: 0;  ftime: 200;  usetick: 0);   // 死亡
        ActDeath:  (start: 0;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);    // 尸体
      );
   { MA32: 小蜘蛛(爆走)怪物动作配置 }
   MA32: TMonsterAction = (
        ActStand:  (start: 0;      frame: 1;  skip: 9;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 0;     frame: 6; skip: 4;  ftime: 200;  usetick: 3);     // 行走
        ActAttack: (start: 0;     frame: 6;  skip: 4;  ftime: 120;  usetick: 0);    // 攻击
        ActCritical:(start: 0;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);    // 特殊攻击
        ActStruck: (start: 0;     frame: 2;  skip: 8;  ftime: 100;  usetick: 0);    // 受击
        ActDie:    (start: 80;     frame: 10; skip: 0;  ftime: 80;  usetick: 0);    // 死亡(快速)
        ActDeath:  (start: 80;     frame: 10; skip: 0;  ftime: 200;  usetick: 3);   // 尸体
      );
   { MA33: 雷血蛇/祝马本王/王豚怪物动作配置 }
   MA33: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 200;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击
        ActCritical:(start: 340;   frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 特殊攻击
        ActStruck: (start: 240;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 260;    frame: 10; skip: 0;  ftime: 200;  usetick: 0);   // 死亡
        ActDeath:  (start: 260;    frame: 10; skip: 0;  ftime: 200;  usetick: 0);   // 尸体
      );
   // 2003/02/11 新增怪物
   { MA34: 骷髅半王怪物动作配置 }
   MA34: TMonsterAction = (
        ActStand:  (start: 0;      frame: 4;  skip: 6;  ftime: 200;  usetick: 0);   // 站立
        ActWalk:   (start: 80;     frame: 6;  skip: 4;  ftime: 200;  usetick: 3);   // 行走
        ActAttack: (start: 160;    frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 攻击
        ActCritical:(start: 320;   frame: 6;  skip: 4;  ftime: 120;  usetick: 0);   // 特殊攻击
        ActStruck: (start: 400;    frame: 2;  skip: 0;  ftime: 100;  usetick: 0);   // 受击
        ActDie:    (start: 420;    frame: 20; skip: 0;  ftime: 200;  usetick: 0);   // 死亡
        ActDeath:  (start: 420;    frame: 20; skip: 0;  ftime: 200;  usetick: 0);   // 尸体
      );
   { MA50: NPC基础动作配置 }
   MA50: TMonsterAction = (
        ActStand:  (start: 0;    frame: 4;  skip: 6;  ftime: 200;  usetick: 0);     // 站立
        ActWalk:   (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 行走(无)
        ActAttack: (start: 30;   frame: 10; skip: 0;  ftime: 150;  usetick: 0);     // 交互动作
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 0;    frame: 1;  skip: 9;  ftime: 0;  usetick: 0);       // 受击(无)
        ActDie:    (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 死亡(无)
        ActDeath:  (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 尸体(无)
      );
   { MA51: NPC扩展动作配置(20帧交互) }
   MA51: TMonsterAction = (
        ActStand:  (start: 0;    frame: 4;  skip: 6;  ftime: 200;  usetick: 0);     // 站立
        ActWalk:   (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 行走(无)
        ActAttack: (start: 30;   frame: 20; skip: 0;  ftime: 150;  usetick: 0);     // 交互动作(20帧)
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 0;    frame: 1;  skip: 9;  ftime: 0;  usetick: 0);       // 受击(无)
        ActDie:    (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 死亡(无)
        ActDeath:  (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 尸体(无)
      );
   { MA52: NPC特殊动作配置 }
   MA52: TMonsterAction = (
        ActStand:  (start: 30;    frame: 4;  skip: 6;  ftime: 200;  usetick: 0);    // 站立
        ActWalk:   (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 行走(无)
        ActAttack: (start: 30;   frame: 4; skip: 6;  ftime: 150;  usetick: 0);      // 交互动作
        ActCritical:(start: 0;     frame: 0;  skip: 0;  ftime: 0;    usetick: 0);   // 暴击(无)
        ActStruck: (start: 0;    frame: 1;  skip: 9;  ftime: 0;  usetick: 0);       // 受击(无)
        ActDie:    (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 死亡(无)
        ActDeath:  (start: 0;    frame: 0;  skip: 0;  ftime: 0;  usetick: 0);       // 尸体(无)
      );


   { WORDER: 武器绘制顺序数组
     功能: 定义每个帧中武器的绘制顺序
     参数: 1=武器在前(先绘制身体后绘制武器), 0=武器在后(先绘制武器后绘制身体)
     索引: [0]=男性, [1]=女性 }
   WORDER: Array[0..1, 0..599] of byte = (
      (       // 男性角色武器绘制顺序
      //정지
      0,0,0,0,0,0,0,0,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,0,0,0,0,0,    0,0,0,0,1,1,1,1,
      0,0,0,0,1,1,1,1,    0,0,0,0,1,1,1,1,
      //걷기
      0,0,0,0,0,0,0,0,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,0,0,0,0,0,    0,0,0,0,0,0,0,1,
      0,0,0,0,0,0,0,1,    0,0,0,0,0,0,0,1,
      //뛰기
      0,0,0,0,0,0,0,0,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,1,1,1,1,1,1,    0,0,1,1,1,0,0,1,
      0,0,0,0,0,0,0,1,    0,0,0,0,0,0,0,1,
      //war모드
      0,1,1,1,0,0,0,0,
      //공격
      1,1,1,0,0,0,1,1,    1,1,1,0,0,0,0,0,    1,1,1,0,0,0,0,0,
      1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,    1,1,1,0,0,0,0,0,
      0,0,0,0,0,0,0,0,    1,1,1,1,0,0,1,1,
      //공격 2
      0,1,1,0,0,0,1,1,    0,1,1,0,0,0,1,1,    1,1,1,0,0,0,0,0,
      1,1,1,0,0,1,1,1,    1,1,1,1,1,1,1,1,    0,1,1,1,1,1,1,1,
      0,0,0,1,1,1,0,0,    0,1,1,1,1,0,1,1,
      //공격3
      1,1,0,1,0,0,0,0,    1,1,0,0,0,0,0,0,    1,1,1,1,1,0,0,0,
      1,1,0,0,1,0,0,0,    1,1,1,0,0,0,0,1,    0,1,1,0,0,0,0,0,
      0,0,0,0,1,1,1,0,    1,1,1,1,1,0,0,0,
      //마법
      0,0,0,0,0,0,1,1,    0,0,0,0,0,0,1,1,    0,0,0,0,0,0,1,1,
      1,0,0,0,0,1,1,1,    1,1,1,1,1,1,1,1,    0,1,1,1,1,1,1,1,
      0,0,1,1,0,0,1,1,    0,0,0,1,0,0,1,1,
      //앚기
      0,0,1,0,1,1,1,1,    1,1,0,0,0,1,0,0,
      //맞기
      0,0,0,1,1,1,1,1,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1,
      0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1,
      //쓰러짐
      0,0,1,1,1,1,1,1,    0,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1,
      0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1
      ),

      (       // 女性角色武器绘制顺序
      // 站立
      0,0,0,0,0,0,0,0,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,0,0,0,0,0,    0,0,0,0,1,1,1,1,
      0,0,0,0,1,1,1,1,    0,0,0,0,1,1,1,1,
      //걷기
      0,0,0,0,0,0,0,0,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,0,0,0,0,0,    0,0,0,0,0,0,0,1,
      0,0,0,0,0,0,0,1,    0,0,0,0,0,0,0,1,
      //뛰기
      0,0,0,0,0,0,0,0,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,1,1,1,1,1,1,    0,0,1,1,1,0,0,1,
      0,0,0,0,0,0,0,1,    0,0,0,0,0,0,0,1,
      //war모드
      1,1,1,1,0,0,0,0,
      //공격
      1,1,1,0,0,0,1,1,    1,1,1,0,0,0,0,0,    1,1,1,0,0,0,0,0,
      1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,    1,1,1,0,0,0,0,0,
      0,0,0,0,0,0,0,0,    1,1,1,1,0,0,1,1,
      //공격 2
      0,1,1,0,0,0,1,1,    0,1,1,0,0,0,1,1,    1,1,1,0,0,0,0,0,
      1,1,1,0,0,1,1,1,    1,1,1,1,1,1,1,1,    0,1,1,1,1,1,1,1,
      0,0,0,1,1,1,0,0,    0,1,1,1,1,0,1,1,
      //공격3
      1,1,0,1,0,0,0,0,    1,1,0,0,0,0,0,0,    1,1,1,1,1,0,0,0,
      1,1,0,0,1,0,0,0,    1,1,1,0,0,0,0,1,    0,1,1,0,0,0,0,0,
      0,0,0,0,1,1,1,0,    1,1,1,1,1,0,0,0,
      //마법
      0,0,0,0,0,0,1,1,    0,0,0,0,0,0,1,1,    0,0,0,0,0,0,1,1,
      1,0,0,0,0,1,1,1,    1,1,1,1,1,1,1,1,    0,1,1,1,1,1,1,1,
      0,0,1,1,0,0,1,1,    0,0,0,1,0,0,1,1,
      //앚기
      0,0,1,0,1,1,1,1,    1,1,0,0,0,1,0,0,
      //맞기
      0,0,0,1,1,1,1,1,    1,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1,
      0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1,
      //쓰러짐
      0,0,1,1,1,1,1,1,    0,1,1,1,1,1,1,1,    1,1,1,1,1,1,1,1,
      1,1,1,1,1,1,1,1,    0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1,
      0,0,0,1,1,1,1,1,    0,0,0,1,1,1,1,1
      )
   );

   { EffDir: 特效方向数组
     功能: 定义哪些方向需要显示特效
     参数: 0=不显示特效, 1=显示特效 }
   EffDir : array[0..7] of byte = (0, 0, 1, 1, 1, 1, 1, 0);


type
   { TActor类
     功能: 游戏角色基础类，所有角色(玩家、怪物、NPC)的父类
     用途: 
       - 管理角色的基本属性(位置、方向、外观等)
       - 处理角色的动作帧播放
       - 管理角色的消息队列
       - 处理角色的移动、攻击、受击等动作
       - 管理角色的声音效果 }
   TActor = class
      RecogId: integer;       // 角色唯一识别ID
      XX:   word;             // 地图上X坐标
      YY:   word;             // 地图上Y坐标
      Dir:  byte;             // 面向方向(0-7共8个方向)
      Sex:  byte;             // 性别(0=男, 1=女)
      Race:    byte;          // 种族类型(0=人类, 50=NPC, 其他=怪物)
      Hair:    byte;          // 发型索引
      Dress:   byte;          // 衣服索引
      Weapon:  byte;          // 武器索引
      Job:  byte;             // 职业(0=战士, 1=法师, 2=道士)
      Appearance: word;       // 外观索引(怪物用)
      DeathState: byte;       // 死亡状态
      Feature: integer;       // 外观特征码(包含衣服、武器、发型等信息)
      State:   integer;       // 角色状态码(包含中毒、护盾等状态)
      Death: Boolean;         // 是否死亡
      Skeleton: Boolean;      // 是否为骷髅状态
      BoDelActor: Boolean;    // 是否需要删除此角色
      BoDelActionAfterFinished: Boolean;  // 动作完成后是否删除
      DescUserName: string;   // 角色描述名称
      UserName: string;       // 角色名称
      NameColor: integer;     // 名称颜色
      Abil: TAbility;         // 角色能力属性(生命、魔法、攻击等)
      Gold: integer;          // 金币数量
      HitSpeed: shortint;     // 攻击速度修正(0=默认, (-)减慢, (+)加快)
      Visible: Boolean;       // 是否可见
      BoHoldPlace: Boolean;   // 是否占据位置

      // 说话气泡相关属性
      Saying: array[0..MAXSAY-1] of string;    // 说话内容数组
      SayWidths: array[0..MAXSAY-1] of integer; // 每行说话的宽度
      SayTime: longword;      // 说话开始时间
      SayX, SayY: integer;    // 说话气泡位置
      SayLineCount: integer;  // 说话行数

      // 位置偏移相关属性
      ShiftX:  integer;       // X方向像素偏移(移动时的平滑过渡)
      ShiftY:  integer;       // Y方向像素偏移

      // 图像绘制位置
      px, hpx, wpx:   integer;  // 身体/头发/武器的X偏移
      py, hpy, wpy:   integer;  // 身体/头发/武器的Y偏移
      Rx, Ry: integer;        // 渲染位置坐标
      DownDrawLevel: integer; // 绘制层级偏移(几个单元格之前绘制)
      TargetX, TargetY: integer;  // 攻击目标位置(用于投掷攻击)
      TargetRecog: integer;   // 攻击目标ID
      HiterCode: integer;     // 攻击者ID
      MagicNum: integer;      // 魔法编号
      CurrentEvent: integer;  // 当前事件ID(服务器事件)
      BoDigFragment: Boolean; // 本次挖矿是否成功
      BoThrow: Boolean;       // 是否正在投掷

      // 图像资源偏移
      BodyOffset, HairOffset, WeaponOffset: integer;  // 身体/头发/武器图像偏移
      
      // 魔法和特效相关
      BoUseMagic: Boolean;    // 是否正在使用魔法
      BoHitEffect: Boolean;   // 是否显示攻击特效
      BoUseEffect: Boolean;   // 是否使用特效
      HitEffectNumber: integer;   // 攻击特效编号
      WaitMagicRequest: longword; // 等待魔法请求的时间
      WaitForRecogId: integer;    // 等待的角色ID(自己消失后显示该角色)
      WaitForFeature: integer;    // 等待角色的外观
      WaitForStatus: integer;     // 等待角色的状态
      //BoEatEffect: Boolean;  // 吃药特效
      //EatEffectFrame: integer;
      //EatEffectTime: longword;







      // 魔法施放相关
      CurEffFrame: integer;   // 当前特效帧
      SpellFrame: integer;    // 魔法施放帧数
      CurMagic: TUseMagicInfo; // 当前使用的魔法信息
      //GlimmingMode: Boolean;
      //CurGlimmer: integer;
      //MaxGlimmer: integer;
      //GlimmerTime: longword;
      GenAniCount: integer;   // 通用动画计数器
      
      // 血条显示相关
      BoOpenHealth: Boolean;  // 是否显示血条
      BoInstanceOpenHealth: Boolean;  // 是否立即显示血条
      OpenHealthStart: longword;  // 血条显示开始时间
      OpenHealthTime: integer;    // 血条显示持续时间

      //SRc: TRect;  // 屏幕矩形(鼠标基准的实际坐标)
      BodySurface: TTexture;  // 身体图像贴图

      // 动作状态相关
      Grouped: Boolean;       // 是否与我组队
      CurrentAction: integer; // 当前执行的动作
      ReverseFrame: Boolean;  // 是否反向播放帧
      WarMode: Boolean;       // 是否处于战斗姿态
      WarModeTime: longword;  // 进入战斗姿态的时间
      ChrLight: integer;      // 角色光照等级
      MagLight: integer;      // 魔法光照等级
      RushDir: integer;       // 冲刺方向(0=左, 1=右，交替使用)

      WalkFrameDelay: integer;    // 行走帧延迟(默认使用客户端值，服务器可覆盖)

      // 时间控制相关
      LockEndFrame: Boolean;  // 是否锁定在最后一帧
      LastStruckTime: longword;   // 上次受击时间
      SendQueryUserNameTime: longword;  // 发送查询用户名的时间
      DeleteTime: longword;   // 删除时间


      // 声音效果相关
      MagicStruckSound: integer;  // 魔法受击声音
      borunsound: Boolean;    // 是否播放声音
      footstepsound: integer; // 脚步声(主角行走/奔跑时)
      strucksound: integer;   // 受击声音
      struckweaponsound: integer;  // 武器受击声音

      appearsound: integer;   // 出场声音
      normalsound: integer;   // 普通声音
      attacksound: integer;   // 攻击声音
      weaponsound: integer;   // 武器挥舞声音
      screamsound: integer;   // 惨叫声音
      diesound: integer;      // 死亡声音
      die2sound: integer;     // 死亡声音2

      magicstartsound: integer;   // 魔法开始施放声音
      magicfiresound: integer;    // 魔法发射声音
      magicexplosionsound: integer;   // 魔法爆炸声音

   private
   protected
      // 动作帧控制变量
      startframe: integer;    // 动作起始帧
      endframe: integer;      // 动作结束帧
      currentframe: integer;  // 当前帧
      effectstart: integer;   // 特效起始帧
      effectframe: integer;   // 特效当前帧
      effectend: integer;     // 特效结束帧
      effectstarttime: longword;  // 特效开始时间
      effectframetime: longword;  // 特效帧时间
      frametime: longword;    // 每帧持续时间(毫秒)
      starttime: longword;    // 最近一帧的时间戳
      maxtick: integer;       // 最大tick数
      curtick: integer;       // 当前tick数
      movestep: integer;      // 移动步数
      msgmuch: Boolean;       // 消息队列是否过多
      struckframetime: longword;  // 受击帧时间
      currentdefframe: integer;   // 当前默认帧
      defframetime: longword; // 默认帧时间
      defframecount: integer; // 默认帧数量
      SkipTick: integer;      // 跳过的tick数(负重时用)
      smoothmovetime: longword;   // 平滑移动时间
      genanicounttime: longword;  // 通用动画计时
      loadsurfacetime: longword;  // 加载贴图时间

      // 位置记录变量
      oldx, oldy, olddir: integer;  // 旧位置和方向(移动失败时恢复用)
      actbeforex, actbeforey: integer;  // 动作前的坐标
      wpord: integer;         // 武器绘制顺序

      // 可重写的保护方法
      procedure CalcActorFrame; dynamic;      // 计算角色动作帧
      procedure DefaultMotion; dynamic;       // 默认动作(站立)
      function  GetDefaultFrame (wmode: Boolean): integer; dynamic;  // 获取默认帧
      procedure DrawEffSurface (dsurface, source: TTexture; ddx, ddy: integer; blend: Boolean; ceff: TColorEffect);  // 绘制特效表面
      procedure DrawWeaponGlimmer (dsurface: TTexture; ddx, ddy: integer);  // 绘制武器闪光效果
   public
      MsgList: TList;         // 消息列表(PTChrMsg指针列表)
      RealActionMsg: TChrMsg; // 实际执行的动作消息(FrmMain使用)

      constructor Create; dynamic;            // 构造函数
      destructor Destroy; override;           // 析构函数
      procedure  SendMsg (ident: word; x, y, cdir, feature, state: integer; str: string; sound: integer);  // 发送消息
      procedure  UpdateMsg (ident: word; x, y, cdir, feature, state: integer; str: string; sound: integer); // 更新消息
      procedure  CleanUserMsgs;               // 清除用户消息
      procedure  ProcMsg;                     // 处理消息
      procedure  ProcHurryMsg;                // 处理紧急消息
      function   IsIdle: Boolean;             // 是否空闲
      function   ActionFinished: Boolean;     // 动作是否完成
      function   CanWalk: Integer;            // 是否可以行走
      function   CanRun: Integer;             // 是否可以奔跑
      function   Strucked: Boolean;           // 是否正在受击
      procedure  Shift (dir, step, cur, max: integer);  // 计算位置偏移
      procedure  ReadyAction (msg: TChrMsg);  // 准备执行动作
      function   CharWidth: Integer;          // 获取角色宽度
      function   CharHeight: Integer;         // 获取角色高度
      function   CheckSelect (dx, dy: integer): Boolean;  // 检查是否被选中
      procedure  CleanCharMapSetting (x, y: integer);     // 清除角色地图设置
      procedure  Say (str: string);           // 说话
      procedure  SetSound; dynamic;           // 设置声音
      procedure  Run; dynamic;                // 运行动作循环
      procedure  RunSound; dynamic;           // 播放声音
      procedure  RunActSound (frame: integer); dynamic;   // 播放动作声音
      procedure  RunFrameAction (frame: integer); dynamic; // 执行帧动作(每帧的特殊处理)
      procedure  ActionEnded; dynamic;        // 动作结束处理
      function   Move (step: integer): Boolean;  // 移动处理
      procedure  MoveFail;                    // 移动失败处理
      function   CanCancelAction: Boolean;    // 是否可以取消动作
      procedure  CancelAction;                // 取消动作
      procedure  FeatureChanged; dynamic;     // 外观变化处理
      function   Light: integer; dynamic;     // 获取光照等级
      procedure  LoadSurface; dynamic;        // 加载图像资源
      function   GetDrawEffectValue: TColorEffect;  // 获取绘制特效值
      procedure  DrawChr (dsurface: TTexture; dx, dy: integer; blend: Boolean); dynamic;  // 绘制角色
      procedure  DrawEff (dsurface: TTexture; dx, dy: integer); dynamic;  // 绘制特效
   end;


   { TNpcActor类
     功能: NPC角色类，继承自TActor
     用途: 处理NPC的特殊动作和显示逻辑 }
   TNpcActor = class (TActor)
   private
   public
      procedure  Run; override;               // 运行动作循环
      procedure  CalcActorFrame; override;    // 计算动作帧
      function   GetDefaultFrame (wmode: Boolean): integer; override;  // 获取默认帧
      procedure  LoadSurface; override;       // 加载图像资源
   end;

   { THumActor类
     功能: 人类角色类，继承自TActor
     用途: 处理玩家角色的特殊动作、装备显示、技能特效等 }
   THumActor = class (TActor)
   private
      HairSurface: TTexture;      // 头发图像贴图
      WeaponSurface: TTexture;    // 武器图像贴图
      BoWeaponEffect: Boolean;    // 是否显示武器特效(精炼成功/破碎)
      CurWpEffect: integer;       // 当前武器特效帧
      CurBubbleStruck: integer;   // 当前护盾受击帧
      wpeffecttime: longword;     // 武器特效时间
      BoHideWeapon: Boolean;      // 是否隐藏武器
   protected
      procedure CalcActorFrame; override;     // 计算动作帧
      procedure DefaultMotion; override;      // 默认动作
      function  GetDefaultFrame (wmode: Boolean): integer; override;  // 获取默认帧
   public
      constructor Create; override;           // 构造函数
      destructor Destroy; override;           // 析构函数
      procedure  Run; override;               // 运行动作循环
      procedure  RunFrameAction (frame: integer); override;  // 执行帧动作
      function   Light: integer; override;    // 获取光照等级
      procedure  LoadSurface; override;       // 加载图像资源
      procedure  DoWeaponBreakEffect;         // 执行武器破碎特效
      procedure  DrawChr (dsurface: TTexture; dx, dy: integer; blend: Boolean); override;  // 绘制角色
   end;

   { RaceByPM
     功能: 根据种族和外观获取怪物动作配置
     参数: race - 种族类型, appr - 外观索引
     返回值: 怪物动作配置指针 }
   function RaceByPM (race, appr: integer): PTMonsterAction;
   
   { GetMonImg
     功能: 根据外观索引获取怪物图像资源
     参数: appr - 外观索引
     返回值: 怪物图像资源对象 }
   function GetMonImg (appr: integer): TGameImages;
   
   { GetOffset
     功能: 根据外观索引获取图像偏移量
     参数: appr - 外观索引
     返回值: 图像偏移量 }
   function GetOffset (appr: integer): integer;


implementation

uses
   ClMain, SoundUtil, clEvent, Path, FState;


{ RaceByPM
  功能: 根据种族和外观获取怪物动作配置
  参数: 
    race - 种族类型(RaceImg)
    appr - 外观索引
  返回值: 怪物动作配置指针
  实现原理: 
    1. 根据race值查找对应的怪物动作配置
    2. NPC(race=50)根据appr进一步区分动作类型 }
function RaceByPM (race, appr: integer): PTMonsterAction;
begin
   Result := nil;
   case race of   // 根据种族类型返回对应的动作配置
      9:       Result := @MA9;      // 足球
      10:      Result := @MA10;    // 鸡/狗类
      11:      Result := @MA11;    // 鹿类
      12, 24:  Result := @MA12;    // 警卫兵类
      13:      Result := @MA13;    // 食人花
      14, 17, 18, 23:  Result := @MA14;  // 骷髅类
      15, 22:  Result := @MA15;    // 投掷类
      16:      Result := @MA16;    // 喷毒类
      30, 31:  Result := @MA17;    // 抖动类
      19, 20, 21,
      37,                          // 毒蛜蚁
      40, 45, 52, 53:
               Result := @MA19;    // 牛面鬼类
      41, 42:  Result := @MA20;    // 僵尸类(可复活)
      43:      Result := @MA21;    // 蜂巢
      47:      Result := @MA22;    // 石像怪物
      48, 49:  Result := @MA23;    // 祝马王
      32:      Result := @MA24;    // 蝎子(两种攻击方式)
      33:      Result := @MA25;    // 蜘蛛王/触龙神
      34:      Result := @MA30;    // 血巨人王/心脏
      35:      Result := @MA31;    // 爆眼蜘蛛
      36:      Result := @MA32;    // 小蜘蛛(爆走)
      54:      Result := @MA28;    // 神兽(变身前)
      55:      Result := @MA29;    // 神兽(变身后)
      60,61,62:Result := @MA33;    // 雷血蛇/王豚/祝马本王
      // 2003/02/11 新增怪物
      63:      Result := @MA34;    // 骷髅半王
      64, 65, 66, 67, 68, 69:      // 浪人鬼/腐蚀鬼/骷髅武将/骷髅兵卒/骷髅武士/骷髅弓手
               Result := @MA19;
      // 2003/03/04 新增怪物
      70,71,72:Result := @MA33;    // 般若右使/般若左使/四大天王
      98:      Result := @MA27;    // 城墙
      99:      Result := @MA26;    // 城门

      50:  // NPC类型
         case appr of
            23:                    // 特殊NPC(20帧交互)
               begin
                  Result := @MA51;
               end;
            24, 25:                // 特殊NPC
               begin
                  Result := @MA52;
               end;
            else
               Result := @MA50;    // 普通NPC
         end;
   end;
end;

{ GetMonImg
  功能: 根据外观索引获取怪物图像资源
  参数: appr - 外观索引
  返回值: 怪物图像资源对象
  实现原理: 根据appr/10的值确定使用哪个怪物图像文件 }
function GetMonImg (appr: integer): TGameImages;
begin
   Result := WMon1Img;           // 默认使用第一个怪物图像文件
   case (appr div 10) of
      0: Result := WMon1Img;     // 怪物图像1
      1: Result := WMon2Img;     // 怪物图像2(食人花等)
      2: Result := WMon3Img;     // 怪物图像3
      3: Result := WMon4Img;     // 怪物图像4
      4: Result := WMon5Img;     // 怪物图像5
      5: Result := WMon6Img;     // 怪物图像6
      6: Result := WMon7Img;     // 怪物图像7
      7: Result := WMon8Img;     // 怪物图像8
      8: Result := WMon9Img;     // 怪物图像9
      9: Result := WMon10Img;    // 怪物图像10
      10: Result := WMon11Img;   // 怪物图像11
      11: Result := WMon12Img;   // 怪物图像12
      12: Result := WMon13Img;   // 怪物图像13
      13: Result := WMon14Img;   // 怪物图像14
      14: Result := WMon15Img;   // 怪物图像15
      15: Result := WMon16Img;   // 怪物图像16
      16: Result := WMon17Img;   // 怪物图像17
      17: Result := WMon18Img;   // 怪物图像18
      90: Result := WEffectImg;  // 特效图像(城门/城墙)
   end;
end;

{ GetOffset
  功能: 根据外观索引获取图像偏移量
  参数: appr - 外观索引
  返回值: 图像在资源文件中的偏移量
  实现原理: 
    1. 将appr分解为nrace(十位)和npos(个位)
    2. 根据nrace确定每个怪物的帧数基数
    3. 计算具体偏移量 }
function GetOffset (appr: integer): integer;
var
   nrace, npos: integer;
begin
   Result := 0;
   nrace := appr div 10;          // 种族索引(十位)
   npos := appr mod 10;           // 位置索引(个位)
   case nrace of
      0:    Result := npos * 280;  // 8帧基础怪物
      1:    Result := npos * 230;  // 食人花类
      2, 3, 7..12, 14..16:    Result := npos * 360;  // 10帧基础怪物

      13:   case npos of           // 特殊怪物组
               1: Result := 360;   // 赤月魔(心脏)
               2: Result := 440;   // 爆眼蜘蛛(母蜘蛛)
               3: Result := 550;   // 小蜘蛛(爆走)
               else Result := npos * 360;
            end;

      4:    begin
               Result := npos * 360;
               if npos = 1 then Result := 600;  // 飞膜原虫
            end;
      5:    Result := npos * 430;   // 僵尸类
      6:    Result := npos * 440;   // 祝马神将/护法/王
      17:   Result := npos * 350;   // 神兽
      18:   case npos of
               0: Result := 0;      // 雷血蛇
               1: Result := 520;    // 王豚
               2: Result := 950;    // 祝马本王
            end;
      // 2003/02/11 新增怪物
      19:   case npos of
               0: Result := 0;      // 浪人鬼
               1: Result := 370;    // 腐蚀鬼
               2: Result := 810;    // 骷髅武将
               3: Result := 1250;   // 骷髅兵卒
               4: Result := 1630;   // 骷髅武士
               5: Result := 2010;   // 骷髅弓手
               6: Result := 2390;   // 骷髅半王
            end;
      // 2003/03/04 新增怪物
      20:   case npos of
               0: Result := 0;      // 般若鬼卒
               1: Result := 360;    // 般若冰鬼
               2: Result := 720;    // 般若云鬼
               3: Result := 1080;   // 般若风鬼
               4: Result := 1440;   // 般若火鬼
               5: Result := 1800;   // 般若右使
               6: Result := 2350;   // 般若左使
               7: Result := 3060;   // 四大天王
            end;

      90:   case npos of            // 城门/城墙
               0: Result := 80;     // 城门
               1: Result := 168;
               2: Result := 184;
               3: Result := 200;
            end;
   end;
end;

{ GetNpcOffset
  功能: 根据NPC外观索引获取图像偏移量
  参数: appr - NPC外观索引
  返回值: 图像在资源文件中的偏移量
  实现原理: 根据appr计算NPC图像的偏移位置 }
function GetNpcOffset (appr: integer): integer;
begin
   case appr of
      0..22:                       // 普通NPC(0-22号)
         Result := MERCHANTFRAME * appr;
      23:                          // 特殊NPC(23号)
         Result := 1380;
      //24,25:
      //   Result :=
      else                         // 其他NPC
         Result := 1470 + MERCHANTFRAME * (appr - 24);
   end;
end;


{ TActor.Create
  功能: TActor类的构造函数
  实现原理: 初始化角色的所有属性为默认值 }
constructor TActor.Create;
begin
   inherited Create;
   MsgList := TList.Create;       // 创建消息列表
   RecogId := 0;
   BodySurface := nil;
   FillChar (Abil, sizeof(TAbility), 0);  // 清空能力属性
   Gold := 0;
   Visible := TRUE;               // 默认可见
   BoHoldPlace := TRUE;           // 默认占据位置

   // 当前执行的动作，即使结束也保留
   // 当currentframe超过endframe时，认为动作已完成
   CurrentAction := 0;
   ReverseFrame := FALSE;
   ShiftX := 0;
   ShiftY := 0;
   DownDrawLevel := 0;
   currentframe := -1;            // -1表示无当前帧
   effectframe := -1;
   RealActionMsg.Ident := 0;
   UserName := '';
   NameColor := clWhite;          // 默认白色名称
   SendQueryUserNameTime := 0;

   WarMode := FALSE;              // 默认非战斗姿态
   WarModeTime := 0;              // 进入战斗姿态的时间
   Death := FALSE;                // 默认未死亡
   Skeleton := FALSE;             // 默认非骷髅状态
   BoDelActor := FALSE;           // 默认不删除
   BoDelActionAfterFinished := FALSE;
   
   ChrLight := 0;                 // 默认无光照
   MagLight := 0;
   LockEndFrame := FALSE;
   smoothmovetime := 0;
   genanicounttime := 0;
   defframetime := 0;
   loadsurfacetime := GetTickCount;
   Grouped := FALSE;              // 默认未组队
   BoOpenHealth := FALSE;         // 默认不显示血条
   BoInstanceOpenHealth := FALSE;

   CurMagic.ServerMagicCode := 0;
   //CurMagic.MagicSerial := 0;
   
   SpellFrame := DEFSPELLFRAME;   // 默认施法帧数

   // 初始化声音效果为-1(无声音)
   normalsound := -1;
   footstepsound := -1;           // 脚步声(主角行走/奔跑时)
   attacksound := -1;
   weaponsound := -1;
   strucksound := s_struck_body_longstick;  // 受击声音
   struckweaponsound := -1;
   screamsound := -1;
   diesound := -1;                // 死亡声音
   die2sound := -1;
end;

{ TActor.Destroy
  功能: TActor类的析构函数
  实现原理: 释放消息列表并调用父类析构 }
destructor TActor.Destroy;
begin
   MsgList.Free;                  // 释放消息列表
   inherited Destroy;
end;

{ TActor.SendMsg
  功能: 向角色发送消息
  参数: 
    ident - 消息标识
    x, y - 坐标
    cdir - 方向
    feature - 外观特征
    state - 状态
    str - 字符串参数
    sound - 声音索引
  实现原理: 创建消息结构并添加到消息列表 }
procedure TActor.SendMsg (ident: word; x, y, cdir, feature, state: integer; str: string; sound: integer);
var
   pmsg: PTChrMsg;
begin
   new (pmsg);                    // 分配消息内存
   pmsg.ident  := ident;
   pmsg.x      := x;
   pmsg.y      := y;
   pmsg.dir    := cdir;
   pmsg.feature:= feature;
   pmsg.state  := state;
   pmsg.saying := str;
   pmsg.Sound := sound;
   MsgList.Add (pmsg);            // 添加到消息列表
end;

{ TActor.UpdateMsg
  功能: 更新角色消息(删除重复消息后添加新消息)
  参数: 同SendMsg
  实现原理: 
    1. 如果是主角，删除客户端发送的消息(3000-3099)和相同消息
    2. 如果是其他角色，删除相同类型的消息
    3. 添加新消息 }
procedure TActor.UpdateMsg (ident: word; x, y, cdir, feature, state: integer; str: string; sound: integer);
var
   i, n: integer;
   pmsg: PTChrMsg;
begin
   if self = Myself then begin    // 如果是主角
      n := 0;
      while TRUE do begin
         if n >= MsgList.Count then break;
         // 删除客户端发送的消息(3000-3099)或相同类型的消息
         if (PTChrMsg (MsgList[n]).Ident >= 3000) and
            (PTChrMsg (MsgList[n]).Ident <= 3099) or
            (PTChrMsg (MsgList[n]).Ident = ident)
         then begin
            Dispose (PTChrMsg (MsgList[n]));
            MsgList.Delete (n);
         end else
            Inc (n);
      end;
      SendMsg (ident, x, y, cdir, feature, state, str, sound);
   end else begin                 // 其他角色
      // 删除相同类型的消息
      if MsgList.Count > 0 then begin
         for i:=0 to MsgList.Count-1 do begin
            if PTChrMsg (MsgList[i]).Ident = ident then begin
               Dispose (PTChrMsg (MsgList[i]));
               MsgList.Delete (i);
               break;
            end;
         end;
      end;
      SendMsg (ident, x, y, cdir, feature, state, str, sound);
   end;
end;

{ TActor.CleanUserMsgs
  功能: 清除用户发送的消息
  实现原理: 删除消息列表中所有客户端发送的消息(3000-3099) }
procedure TActor.CleanUserMsgs;
var
   n: integer;
begin
   n := 0;
   while TRUE do begin
      if n >= MsgList.Count then break;
      // 删除客户端发送的消息(3000-3099)
      if (PTChrMsg (MsgList[n]).Ident >= 3000) and
         (PTChrMsg (MsgList[n]).Ident <= 3099)
         then begin
         Dispose (PTChrMsg (MsgList[n]));
         MsgList.Delete (n);
      end else
         Inc (n);
   end;
end;

{ TActor.CalcActorFrame
  功能: 计算角色动作帧
  实现原理: 
    1. 根据当前动作类型获取对应的动作配置
    2. 计算起始帧、结束帧、帧时间等参数
    3. 设置位置偏移 }
procedure TActor.CalcActorFrame;
var
   pm: PTMonsterAction;
   haircount: integer;
begin
   BoUseMagic := FALSE;
   currentframe := -1;

   BodyOffset := GetOffset (Appearance);  // 获取图像偏移
   pm := RaceByPM (Race, Appearance);     // 获取动作配置
   if pm = nil then exit;

   case CurrentAction of
      SM_TURN:                            // 转向/站立动作
         begin
            startframe := pm.ActStand.start + Dir * (pm.ActStand.frame + pm.ActStand.skip);
            endframe := startframe + pm.ActStand.frame - 1;
            frametime := pm.ActStand.ftime;
            starttime := GetTickCount;
            defframecount := pm.ActStand.frame;
            Shift (Dir, 0, 0, 1);
         end;
      SM_WALK, SM_RUSH, SM_RUSHKUNG, SM_BACKSTEP:  // 行走/冲刺/后退动作
         begin
            startframe := pm.ActWalk.start + Dir * (pm.ActWalk.frame + pm.ActWalk.skip);
            endframe := startframe + pm.ActWalk.frame - 1;
            frametime := WalkFrameDelay;  // 使用行走帧延迟
            starttime := GetTickCount;
            maxtick := pm.ActWalk.UseTick;
            curtick := 0;
            movestep := 1;
            if CurrentAction = SM_BACKSTEP then  // 后退时反向移动
               Shift (GetBack(Dir), movestep, 0, endframe-startframe+1)
            else
               Shift (Dir, movestep, 0, endframe-startframe+1);
         end;
      {SM_BACKSTEP:
         begin
            startframe := pm.ActWalk.start + (pm.ActWalk.frame - 1) + Dir * (pm.ActWalk.frame + pm.ActWalk.skip);
            endframe := startframe - (pm.ActWalk.frame - 1);
            frametime := WalkFrameDelay; //pm.ActWalk.ftime;
            starttime := GetTickCount;
            maxtick := pm.ActWalk.UseTick;
            curtick := 0;
            movestep := 1;
            Shift (GetBack(Dir), movestep, 0, endframe-startframe+1);
         end;}
      SM_HIT:                            // 攻击动作
         begin
            startframe := pm.ActAttack.start + Dir * (pm.ActAttack.frame + pm.ActAttack.skip);
            endframe := startframe + pm.ActAttack.frame - 1;
            frametime := pm.ActAttack.ftime;
            starttime := GetTickCount;
            WarModeTime := GetTickCount;  // 记录战斗时间
            Shift (Dir, 0, 0, 1);
         end;
      SM_STRUCK:                          // 受击动作
         begin
            startframe := pm.ActStruck.start + Dir * (pm.ActStruck.frame + pm.ActStruck.skip);
            endframe := startframe + pm.ActStruck.frame - 1;
            frametime := struckframetime; // 使用受击帧时间
            starttime := GetTickCount;
            Shift (Dir, 0, 0, 1);
         end;
      SM_DEATH:                           // 死亡状态(已死亡)
         begin
            startframe := pm.ActDie.start + Dir * (pm.ActDie.frame + pm.ActDie.skip);
            endframe := startframe + pm.ActDie.frame - 1;
            startframe := endframe;       // 直接显示最后一帧
            frametime := pm.ActDie.ftime;
            starttime := GetTickCount;
         end;
      SM_NOWDEATH:                        // 正在死亡(播放死亡动画)
         begin
            startframe := pm.ActDie.start + Dir * (pm.ActDie.frame + pm.ActDie.skip);
            endframe := startframe + pm.ActDie.frame - 1;
            frametime := pm.ActDie.ftime;
            starttime := GetTickCount;
         end;
      SM_SKELETON:                        // 骷髅状态
         begin
            startframe := pm.ActDeath.start + Dir;
            endframe := startframe + pm.ActDeath.frame - 1;
            frametime := pm.ActDeath.ftime;
            starttime := GetTickCount;
         end;
   end;
end;

procedure TActor.ReadyAction (msg: TChrMsg);
var
   i, n: integer;
   pmag: PTUseMagicInfo;
begin
   actbeforex := XX;
   actbeforey := YY;

   if msg.Ident = SM_ALIVE then begin
      Death := FALSE;
      Skeleton := FALSE;
   end;

   if not Death then begin
      case msg.Ident of
         SM_TURN, SM_WALK, SM_BACKSTEP, SM_RUSH, SM_RUSHKUNG,SM_RUN, SM_DIGUP, SM_ALIVE:
            begin
               Feature := msg.feature;
               State := msg.state;

               //캐릭터의 부가적인 상태 표시
               if State and STATE_OPENHEATH <> 0 then BoOpenHealth := TRUE
               // 2003/03/04 그룹원 탐기표시
               else begin
                   n := 0;
                   for i:=1 to ViewListCount do
                       if (ViewList[i].Index = RecogId) then n := i;
                   if n = 0 then
                       BoOpenHealth := FALSE;
               end;
            end;
      end;
      if msg.ident = SM_LIGHTING then
         n := 0;
      if Myself = self then begin
         if (msg.Ident = CM_WALK) then
            if not PlayScene.CanWalk (msg.x, msg.y) then
               exit;  //이동 불가
         if (msg.Ident = CM_RUN) then
            if not PlayScene.CanRun (Myself.XX, Myself.YY, msg.x, msg.y) then
               exit; //이동 불가

         //msg
         case msg.Ident of
            CM_TURN,
            CM_WALK,
            CM_SITDOWN,
            CM_RUN,
            CM_HIT,
            CM_POWERHIT,
            CM_LONGHIT,
            CM_WIDEHIT,  
            // 2003/03/15 신규무공
            CM_CROSSHIT,
            CM_HEAVYHIT,
            CM_BIGHIT:
               begin
                  RealActionMsg := msg; //현재 실행되고 있는 행동, 서버에 메세지를 보냄.
                  msg.Ident := msg.Ident - 3000;  //SM_?? 으로 변환 함
               end;
            CM_THROW:
               begin
                  if feature <> 0 then begin
                     TargetX := TActor(msg.feature).XX;  //x 던지는 목표
                     TargetY := TActor(msg.feature).YY;    //y
                     TargetRecog := TActor(msg.feature).RecogId;
                  end;
                  RealActionMsg := msg;
                  msg.Ident := SM_THROW;
               end;
            CM_FIREHIT:
               begin
                  RealActionMsg := msg;
                  msg.Ident := SM_FIREHIT;
               end;  
            CM_SPELL:
               begin
                  RealActionMsg := msg;
                  pmag := PTUseMagicInfo (msg.feature);
                  RealActionMsg.Dir := pmag.MagicSerial;
                  msg.Ident := msg.Ident - 3000;  //SM_?? 으로 변환 함
               end;
         end;

         oldx := XX;
         oldy := YY;
         olddir := Dir;
      end;
      case msg.Ident of
         SM_STRUCK:
            begin
               //Abil.HP := msg.x; {HP}
               //Abil.MaxHP := msg.y; {maxHP}
               //msg.dir {damage}
               //레벨이 높으면 맞는 시간이 짧다.
               MagicStruckSound := msg.x; //1이상, 마법효과
               n := Round (200 - Abil.Level * 5);
               if n > 80 then struckframetime := n
               else struckframetime := 80;
               LastStruckTime := GetTickCount;
            end;
         SM_SPELL:
            begin
               Dir := msg.dir;
               //msg.x  :targetx
               //msg.y  :targety
               pmag := PTUseMagicInfo (msg.feature);
               if pmag <> nil then begin
                  CurMagic := pmag^;
                  CurMagic.ServerMagicCode := -1; //FIRE 대기
                  //CurMagic.MagicSerial := 0;
                  CurMagic.TargX := msg.x;
                  CurMagic.TargY := msg.y;
                  Dispose (pmag);
               end;
               //DScreen.AddSysMsg ('SM_SPELL');
            end;
         else begin
               XX := msg.x;
               YY := msg.y;
               Dir := msg.dir;
            end;
      end;

      CurrentAction := msg.Ident;
      CalcActorFrame;
      //DScreen.AddSysMsg (IntToStr(msg.Ident) + ' ' + IntToStr(XX) + ' ' + IntToStr(YY) + ' : ' + IntToStr(msg.x) + ' ' + IntToStr(msg.y));
   end else begin
      if msg.Ident = SM_SKELETON then begin
         CurrentAction := msg.Ident;
         CalcActorFrame;
         Skeleton := TRUE;
      end;
   end;
   if (msg.Ident = SM_DEATH) or (msg.Ident = SM_NOWDEATH) then begin
      Death := TRUE;
      PlayScene.ActorDied (self);
   end;

   RunSound;

end;

{ TActor.ProcMsg
  功能: 处理消息队列中的消息
  实现原理: 
    1. 循环处理消息列表中的消息
    2. 如果当前有动作在执行则等待
    3. 根据消息类型执行相应操作 }
procedure TActor.ProcMsg;
var
   msg: TChrMsg;
   meff: TMagicEff;
begin
   while TRUE do begin
      if MsgList.Count <= 0 then break;   // 消息列表为空则退出
      if CurrentAction <> 0 then break;   // 有动作在执行则等待
      msg := PTChrMsg (MsgList[0])^;
      Dispose (PTChrMsg (MsgList[0]));
      MsgList.Delete (0);
      case msg.ident of
         SM_STRUCK:                        // 受击消息
            begin
               HiterCode := msg.Sound;     // 记录攻击者ID
               ReadyAction (msg);
            end;
         SM_DEATH,                         // 死亡相关消息
         SM_NOWDEATH,
         SM_SKELETON,
         SM_ALIVE,
         SM_CROSSHIT,                      // 十字斩动作
         SM_ACTION_MIN..SM_ACTION_MAX,     // 动作消息范围
         SM_ACTION2_MIN..SM_ACTION2_MAX,
         3000..3099:                       // 客户端移动消息
            begin
               ReadyAction (msg);
            end;
         SM_SPACEMOVE_HIDE:                // 传送隐藏特效
            begin
               meff := TScrollHideEffect.Create (250, 10, XX, YY, self);
               PlayScene.EffectList.Add (meff);
               PlaySound (s_spacemove_out);
            end;
         SM_SPACEMOVE_HIDE2:               // 传送隐藏特效(类型2)
            begin
               meff := TScrollHideEffect.Create (1590, 10, XX, YY, self);
               PlayScene.EffectList.Add (meff);
               PlaySound (s_spacemove_out);
            end;
         SM_SPACEMOVE_SHOW:                // 传送显示特效
            begin
               meff := TCharEffect.Create (260, 10, self);
               PlayScene.EffectList.Add (meff);
               msg.ident := SM_TURN;
               ReadyAction (msg);
               PlaySound (s_spacemove_in);
            end;
         SM_SPACEMOVE_SHOW2:               // 传送显示特效(类型2)
            begin
               meff := TCharEffect.Create (1600, 10, self);
               PlayScene.EffectList.Add (meff);
               msg.ident := SM_TURN;
               ReadyAction (msg);
               PlaySound (s_spacemove_in);
            end;
         else
            begin
            end;
      end;
   end;

end;

{ TActor.ProcHurryMsg
  功能: 处理需要快速响应的消息(魔法相关)
  实现原理: 
    1. 遍历消息列表查找魔法消息
    2. 处理魔法发射成功/失败消息
    3. 处理完成后删除消息 }
procedure TActor.ProcHurryMsg;
var
   n: integer;
   msg: TChrMsg;
   fin: Boolean;
begin
   n := 0;
   while TRUE do begin
      if MsgList.Count <= n then break;
      msg := PTChrMsg (MsgList[n])^;
      fin := FALSE;
      case msg.Ident of
         SM_MAGICFIRE:                     // 魔法发射成功
            if CurMagic.ServerMagicCode <> 0 then begin
               CurMagic.ServerMagicCode := 111;  // 标记为已确认
               CurMagic.Target := msg.x;         // 目标ID
               if msg.y in [0..MAXMAGICTYPE-1] then
                  CurMagic.EffectType := TMagicType(msg.y);  // 特效类型
               CurMagic.EffectNumber := msg.dir; // 特效编号
               CurMagic.TargX := msg.feature;    // 目标X坐标
               CurMagic.TargY := msg.state;      // 目标Y坐标
               CurMagic.Recusion := TRUE;        // 允许递归
               fin := TRUE;
            end;
         SM_MAGICFIRE_FAIL:                // 魔法发射失败
            if CurMagic.ServerMagicCode <> 0 then begin
               CurMagic.ServerMagicCode := 0;    // 清除魔法码
               fin := TRUE;
            end;
      end;
      if fin then begin
         Dispose (PTChrMsg (MsgList[n]));
         MsgList.Delete (n);
      end else
         Inc (n);
   end;
end;

{ TActor.IsIdle
  功能: 检查角色是否空闲
  返回值: 无动作且无消息时返回TRUE }
function  TActor.IsIdle: Boolean;
begin
   if (CurrentAction = 0) and (MsgList.Count = 0) then
      Result := TRUE
   else Result := FALSE;
end;

{ TActor.ActionFinished
  功能: 检查当前动作是否完成
  返回值: 无动作或当前帧>=结束帧时返回TRUE }
function  TActor.ActionFinished: Boolean;
begin
   if (CurrentAction = 0) or (currentframe >= endframe) then
      Result := TRUE
   else Result := FALSE;
end;

{ TActor.CanWalk
  功能: 检查角色是否可以行走
  返回值: 1=可以行走, -1=魔法延迟中不能行走 }
function  TActor.CanWalk: Integer;
begin
   // 魔法延迟期间不能行走
   if (GetTickCount - LatestSpellTime < MagicPKDelayTime) then
      Result := -1   // 延迟中
   else
      Result := 1;
end;

{ TActor.CanRun
  功能: 检查角色是否可以奔跑
  返回值: 1=可以奔跑, -1=体力不足, -2=受击/魔法延迟中 }
function  TActor.CanRun: Integer;
begin
   // 体力不足或受击/魔法延迟期间不能奔跑
   Result := 1;
   if Abil.HP < RUN_MINHEALTH then begin
      Result := -1;                       // 体力不足
   end else
   if (GetTickCount - LastStruckTime < RUN_STRUCK_DELAY) or (GetTickCount - LatestSpellTime < MagicPKDelayTime) then
      Result := -2;                       // 受击或魔法延迟中
end;

{ TActor.Strucked
  功能: 检查消息列表中是否有受击消息
  返回值: 有受击消息返回TRUE }
function  TActor.Strucked: Boolean;
var
   i: integer;
begin
   Result := FALSE;
   for i:=0 to MsgList.Count-1 do begin
      if PTChrMsg (MsgList[i]).Ident = SM_STRUCK then begin
         Result := TRUE;
         break;
      end;
   end;
end;


{ TActor.Shift
  功能: 计算角色移动时的位置偏移
  参数: 
    dir - 移动方向(0-7)
    step - 移动格数
    cur - 当前步骤
    max - 最大步骤数
  实现原理: 
    1. 根据方向计算X/Y方向的像素偏移
    2. 实现平滑移动效果 }
procedure TActor.Shift (dir, step, cur, max: integer);
var
   unx, uny, ss, v: integer;
begin
   unx := UNITX * step;
   uny := UNITY * step;
   if cur > max then cur := max;
   Rx := XX;
   Ry := YY;
   ss := Round((max-cur-1) / max) * step;
   case dir of
      DR_UP:
         begin
            ss := Round((max-cur) / max) * step;
            ShiftX := 0;
            Ry := YY + ss;
            if ss = step then ShiftY := -Round(uny / max * cur)
            else ShiftY := Round(uny / max * (max-cur));
         end;
      DR_UPRIGHT:
         begin
            if max >= 6 then v := 2
            else v := 0;
            ss := Round((max-cur+v) / max) * step;
            Rx := XX - ss;
            Ry := YY + ss;
            if ss = step then begin
               ShiftX :=  Round(unx / max * cur);
               ShiftY := -Round(uny / max * cur);
            end else begin
               ShiftX := -Round(unx / max * (max-cur));
               ShiftY :=  Round(uny / max * (max-cur));
            end;
         end;
      DR_RIGHT:
         begin
            ss := Round((max-cur) / max) * step;
            Rx := XX - ss;
            if ss = step then ShiftX := Round(unx / max * cur)
            else ShiftX := -Round(unx / max * (max-cur));
            ShiftY := 0;
         end;
      DR_DOWNRIGHT:
         begin
            if max >= 6 then v := 2
            else v := 0;
            ss := Round((max-cur-v) / max) * step;
            Rx := XX - ss;
            Ry := YY - ss;
            if ss = step then begin
               ShiftX := Round(unx / max * cur);
               ShiftY := Round(uny / max * cur);
            end else begin
               ShiftX := -Round(unx / max * (max-cur));
               ShiftY := -Round(uny / max * (max-cur));
            end;
         end;
      DR_DOWN:
         begin
            if max >= 6 then v := 1
            else v := 0;
            ss := Round((max-cur-v) / max) * step;
            ShiftX := 0;
            Ry := YY - ss;
            if ss = step then ShiftY := Round(uny / max * cur)
            else ShiftY := -Round(uny / max * (max-cur));
         end;
      DR_DOWNLEFT:
         begin
            if max >= 6 then v := 2
            else v := 0;
            ss := Round((max-cur-v) / max) * step;
            Rx := XX + ss;
            Ry := YY - ss;
            if ss = step then begin
               ShiftX := -Round(unx / max * cur);
               ShiftY :=  Round(uny / max * cur);
            end else begin
               ShiftX :=  Round(unx / max * (max-cur));
               ShiftY := -Round(uny / max * (max-cur));
            end;
         end;
      DR_LEFT:
         begin
            ss := Round((max-cur) / max) * step;
            Rx := XX + ss;
            if ss = step then ShiftX := -Round(unx / max * cur)
            else ShiftX := Round(unx / max * (max-cur));
            ShiftY := 0;
         end;
      DR_UPLEFT:
         begin
            if max >= 6 then v := 2
            else v := 0;
            ss := Round((max-cur+v) / max) * step;
            Rx := XX + ss;
            Ry := YY + ss;
            if ss = step then begin
               ShiftX := -Round(unx / max * cur);
               ShiftY := -Round(uny / max * cur);
            end else begin
               ShiftX := Round(unx / max * (max-cur));
               ShiftY := Round(uny / max * (max-cur));
            end;
         end;
   end;
end;

{ TActor.FeatureChanged
  功能: 处理角色外观变化
  实现原理: 
    1. 人类角色: 更新发型、衣服、武器的图像偏移
    2. NPC: 不处理
    3. 怪物: 更新外观和图像偏移 }
procedure  TActor.FeatureChanged;
var
   haircount: integer;
begin
   case Race of
      0: begin                             // 人类角色
         hair   := HAIRfeature (Feature);  // 提取发型
         dress  := DRESSfeature (Feature); // 提取衣服
         weapon := WEAPONfeature (Feature);// 提取武器
         BodyOffset := HUMANFRAME * Dress; // 计算身体图像偏移
         haircount := WHairImg.ImageCount div HUMANFRAME div 2;
         if hair > haircount-1 then hair := haircount-1;
         hair := hair * 2;
         if hair > 1 then
            HairOffset := HUMANFRAME * (hair + Sex)  // 计算头发图像偏移
         else HairOffset := -1;
         WeaponOffset := HUMANFRAME * weapon;  // 计算武器图像偏移
      end;
      50: ;                                // NPC不处理
      else begin                           // 怪物
         Appearance := APPRfeature (Feature);  // 提取外观
         BodyOffset := GetOffset (Appearance); // 计算图像偏移
      end;
   end;
end;

{ TActor.Light
  功能: 获取角色的光照等级
  返回值: 角色光照值 }
function   TActor.Light: integer;
begin
   Result := ChrLight;
end;

{ TActor.LoadSurface
  功能: 加载角色的图像资源
  实现原理: 
    1. 根据外观获取怪物图像资源
    2. 根据当前帧加载对应的图像
    3. 支持反向播放 }
procedure  TActor.LoadSurface;
var
   mimg: TGameImages;
begin
   mimg := GetMonImg (Appearance);         // 获取怪物图像资源
   if mimg <> nil then begin
      if (not ReverseFrame) then           // 正常播放
         BodySurface := mimg.GetCachedImage (GetOffset (Appearance) + currentframe, px, py)
      else                                 // 反向播放
         BodySurface := mimg.GetCachedImage (
                            GetOffset (Appearance) + endframe - (currentframe-startframe),
                            px, py);
   end;
end;

{ TActor.CharWidth
  功能: 获取角色图像宽度
  返回值: 图像宽度(默认48像素) }
function  TActor.CharWidth: Integer;
begin
   if BodySurface <> nil then
      Result := BodySurface.Width
   else Result := 48;
end;

{ TActor.CharHeight
  功能: 获取角色图像高度
  返回值: 图像高度(默认70像素) }
function  TActor.CharHeight: Integer;
begin
   if BodySurface <> nil then
      Result := BodySurface.Height
   else Result := 70;
end;

{ TActor.CheckSelect
  功能: 检查指定位置是否选中了角色
  参数: dx, dy - 相对于角色图像的坐标
  返回值: 选中返回TRUE
  实现原理: 检查该点及周围四个点的像素是否非透明 }
function  TActor.CheckSelect (dx, dy: integer): Boolean;
var
   c: integer;
begin
   Result := FALSE;
   if BodySurface <> nil then begin
      c := BodySurface.Pixels[dx, dy];
      // 检查当前点和上下左右四个点是否都非透明
      if (c <> 0) and
         ((BodySurface.Pixels[dx-1, dy] <> 0) and
          (BodySurface.Pixels[dx+1, dy] <> 0) and
          (BodySurface.Pixels[dx, dy-1] <> 0) and
          (BodySurface.Pixels[dx, dy+1] <> 0)) then
         Result := TRUE;
   end;
end;

{ TActor.DrawEffSurface
  功能: 绘制带特效的图像表面
  参数: 
    dsurface - 目标表面
    source - 源图像
    ddx, ddy - 绘制位置
    blend - 是否混合绘制
    ceff - 颜色特效
  实现原理: 
    1. 检查是否隐身状态
    2. 根据是否混合和特效类型选择绘制方式 }
procedure TActor.DrawEffSurface (dsurface, source: TTexture; ddx, ddy: integer; blend: Boolean; ceff: TColorEffect);
begin
   // 检查隐身状态
   if State and $00800000 <> 0 then  blend := TRUE;

   if not Blend then begin           // 非混合模式
      if ceff = ceNone then begin    // 无特效，直接绘制
         if source <> nil then
            dsurface.Draw (ddx, ddy, source.ClientRect, source, TRUE);
      end else begin                 // 有特效，先应用特效再绘制
         if source <> nil then begin
            ImgMixSurface.SetSize(source.Width, source.Height);
            ImgMixSurface.Draw (0, 0, source.ClientRect, source, FALSE);
            DrawEffect (ImgMixSurface, ceff);
            dsurface.Draw (ddx, ddy, source.ClientRect, ImgMixSurface, TRUE);
         end;
      end;
   end else begin                    // 混合模式(半透明)
      if ceff = ceNone then begin    // 无特效
         if source <> nil then
            dsurface.FastDrawAlpha(Bounds(ddx, ddy, Source.Width, Source.Height), Source.ClientRect, Source);
      end else begin                 // 有特效
         if source <> nil then begin
            ImgMixSurface.SetSize(source.Width, source.Height);
            ImgMixSurface.Draw (0, 0, source.ClientRect, source, FALSE);
            DrawEffect (ImgMixSurface, ceff);
            dsurface.FastDrawAlpha(Bounds(ddx, ddy, ImgMixSurface.Width, ImgMixSurface.Height), ImgMixSurface.ClientRect, ImgMixSurface);
         end;
      end;
   end;
end;

{ TActor.DrawWeaponGlimmer
  功能: 绘制武器闪光效果
  参数: 
    dsurface - 目标表面
    ddx, ddy - 绘制位置
  注意: 当前未使用(图形错误) }
procedure TActor.DrawWeaponGlimmer (dsurface: TTexture; ddx, ddy: integer);
var
   idx, ax, ay: integer;
   d: TTexture;
begin
   // 未使用(图形错误)...
   (*if BoNextTimeFireHit and WarMode and GlimmingMode then begin
      if GetTickCount - GlimmerTime > 200 then begin
         GlimmerTime := GetTickCount;
         Inc (CurGlimmer);
         if CurGlimmer >= MaxGlimmer then CurGlimmer := 0;
      end;
      idx := GetEffectBase (5-1{죠삽숲랬반짝임}, 1) + Dir*10 + CurGlimmer;
      d := FrmMain.WMagic.GetCachedImage (idx, ax, ay);
      if d <> nil then
         DrawBlend (dsurface, ddx + ax, ddy + ay, d, 1);
                          //dx + ax + ShiftX,
                          //dy + ay + ShiftY,
                          //d, 1);
   end;*)
end;

{ TActor.GetDrawEffectValue
  功能: 获取角色绘制时的颜色特效
  返回值: 颜色特效类型
  实现原理: 根据角色状态返回对应的颜色特效 }
function TActor.GetDrawEffectValue: TColorEffect;
var
   ceff: TColorEffect;
begin
   ceff := ceNone;

   // 被选中的角色变亮
   if (FocusCret = self) or (MagicTarget = self) then begin
      ceff := ceBright;
   end;

   // 中毒状态的颜色特效
   if State and $80000000 <> 0 then begin
      ceff := ceGreen;                    // 绿色(绿毒)
   end;
   if State and $40000000 <> 0 then begin
      ceff := ceRed;                      // 红色(红毒)
   end;
   if State and $20000000 <> 0 then begin
      ceff := ceBlue;                     // 蓝色
   end;
   if State and $10000000 <> 0 then begin
      ceff := ceYellow;                   // 黄色
   end;
   // 麻痹类状态
   if State and $08000000 <> 0 then begin
      ceff := ceFuchsia;                  // 紫红色(麻痹)
   end;
   if State and $04000000 <> 0 then begin
      ceff := ceGrayScale;                // 灰度(石化)
   end;
   Result := ceff;
end;

{ TActor.DrawChr
  功能: 绘制角色
  参数: 
    dsurface - 目标表面
    dx, dy - 绘制位置
    blend - 是否混合绘制
  实现原理: 
    1. 每60秒重新加载图像资源
    2. 绘制身体图像
    3. 绘制魔法特效 }
procedure TActor.DrawChr (dsurface: TTexture; dx, dy: integer; blend: Boolean);
var
   idx, ax, ay: integer;
   d: TTexture;
   ceff: TColorEffect;
   wimg: TGameImages;
begin
   if not (Dir in [0..7]) then exit;
   // 每60秒重新加载图像资源(防止内存泄漏)
   if GetTickCount - loadsurfacetime > 60 * 1000 then begin
      loadsurfacetime := GetTickCount;
      LoadSurface;
   end;

   ceff := GetDrawEffectValue;            // 获取颜色特效

   // 绘制身体
   if BodySurface <> nil then begin
      DrawEffSurface (dsurface, BodySurface, dx + px + ShiftX, dy + py + ShiftY, blend, ceff);
   end;

   // 绘制魔法特效
   if BoUseMagic and (CurMagic.EffectNumber > 0) then
      if CurEffFrame in [0..SpellFrame-1] then begin
         GetEffectBase (Curmagic.EffectNumber-1, 0, wimg, idx);
         idx := idx + CurEffFrame;
         if wimg <> nil then
            d := wimg.GetCachedImage (idx, ax, ay);
         if d <> nil then
            DrawBlend (dsurface,
                             dx + ax + ShiftX,
                             dy + ay + ShiftY,
                             d);
      end;
end;

{ TActor.DrawEff
  功能: 绘制角色特效(空实现，由子类重写)
  参数: 
    dsurface - 目标表面
    dx, dy - 绘制位置 }
procedure  TActor.DrawEff (dsurface: TTexture; dx, dy: integer);
begin
end;


{ TActor.GetDefaultFrame
  功能: 获取默认帧(站立或死亡状态)
  参数: wmode - 是否战斗姿态
  返回值: 默认帧索引
  实现原理: 
    1. 死亡状态返回死亡帧
    2. 存活状态返回站立帧 }
function  TActor.GetDefaultFrame (wmode: Boolean): integer;
var
   cf, dr: integer;
   pm: PTMonsterAction;
begin
   pm := RaceByPm (Race, Appearance);
   if pm = nil then exit;

   if Death then begin                    // 死亡状态
      if Skeleton then
         Result := pm.ActDeath.start      // 骷髅帧
      else Result := pm.ActDie.start + Dir * (pm.ActDie.frame + pm.ActDie.skip) + (pm.ActDie.frame - 1);  // 死亡最后一帧
   end else begin                         // 存活状态
      defframecount := pm.ActStand.frame;
      if currentdefframe < 0 then cf := 0
      else if currentdefframe >= pm.ActStand.frame then cf := 0
      else cf := currentdefframe;
      Result := pm.ActStand.start + Dir * (pm.ActStand.frame + pm.ActStand.skip) + cf;  // 站立帧
   end;
end;

{ TActor.DefaultMotion
  功能: 设置角色为默认姿态(站立)
  实现原理: 
    1. 检查战斗姿态是否超时(4秒)
    2. 设置当前帧为默认帧 }
procedure TActor.DefaultMotion;
begin
   ReverseFrame := FALSE;
   if WarMode then begin
      // 战斗姿态超过4秒后自动取消
      if (GetTickCount - WarModeTime > 4*1000) then
         WarMode := FALSE;
   end;
   currentframe := GetDefaultFrame (WarMode);
   Shift (Dir, 0, 1, 1);
end;

{ TActor.SetSound
  功能: 初始化角色的声音变量
  实现原理: 
    1. 人类角色: 根据地形设置脚步声，根据武器设置攻击声音
    2. 怪物: 根据种族设置各种声音效果 }
procedure TActor.SetSound;
var
   cx, cy, bidx, wunit, attackweapon: integer;
   hiter: TActor;
begin
   if Race = 0 then begin
      if (self = Myself) and
         ((CurrentAction=SM_WALK) or
          (CurrentAction=SM_BACKSTEP) or
          (CurrentAction=SM_RUN) or
          (CurrentAction=SM_RUSH) or 
          (CurrentAction=SM_RUSHKUNG)
         )
      then begin
         cx := Myself.XX - Map.BlockLeft;
         cy := Myself.YY - Map.BlockTop;
         cx := cx div 2 * 2;
         cy := cy div 2 * 2;
         bidx := Map.MArr[cx, cy].BkImg and $7FFF;
         wunit := Map.MArr[cx, cy].Area;
         bidx := wunit * 10000 + bidx - 1;
         case bidx of
            //짧은 풀
            330..349, 450..454, 550..554, 750..754,
            950..954, 1250..1254, 1400..1424, 1455..1474,
            1500..1524, 1550..1574:
               footstepsound := s_walk_lawn_l;

            //중간풀

            //긴 풀
            250..254, 1005..1009, 1050..1054, 1060..1064, 1450..1454,
            1650..1654:
               footstepsound := s_walk_rough_l;

            //돌 길
            //대리석 바닥
            605..609, 650..654, 660..664, 2000..2049,
            3025..3049, 2400..2424, 4625..4649, 4675..4678:
               footstepsound := s_walk_stone_l;

            //동굴안
            1825..1924, 2150..2174, 3075..3099, 3325..3349,
            3375..3399:
               footstepsound := s_walk_cave_l;

            //나무바닥
           3230, 3231, 3246, 3277:
               footstepsound := s_walk_wood_l;

           //던전..
           3780..3799:
               footstepsound := s_walk_wood_l;

           3825..4434:
               if (bidx-3825) mod 25 = 0 then footstepsound := s_walk_wood_l
               else footstepsound := s_walk_ground_l;


            //집안(소리 별루 안남)
             2075..2099, 2125..2149:
               footstepsound := s_walk_room_l;

            //개울
            1800..1824:
               footstepsound := s_walk_water_l;

            else
               footstepsound := s_walk_ground_l;
         end;
         //궁전내부
         if (bidx >= 825) and (bidx <= 1349) then begin
            if ((bidx-825) div 25) mod 2 = 0 then
               footstepsound := s_walk_stone_l;
         end;
         //동굴내부
         if (bidx >= 1375) and (bidx <= 1799) then begin
            if ((bidx-1375) div 25) mod 2 = 0 then
               footstepsound := s_walk_cave_l;
         end;
         case bidx of
            1385, 1386, 1391, 1392:
               footstepsound := s_walk_wood_l;
         end;

         bidx := Map.MArr[cx, cy].MidImg and $7FFF;
         bidx := bidx - 1;
         case bidx of
            0..115:
               footstepsound := s_walk_ground_l;
            120..124:
               footstepsound := s_walk_lawn_l;
         end;

         bidx := Map.MArr[cx, cy].FrImg and $7FFF;
         bidx := bidx - 1;
         case bidx of
            //벽돌길
            221..289, 583..658, 1183..1206, 7163..7295,
            7404..7414:
               footstepsound := s_walk_stone_l;
            //나무마루
            3125..3267, {3319..3345, 3376..3433,} 3757..3948,
            6030..6999:
               footstepsound := s_walk_wood_l;
            //방바닥
            3316..3589:
               footstepsound := s_walk_room_l;
         end;
         if CurrentAction = SM_RUN then
            footstepsound := footstepsound + 2;

      end;

      if Sex = 0 then begin //남자
         screamsound := s_man_struck;
         diesound := s_man_die;
      end else begin //여자
         screamsound := s_wom_struck;
         diesound := s_wom_die;
      end;

      case CurrentAction of
         // 2003/03/15 신규무공
         SM_THROW, SM_HIT, SM_HIT+1, SM_HIT+2, SM_POWERHIT, SM_LONGHIT, SM_WIDEHIT, SM_FIREHIT, SM_CROSSHIT:
            begin
               case (weapon div 2) of
                  6, 20:  weaponsound := s_hit_short;
                  1, 27, 28:  weaponsound := s_hit_wooden;
                  2, 13, 9, 5, 14, 22, 25:  weaponsound := s_hit_sword;
                  4, 17, 10, 15, 16, 23, 26, 29:  weaponsound := s_hit_do;
                  3, 7, 11:  weaponsound := s_hit_axe;
                  24:  weaponsound := s_hit_club;
                  8, 12, 18, 21:  weaponsound := s_hit_long;
                  else weaponsound := s_hit_fist;
               end;
            end;
         SM_STRUCK:
            begin
               if MagicStruckSound >= 1 then begin  //마법으로 맞음
                  //strucksound := s_struck_magic;  //임시..
               end else begin
                  hiter := PlayScene.FindActor (HiterCode);
                  attackweapon := 0;
                  if hiter <> nil then begin //때린놈이 무엇으로 때렸는지 검사
                     attackweapon := hiter.weapon div 2;
                     if hiter.Race = 0 then
                        case (dress div 2) of
                           3: //갑옷
                              case attackweapon of
                                 6:  strucksound := s_struck_armor_sword;
                                 1,2,4,5,9,10,13,14,15,16,17: strucksound := s_struck_armor_sword;
                                 3,7,11: strucksound := s_struck_armor_axe;
                                 8,12,18: strucksound := s_struck_armor_longstick;
                                 else strucksound := s_struck_armor_fist;
                              end;
                           else //일반
                              case attackweapon of
                                 6: strucksound := s_struck_body_sword;
                                 1,2,4,5,9,10,13,14,15,16,17: strucksound := s_struck_body_sword;
                                 3,7,11: strucksound := s_struck_body_axe;
                                 8,12,18: strucksound := s_struck_body_longstick;
                                 else strucksound := s_struck_body_fist;
                              end;
                        end;
                  end;
               end;
            end;
      end;

      //마법 소리
      if BoUseMagic and (CurMagic.MagicSerial > 0) then begin
         magicstartsound := 10000 + CurMagic.MagicSerial * 10;
         magicfiresound := 10000 + CurMagic.MagicSerial * 10 + 1;
         magicexplosionsound := 10000 + CurMagic.MagicSerial * 10 + 2;
      end;

   end else begin
      if CurrentAction = SM_STRUCK then begin
         if MagicStruckSound >= 1 then begin  //마법으로 맞음
            //strucksound := s_struck_magic;  //임시..
         end else begin
            hiter := PlayScene.FindActor (HiterCode);
            if hiter <> nil then begin  //때린놈이 무엇으로 때렸는지 검사
               attackweapon := hiter.weapon div 2;
               case attackweapon of
                  6: strucksound := s_struck_body_sword;
                  1,2,4,5,9,10,13,14,15,16,17: strucksound := s_struck_body_sword;
                  3,11: strucksound := s_struck_body_axe;
                  8,12,18: strucksound := s_struck_body_longstick;
                  else strucksound := s_struck_body_fist;
               end;
            end;
         end;
      end;

      if Race = 50 then begin
      end else begin
         appearsound := 200 + (Appearance) * 10;
         normalsound := 200 + (Appearance) * 10 + 1;
         attacksound := 200 + (Appearance) * 10 + 2;  //우워억
         weaponsound := 200 + (Appearance) * 10 + 3;  //휙(무기휘두룸)
         screamsound := 200 + (Appearance) * 10 + 4;
         diesound := 200 + (Appearance) * 10 + 5;
         die2sound := 200 + (Appearance) * 10 + 6;
      end;
   end;

   //칼 맞는 소리
   if CurrentAction = SM_STRUCK then begin
      hiter := PlayScene.FindActor (HiterCode);
      attackweapon := 0;
      if hiter <> nil then begin  //때린놈이 무엇으로 때렸는지 검사
         attackweapon := hiter.weapon div 2;
         if hiter.Race = 0 then
            case (attackweapon div 2) of
               6, 20:  struckweaponsound := s_struck_short;
               1:  struckweaponsound := s_struck_wooden;
               2, 13, 9, 5, 14, 22:  struckweaponsound := s_struck_sword;
               4, 17, 10, 15, 16, 23:  struckweaponsound := s_struck_do;
               3, 7, 11:  struckweaponsound := s_struck_axe;
               24:  struckweaponsound := s_struck_club;
               8, 12, 18, 21:  struckweaponsound := s_struck_wooden; //long;
               //else struckweaponsound := s_struck_fist;
            end;
      end;
   end;
end;

{ TActor.RunSound
  功能: 播放角色动作声音
  实现原理: 根据当前动作类型播放对应的声音效果 }
procedure  TActor.RunSound;
begin
   borunsound := TRUE;
   SetSound;
   case CurrentAction of
      SM_STRUCK:                          // 受击声音
         begin
            if (struckweaponsound >= 0) then PlaySound (struckweaponsound);
            if (strucksound >= 0) then PlaySound (strucksound);
            if (screamsound >= 0) then PlaySound (screamsound);
         end;
      SM_NOWDEATH:                        // 死亡声音
         begin
            if (diesound >= 0) then PlaySound (diesound);
         end;
      SM_THROW, SM_HIT, SM_FLYAXE, SM_LIGHTING, SM_DIGDOWN:  // 攻击声音
         begin
            if attacksound >= 0 then PlaySound (attacksound);
         end;
      SM_ALIVE, SM_DIGUP:                 // 出场声音
         begin
            PlaySound (appearsound);
         end;
      SM_SPELL:                           // 魔法声音
         begin
            PlaySound (magicstartsound);
         end;
   end;
end;

procedure  TActor.RunActSound (frame: integer);
begin
   if borunsound then begin
      if Race = 0 then begin
         case CurrentAction of
            SM_THROW, SM_HIT, SM_HIT+1, SM_HIT+2:
               if frame = 2 then begin
                  PlaySound (weaponsound);
                  borunsound := FALSE; //한번만 소리냄
               end;
            SM_POWERHIT:
               if frame = 2 then begin
                  PlaySound (weaponsound);
                  if Sex = 0 then PlaySound (s_yedo_man)
                  else PlaySound (s_yedo_woman);
                  borunsound := FALSE; //한번만 소리냄
               end;
            SM_LONGHIT:
               if frame = 2 then begin
                  PlaySound (weaponsound);
                  PlaySound (s_longhit);
                  borunsound := FALSE; //한번만 소리냄
               end;
            SM_WIDEHIT:
               if frame = 2 then begin
                  PlaySound (weaponsound);
                  PlaySound (s_widehit);
                  borunsound := FALSE; //한번만 소리냄
               end; 
            SM_FIREHIT:
               if frame = 2 then begin
                  PlaySound (weaponsound);
                  PlaySound (s_firehit);
                  borunsound := FALSE; //한번만 소리냄
               end;
            SM_CROSSHIT:
               if frame = 2 then begin
                  PlaySound (weaponsound);
                 // PlaySound (s_crosshit);   董珂
                 
                  borunsound := FALSE; //한번만 소리냄
               end;
         end;
      end else begin
         if Race = 50 then begin
         end else begin
          //(** 새 사운드
            if (CurrentAction = SM_WALK) or (CurrentAction = SM_TURN) then begin
               if (frame = 1) and (Random(8) = 1) then begin
                  PlaySound (normalsound);
                  borunsound := FALSE; //한번만 소리냄
               end;
            end;
            if CurrentAction = SM_HIT then begin
               if (frame = 3) and (attacksound >= 0) then begin
                  PlaySound (weaponsound);
                  borunsound := FALSE;
               end;
            end;
            case Appearance of
               80: //관박쥐
                  begin
                     if CurrentAction = SM_NOWDEATH then begin
                        if (frame = 2) then begin
                           PlaySound (die2sound);
                           borunsound := FALSE; //한번만 소리냄
                        end;
                     end;
                  end;
            end;
         end; //*)

      end;
   end;
end;

{ TActor.RunFrameAction
  功能: 执行帧动作(空实现，由子类重写)
  参数: frame - 当前帧索引 }
procedure  TActor.RunFrameAction (frame: integer);
begin
end;

{ TActor.ActionEnded
  功能: 动作结束处理(空实现，由子类重写) }
procedure  TActor.ActionEnded;
begin
end;

{ TActor.Run
  功能: 角色动作主循环
  实现原理: 
    1. 处理动作帧进度
    2. 处理魔法施放和发射
    3. 处理动作完成后的状态切换
    4. 空闲时显示默认动作 }
procedure TActor.Run;
   // 内部函数: 检查魔法是否超时
   function MagicTimeOut: Boolean;
   begin
      if self = Myself then begin
         Result := GetTickCount - WaitMagicRequest > 3000;  // 主角3秒超时
      end else
         Result := GetTickCount - WaitMagicRequest > 2000;  // 其他2秒超时
      if Result then
         CurMagic.ServerMagicCode := 0;
   end;
var
   prv: integer;
   frametimetime: longword;
   bofly: Boolean;
begin
   if (CurrentAction = SM_WALK) or
      (CurrentAction = SM_BACKSTEP) or
      (CurrentAction = SM_RUN) or
      (CurrentAction = SM_RUSH) or   
      (CurrentAction = SM_RUSHKUNG)
   then exit;

   msgmuch := FALSE;
   if self <> Myself then begin
      if MsgList.Count >= 2 then msgmuch := TRUE;
   end;

   //사운드 효과
   RunActSound (currentframe - startframe);
   RunFrameAction (currentframe - startframe);

   prv := currentframe;
   if CurrentAction <> 0 then begin
      if (currentframe < startframe) or (currentframe > endframe) then
         currentframe := startframe;

      if (self <> Myself) and (BoUseMagic) then begin
         frametimetime := Round(frametime / 1.8);
      end else begin
         if msgmuch then frametimetime := Round(frametime * 2 / 3)
         else frametimetime := frametime;
      end;

      if GetTickCount - starttime > frametimetime then begin
         if currentframe < endframe then begin
            //마법인 경우 서버의 신호를 받아, 성공/실패를 확인한후
            //마지막동작을 끝낸다.
            if BoUseMagic then begin
               if (CurEffFrame = SpellFrame-2) or (MagicTimeOut) then begin //기다림 끝
                  if (CurMagic.ServerMagicCode >= 0) or (MagicTimeOut) then begin //서버로 부터 받은 결과. 아직 안왔으면 기다림
                     Inc (currentframe);
                     Inc(CurEffFrame);
                     starttime := GetTickCount;
                  end;
               end else begin
                  if currentframe < endframe - 1 then Inc (currentframe);
                  Inc (CurEffFrame);
                  starttime := GetTickCount;
               end;
            end else begin
               Inc (currentframe);
               starttime := GetTickCount;
            end;

         end else begin
            if BoDelActionAfterFinished then begin
               //이 동작후 사라짐.
               BoDelActor := TRUE;
            end;
            //동작이 끝남.
            if self = Myself then begin
               //주인공 인경우
               if FrmMain.ServerAcceptNextAction then begin
                  ActionEnded;
                  CurrentAction := 0;
                  BoUseMagic := FALSE;
               end;
            end else begin
               ActionEnded;
               CurrentAction := 0; //동작 완료
               BoUseMagic := FALSE;
            end;
         end;
         if BoUseMagic then begin
            //마법을 쓰는 경우
            if CurEffFrame = SpellFrame-1 then begin //마법 발사 시점
               //마법 발사
               if CurMagic.ServerMagicCode > 0 then begin
                  with CurMagic do
                     PlayScene.NewMagic (self,
                                      ServerMagicCode,
                                      EffectNumber,
                                      XX,
                                      YY,
                                      TargX,
                                      TargY,
                                      Target,
                                      EffectType,
                                      Recusion,
                                      AniTime,
                                      bofly);
                  if bofly then
                     PlaySound (magicfiresound)
                  else
                     PlaySound (magicexplosionsound);
               end;
               //LatestSpellTime := GetTickCount;
               CurMagic.ServerMagicCode := 0;
            end;
         end;
      end;
      if Appearance in [0, 1, 43] then currentdefframe := -10
      else currentdefframe := 0;
      defframetime := GetTickCount;
   end else begin
      if GetTickCount - smoothmovetime > 200 then begin
         if GetTickCount - defframetime > 500 then begin
            defframetime := GetTickCount;
            Inc (currentdefframe);
            if currentdefframe >= defframecount then
               currentdefframe := 0;
         end;
         DefaultMotion;
      end;
   end;

   if prv <> currentframe then begin
      loadsurfacetime := GetTickCount;
      LoadSurface;
   end;

end;

function  TActor.Move (step: integer): Boolean;
var
   prv, curstep, maxstep: integer;
   fastmove, normmove: Boolean;
begin
   Result := FALSE;
   fastmove := FALSE;
   normmove := FALSE;
   if (CurrentAction = SM_BACKSTEP) then //or (CurrentAction = SM_RUSH) or (CurrentAction = SM_RUSHKUNG) then
      fastmove := TRUE;
   if (CurrentAction = SM_RUSH) or (CurrentAction = SM_RUSHKUNG){ or (CurrentAction = SM_DAILYHIT{磊휑숲랬} then
      normmove := TRUE;
   if (self = Myself) and (not fastmove) and (not normmove) then begin
      BoMoveSlow := FALSE;
      BoAttackSlow := FALSE;
      MoveSlowLevel := 0;
      if Abil.Weight > Abil.MaxWeight then begin
         MoveSlowLevel := Abil.Weight div Abil.MaxWeight;
         BoMoveSlow := TRUE;
      end;
      if Abil.WearWeight > Abil.MaxWearWeight then begin
         MoveSlowLevel := MoveSlowLevel + Abil.WearWeight div Abil.MaxWearWeight;
         BoMoveSlow := TRUE;
      end;
      if Abil.HandWeight > Abil.MaxHandWeight then begin
         BoAttackSlow := TRUE;
      end;
      if BoMoveSlow and (SkipTick < MoveSlowLevel) then begin
         Inc (SkipTick); //한번 쉰다.
         exit;
      end else begin
         SkipTick := 0;
      end;
      //사운드 효과
      if (CurrentAction = SM_WALK) or
         (CurrentAction = SM_BACKSTEP) or
         (CurrentAction = SM_RUN) or
         (CurrentAction = SM_RUSH) or    
        // (CurrentAction=  SM_DAILYHIT){磊휑숲랬} or
         (CurrentAction = SM_RUSHKUNG)
      then begin
         case (currentframe - startframe) of
            1: PlaySound (footstepsound);
            4: PlaySound (footstepsound + 1);
         end;
      end;
   end;

   Result := FALSE;
   msgmuch := FALSE;
   if self <> Myself then begin
      if MsgList.Count >= 2 then msgmuch := TRUE;
   end;
   prv := currentframe;
   //걷기 뛰기
   if (CurrentAction = SM_WALK) or
      (CurrentAction = SM_RUN) or
      (CurrentAction = SM_RUSH) or   
      (CurrentAction = SM_RUSHKUNG)
   then begin
      if (currentframe < startframe) or (currentframe > endframe) then begin
         currentframe := startframe - 1;
      end;
      if currentframe < endframe then begin
         Inc (currentframe);
         if msgmuch and not normmove then //or fastmove then
            if currentframe < endframe then
               Inc (currentframe);

         //부드럽게 이동하게 하려고
         curstep := currentframe-startframe + 1;
         maxstep := endframe-startframe + 1;
         Shift (Dir, movestep, curstep, maxstep);
      end;
      if currentframe >= endframe then begin
         if self = Myself then begin
            if FrmMain.ServerAcceptNextAction then begin
               CurrentAction := 0;     //서버의 신호를 받으면 다음 동작
               LockEndFrame := TRUE;   //서버의신호가 없어서 마지막프래임에서 멈춤
               smoothmovetime := GetTickCount;
            end;
         end else begin
            CurrentAction := 0; //동작 완료
            LockEndFrame := TRUE;
            smoothmovetime := GetTickCount;
         end;
         Result := TRUE;
      end;
      if CurrentAction = SM_RUSH then begin
         if self = Myself then begin
            DizzyDelayStart := GetTickCount;
            DizzyDelayTime := 300; //딜레이
         end;
      end;
      if CurrentAction = SM_RUSHKUNG then begin  //柰찬녑旒
         if currentframe >= endframe - 3 then begin
            XX := actbeforex;
            YY := actbeforey;
            Rx := XX;
            Ry := YY;
            CurrentAction := 0;
            LockEndFrame := TRUE;
            //smoothmovetime := GetTickCount;
         end;
      end;    
      Result := TRUE;
   end;
   //뒷걸음질
   if (CurrentAction = SM_BACKSTEP) then begin
      if (currentframe > endframe) or (currentframe < startframe) then begin
         currentframe := endframe + 1;
      end;
      if currentframe > startframe then begin
         Dec (currentframe);
         if msgmuch or fastmove then
            if currentframe > startframe then Dec (currentframe);

         //부드럽게 이동하게 하려고
         curstep := endframe - currentframe + 1;
         maxstep := endframe - startframe + 1;
         Shift (GetBack(Dir), movestep, curstep, maxstep);
      end;
      if currentframe <= startframe then begin
         if self = Myself then begin
            //if FrmMain.ServerAcceptNextAction then begin
               CurrentAction := 0;     //서버의 신호를 받으면 다음 동작
               LockEndFrame := TRUE;   //서버의신호가 없어서 마지막프래임에서 멈춤
               smoothmovetime := GetTickCount;

               //뒤로 밀린 다음 한동안 못 움직인다.
               DizzyDelayStart := GetTickCount;
               DizzyDelayTime := 1000; //1초 딜레이
            //end;
         end else begin
            CurrentAction := 0; //동작 완료
            LockEndFrame := TRUE;
            smoothmovetime := GetTickCount;
         end;
         Result := TRUE;
      end;
      Result := TRUE;
   end;
   if prv <> currentframe then begin
      loadsurfacetime := GetTickCount;
      LoadSurface;
   end;
end;

{ TActor.MoveFail
  功能: 移动失败处理
  实现原理: 恢复到移动前的位置和方向 }
procedure TActor.MoveFail;
begin
   CurrentAction := 0;                    // 动作完成
   LockEndFrame := TRUE;
   Myself.XX := oldx;                     // 恢复旧位置
   Myself.YY := oldy;
   Myself.Dir := olddir;                  // 恢复旧方向
   CleanUserMsgs;
end;

{ TActor.CanCancelAction
  功能: 检查是否可以取消当前动作
  返回值: 可以取消返回TRUE }
function  TActor.CanCancelAction: Boolean;
begin
   Result := FALSE;
   if CurrentAction = SM_HIT then         // 只有攻击动作可取消
      if not BoUseEffect then             // 且未使用特效
         Result := TRUE;
end;

{ TActor.CancelAction
  功能: 取消当前动作 }
procedure TActor.CancelAction;
begin
   CurrentAction := 0;                    // 动作完成
   LockEndFrame := TRUE;
end;

{ TActor.CleanCharMapSetting
  功能: 清除角色地图设置
  参数: x, y - 新位置坐标
  实现原理: 重置角色位置和动作状态 }
procedure TActor.CleanCharMapSetting (x, y: integer);
begin
   Myself.XX := x;
   Myself.YY := y;
   Myself.RX := x;
   Myself.RY := y;
   oldx := x;
   oldy := y;
   CurrentAction := 0;
   currentframe := -1;
   CleanUserMsgs;
end;

{ TActor.Say
  功能: 显示角色说话气泡
  参数: str - 说话内容
  实现原理: 
    1. 将字符串按最大宽度分行
    2. 记录每行内容和宽度 }
procedure TActor.Say (str: string);
var
   i, len, aline, n: integer;
   dline, temp: string;
   loop: Boolean;
const
   MAXWIDTH = 150;                        // 最大宽度150像素
begin
   SayTime := GetTickCount;               // 记录说话时间
   SayLineCount := 0;

   n := 0;
   loop := TRUE;
   while loop do begin
      temp := '';
      i := 1;
      len := Length (str);
      while TRUE do begin
         if i > len then begin
            loop := FALSE;
            break;
         end;
         if byte (str[i]) >= 128 then begin
            temp := temp + str[i];
            Inc (i);
            if i <= len then temp := temp + str[i]
            else begin
               loop := FALSE;
               break;
            end;
         end else
            temp := temp + str[i];

         aline := FrmMain.Canvas.TextWidth (temp);
         if aline > MAXWIDTH then begin
            Saying[n] := temp;
            SayWidths[n] := aline;
            Inc (SayLineCount);
            Inc (n);
            if n >= MAXSAY then begin
               loop := FALSE;
               break;
            end;
            str := Copy (str, i+1, Len-i);
            temp := '';
            break;
         end;
         Inc (i);
      end;
      if temp <> '' then begin
         if n < MAXWIDTH then begin
            Saying[n] := temp;
            SayWidths[n] := FrmMain.Canvas.TextWidth (temp);
            Inc (SayLineCount);
         end;
      end;
   end;
end;



{============================== NPCActor =============================}
{ TNpcActor类实现
  功能: NPC角色的特殊处理
  特点: NPC只有3个方向(0,1,2)，只有站立和交互动作 }

{ TNpcActor.CalcActorFrame
  功能: 计算NPC动作帧
  实现原理: NPC只有3个方向，只处理站立和交互动作 }
procedure TNpcActor.CalcActorFrame;
var
   pm: PTMonsterAction;
   haircount: integer;
begin
   BoUseMagic := FALSE;
   currentframe := -1;

   BodyOffset := GetNpcOffset (Appearance);  // 获取NPC图像偏移

   pm := RaceByPm (Race, Appearance);
   if pm = nil then exit;
   Dir := Dir mod 3;                      // NPC只有3个方向(0,1,2)
   case CurrentAction of
      SM_TURN:                            // 站立动作
         begin
            startframe := pm.ActStand.start + Dir * (pm.ActStand.frame + pm.ActStand.skip);
            endframe := startframe + pm.ActStand.frame - 1;
            frametime := pm.ActStand.ftime;
            starttime := GetTickCount;
            defframecount := pm.ActStand.frame;
            Shift (Dir, 0, 0, 1);
         end;
      SM_HIT:                             // 交互动作
         begin
            startframe := pm.ActAttack.start + Dir * (pm.ActAttack.frame + pm.ActAttack.skip);
            endframe := startframe + pm.ActAttack.frame - 1;
            frametime := pm.ActAttack.ftime;
            starttime := GetTickCount;
         end;
   end;
end;

{ TNpcActor.GetDefaultFrame
  功能: 获取NPC默认帧
  参数: wmode - 未使用
  返回值: 默认帧索引 }
function  TNpcActor.GetDefaultFrame (wmode: Boolean): integer;
var
   cf, dr: integer;
   pm: PTMonsterAction;
begin
   pm := RaceByPm (Race, Appearance);
   if pm = nil then exit;
   Dir := Dir mod 3;                      // NPC只有3个方向

   if currentdefframe < 0 then cf := 0
   else if currentdefframe >= pm.ActStand.frame then cf := 0
   else cf := currentdefframe;
   Result := pm.ActStand.start + Dir * (pm.ActStand.frame + pm.ActStand.skip) + cf;
end;

{ TNpcActor.LoadSurface
  功能: 加载NPC图像资源 }
procedure TNpcActor.LoadSurface;
begin
   case Race of
      50: begin                           // 商人NPC
            BodySurface := WNpcImg.GetCachedImage (BodyOffset + currentframe, px, py);
         end;
   end;
end;

{ TNpcActor.Run
  功能: NPC动作主循环 }
procedure TNpcActor.Run;
begin
   inherited Run;
end;


{============================== HUMActor =============================}
{ THumActor类实现
  功能: 人类角色的特殊处理
  特点: 支持多种动作、装备显示、魔法施放等 }
{-------------------------------}

{ THumActor.Create
  功能: 人类角色构造函数 }
constructor THumActor.Create;
begin
   inherited Create;
   HairSurface := nil;                    // 头发图像
   WeaponSurface := nil;                  // 武器图像
   BoWeaponEffect := FALSE;               // 武器特效开关
end;

{ THumActor.Destroy
  功能: 人类角色析构函数 }
destructor THumActor.Destroy;
begin
   inherited Destroy;
end;

{ THumActor.CalcActorFrame
  功能: 计算人类角色动作帧
  实现原理: 
    1. 提取外观特征(发型、衣服、武器)
    2. 计算各部位图像偏移
    3. 根据动作类型设置帧参数 }
procedure THumActor.CalcActorFrame;
var
   haircount: integer;
   meff: TMagicEff;   
begin
   BoUseMagic := FALSE;
   BoHitEffect := FALSE;
   currentframe := -1;
   // 提取外观特征
   hair   := HAIRfeature (Feature);       // 发型
   dress  := DRESSfeature (Feature);      // 衣服
   weapon := WEAPONfeature (Feature);     // 武器
   BodyOffset := HUMANFRAME * (dress);    // 身体图像偏移

   haircount := WHairImg.ImageCount div HUMANFRAME div 2;
   if hair > haircount-1 then hair := haircount-1;
   hair := hair * 2;
   if hair > 1 then
      HairOffset := HUMANFRAME * (hair + Sex)
   else HairOffset := -1;
   WeaponOffset := HUMANFRAME * weapon; //(weapon*2 + Sex);

   case CurrentAction of
      SM_TURN:
         begin
            startframe := HA.ActStand.start + Dir * (HA.ActStand.frame + HA.ActStand.skip);
            endframe := startframe + HA.ActStand.frame - 1;
            frametime := HA.ActStand.ftime;
            starttime := GetTickCount;
            defframecount := HA.ActStand.frame;
            Shift (Dir, 0, 0, endframe-startframe+1);
         end;
      SM_WALK,
      SM_BACKSTEP:
         begin
            startframe := HA.ActWalk.start + Dir * (HA.ActWalk.frame + HA.ActWalk.skip);
            endframe := startframe + HA.ActWalk.frame - 1;
            frametime := HA.ActWalk.ftime;
            starttime := GetTickCount;
            maxtick := HA.ActWalk.UseTick;
            curtick := 0;
            //WarMode := FALSE;
            movestep := 1;
            if CurrentAction = SM_BACKSTEP then
               Shift (GetBack(Dir), movestep, 0, endframe-startframe+1)
            else
               Shift (Dir, movestep, 0, endframe-startframe+1);
         end;
      SM_RUSH:
         begin
            if RushDir = 0 then begin
               RushDir := 1;
               startframe := HA.ActRushLeft.start + Dir * (HA.ActRushLeft.frame + HA.ActRushLeft.skip);
               endframe := startframe + HA.ActRushLeft.frame - 1;
               frametime := HA.ActRushLeft.ftime;
               starttime := GetTickCount;
               maxtick := HA.ActRushLeft.UseTick;
               curtick := 0;
               movestep := 1;
               Shift (Dir, 1, 0, endframe-startframe+1);
            end else begin
               RushDir := 0;
               startframe := HA.ActRushRight.start + Dir * (HA.ActRushRight.frame + HA.ActRushRight.skip);
               endframe := startframe + HA.ActRushRight.frame - 1;
               frametime := HA.ActRushRight.ftime;
               starttime := GetTickCount;
               maxtick := HA.ActRushRight.UseTick;
               curtick := 0;
               movestep := 1;
               Shift (Dir, 1, 0, endframe-startframe+1);
            end;
         end;
      SM_RUSHKUNG:
         begin
            startframe := HA.ActRun.start + Dir * (HA.ActRun.frame + HA.ActRun.skip);
            endframe := startframe + HA.ActRun.frame - 1;
            frametime := HA.ActRun.ftime;
            starttime := GetTickCount;
            maxtick := HA.ActRun.UseTick;
            curtick := 0;
            movestep := 1;
            Shift (Dir, movestep, 0, endframe-startframe+1);
         end;
      {SM_BACKSTEP:
         begin
            startframe := pm.ActWalk.start + (pm.ActWalk.frame - 1) + Dir * (pm.ActWalk.frame + pm.ActWalk.skip);
            endframe := startframe - (pm.ActWalk.frame - 1);
            frametime := pm.ActWalk.ftime;
            starttime := GetTickCount;
            maxtick := pm.ActWalk.UseTick;
            curtick := 0;
            movestep := 1;
            Shift (GetBack(Dir), movestep, 0, endframe-startframe+1);
         end;  }
      SM_SITDOWN:
         begin
            startframe := HA.ActSitdown.start + Dir * (HA.ActSitdown.frame + HA.ActSitdown.skip);
            endframe := startframe + HA.ActSitdown.frame - 1;
            frametime := HA.ActSitdown.ftime;
            starttime := GetTickCount;
         end;
      SM_RUN:
         begin
            startframe := HA.ActRun.start + Dir * (HA.ActRun.frame + HA.ActRun.skip);
            endframe := startframe + HA.ActRun.frame - 1;
            frametime := HA.ActRun.ftime;
            starttime := GetTickCount;
            maxtick := HA.ActRun.UseTick;
            curtick := 0;
            //WarMode := FALSE;
            if CurrentAction = SM_RUN then movestep := 2
            else movestep := 1;
            //movestep := 2;
            Shift (Dir, movestep, 0, endframe-startframe+1);
         end;
      SM_THROW:
         begin
            startframe := HA.ActHit.start + Dir * (HA.ActHit.frame + HA.ActHit.skip);
            endframe := startframe + HA.ActHit.frame - 1;
            frametime := HA.ActHit.ftime;
            starttime := GetTickCount;
            WarMode := TRUE;
            WarModeTime := GetTickCount;
            BoThrow := TRUE;
            Shift (Dir, 0, 0, 1);
         end;
      // 2003/03/15 신규무공
      SM_HIT, SM_POWERHIT, SM_LONGHIT, SM_WIDEHIT, SM_FIREHIT,SM_CROSSHIT:
         begin
//          DScreen.AddSysMsg (UserName +' ''s Current Action =' + IntToStr(CurrentAction));
            startframe := HA.ActHit.start + Dir * (HA.ActHit.frame + HA.ActHit.skip);
            endframe := startframe + HA.ActHit.frame - 1;
            frametime := HA.ActHit.ftime;
            starttime := GetTickCount;
            WarMode := TRUE;
            WarModeTime := GetTickCount;
            if (CurrentAction = SM_POWERHIT) then begin
               BoHitEffect := TRUE;
               MagLight := 2;
               HitEffectNumber := 1;
            end;
            if (CurrentAction = SM_LONGHIT) then begin
               BoHitEffect := TRUE;
               MagLight := 2;
               HitEffectNumber := 2;
            end;
            if (CurrentAction = SM_WIDEHIT) then begin
               BoHitEffect := TRUE;
               MagLight := 2;
               HitEffectNumber := 3;
            end;
            if (CurrentAction = SM_FIREHIT) then begin
               BoHitEffect := TRUE;
               MagLight := 2;
               HitEffectNumber := 4;
            end;
            // 2003/03/15 신규무공
            if (CurrentAction = SM_CROSSHIT) then begin
               BoHitEffect := TRUE;
               MagLight := 2;
               HitEffectNumber := 6;
            end;
            Shift (Dir, 0, 0, 1);
         end;
      SM_HEAVYHIT:
         begin
            startframe := HA.ActHeavyHit.start + Dir * (HA.ActHeavyHit.frame + HA.ActHeavyHit.skip);
            endframe := startframe + HA.ActHeavyHit.frame - 1;
            frametime := HA.ActHeavyHit.ftime;
            starttime := GetTickCount;
            WarMode := TRUE;
            WarModeTime := GetTickCount;
            Shift (Dir, 0, 0, 1);
         end;
      SM_BIGHIT:
         begin
            startframe := HA.ActBigHit.start + Dir * (HA.ActBigHit.frame + HA.ActBigHit.skip);
            endframe := startframe + HA.ActBigHit.frame - 1;
            frametime := HA.ActBigHit.ftime;
            starttime := GetTickCount;
            WarMode := TRUE;
            WarModeTime := GetTickCount;
            Shift (Dir, 0, 0, 1);
         end;
      SM_SPELL:
         begin
            startframe := HA.ActSpell.start + Dir * (HA.ActSpell.frame + HA.ActSpell.skip);
            endframe := startframe + HA.ActSpell.frame - 1;
            frametime := HA.ActSpell.ftime;
            starttime := GetTickCount;
            CurEffFrame := 0;
            BoUseMagic := TRUE;
            case CurMagic.EffectNumber of
               22: begin //삽퓰
                     MagLight := 4;  //뢰설화
                     SpellFrame := 10; //뢰설화는 10 프래임으로 변경
                  end;
               26: begin //탐기파연
                     MagLight := 2;
                     SpellFrame := 20;
                     frametime := frametime div 2;
                  end;
               35: begin //무극진기 PDS 2003-03-27
                     MagLight := 2;  //무극진기
                     SpellFrame := 15; //무극진기는 15 프래임으로 변경
                  end;
               43: begin //哥綾븟
                     MagLight := 2;
                     SpellFrame := 20;
                  end;
               else
               begin //.....  대회복술, 사자윤회, 빙설풍
                  MagLight := 2;
                  SpellFrame := DEFSPELLFRAME;
               end;
            end;
            WaitMagicRequest := GetTickCount;
            WarMode := TRUE;
            WarModeTime := GetTickCount;
            Shift (Dir, 0, 0, 1);
         end;
      (*SM_READYFIREHIT:
         begin
            startframe := HA.ActFireHitReady.start + Dir * (HA.ActFireHitReady.frame + HA.ActFireHitReady.skip);
            endframe := startframe + HA.ActFireHitReady.frame - 1;
            frametime := HA.ActFireHitReady.ftime;
            starttime := GetTickCount;

            BoHitEffect := TRUE;
            HitEffectNumber := 4;
            MagLight := 2;

            CurGlimmer := 0;
            MaxGlimmer := 6;

            WarMode := TRUE;
            WarModeTime := GetTickCount;
            Shift (Dir, 0, 0, 1);
         end; *)
      SM_STRUCK:
         begin
            startframe := HA.ActStruck.start + Dir * (HA.ActStruck.frame + HA.ActStruck.skip);
            endframe := startframe + HA.ActStruck.frame - 1;
            frametime := struckframetime; //HA.ActStruck.ftime;
            starttime := GetTickCount;
            Shift (Dir, 0, 0, 1);

            genanicounttime := GetTickCount;
            CurBubbleStruck := 0;
         end;
      SM_NOWDEATH:
         begin
            startframe := HA.ActDie.start + Dir * (HA.ActDie.frame + HA.ActDie.skip);
            endframe := startframe + HA.ActDie.frame - 1;
            frametime := HA.ActDie.ftime;
            starttime := GetTickCount;
         end;
   end;
end;

{ THumActor.DefaultMotion
  功能: 设置人类角色为默认姿态 }
procedure THumActor.DefaultMotion;
begin
   inherited DefaultMotion;
end;

{ THumActor.GetDefaultFrame
  功能: 获取人类角色默认帧
  参数: wmode - 是否战斗姿态
  返回值: 默认帧索引
  实现原理: 
    1. 死亡状态返回死亡帧
    2. 战斗姿态返回战斗站立帧
    3. 普通状态返回普通站立帧 }
function  THumActor.GetDefaultFrame (wmode: Boolean): integer;
var
   cf, dr: integer;
   pm: PTMonsterAction;
begin
   if Death then                          // 死亡状态
      Result := HA.ActDie.start + Dir * (HA.ActDie.frame + HA.ActDie.skip) + (HA.ActDie.frame - 1)
   else
   if wmode then begin                    // 战斗姿态
      Result := HA.ActWarMode.start + Dir * (HA.ActWarMode.frame + HA.ActWarMode.skip);
   end else begin                         // 普通站立
      defframecount := HA.ActStand.frame;
      if currentdefframe < 0 then cf := 0
      else if currentdefframe >= HA.ActStand.frame then cf := 0
      else cf := currentdefframe;
      Result := HA.ActStand.start + Dir * (HA.ActStand.frame + HA.ActStand.skip) + cf;
   end;
end;

{ THumActor.RunFrameAction
  功能: 执行帧动作
  参数: frame - 当前帧索引
  实现原理: 
    1. 重击动作第5帧时创建破碎特效
    2. 投掷动作第3帧时创建飞斧对象 }
procedure  THumActor.RunFrameAction (frame: integer);
var
   meff: TMapEffect;
   event: TClEvent;
   mfly: TFlyingAxe;
begin
   BoHideWeapon := FALSE;
   // 重击动作 - 第5帧时创建破碎特效
   if CurrentAction = SM_HEAVYHIT then begin
      if (frame = 5) and (BoDigFragment) then begin
         BoDigFragment := FALSE;
         meff := TMapEffect.Create (8 * Dir, 3, XX, YY);
         meff.ImgLib := WEffectImg;
         meff.NextFrameTime := 80;
         PlaySound (s_strike_stone);
         PlayScene.EffectList.Add (meff);
         event := EventMan.GetEvent (XX, YY, ET_PILESTONES);
         if event <> nil then
            event.EventParam := event.EventParam + 1;
      end;
   end;
   // 投掷动作 - 第3帧时创建飞斧
   if CurrentAction = SM_THROW then begin
      if (frame = 3) and (BoThrow) then begin
         BoThrow := FALSE;
         mfly := TFlyingAxe (PlayScene.NewFlyObject (self,
                          XX,
                          YY,
                          TargetX,
                          TargetY,
                          TargetRecog,
                          mtFlyAxe));
         if mfly <> nil then begin
            TFlyingAxe(mfly).ReadyFrame := 40;
            mfly.ImgLib := WMon3Img;
            mfly.FlyImageBase := FLYOMAAXEBASE;
         end;
      end;
      if frame >= 3 then
         BoHideWeapon := TRUE;            // 第3帧后隐藏武器
   end;
end;

{ THumActor.DoWeaponBreakEffect
  功能: 启动武器破碎特效 }
procedure  THumActor.DoWeaponBreakEffect;
begin
   BoWeaponEffect := TRUE;                // 开启武器特效
   CurWpEffect := 0;                      // 重置特效帧
end;

{ THumActor.Run
  功能: 人类角色动作主循环
  实现原理: 
    1. 处理通用动画效果
    2. 处理武器特效
    3. 处理动作帧进度和魔法施放 }
procedure  THumActor.Run;
   // 内部函数: 检查魔法是否超时
   function MagicTimeOut: Boolean;
   begin
      if self = Myself then begin
         Result := GetTickCount - WaitMagicRequest > 3000;  // 主角3秒超时
      end else
         Result := GetTickCount - WaitMagicRequest > 2000;  // 其他2秒超时
      if Result then
         CurMagic.ServerMagicCode := 0;
   end;
var
   prv: integer;
   frametimetime: longword;
   bofly: Boolean;
begin
   if GetTickCount - genanicounttime > 120 then begin //주술의막 등... 애니메이션 효과
      genanicounttime := GetTickCount;
      Inc (GenAniCount);
      if GenAniCount > 100000 then GenAniCount := 0;
      Inc (CurBubbleStruck);
   end;
   if BoWeaponEffect then begin  //무기 향상/부서짐 효과
      if GetTickCount - wpeffecttime > 120 then begin
         wpeffecttime := GetTickCount;
         Inc (CurWpEffect);
         if CurWpEffect >= MAXWPEFFECTFRAME then
            BoWeaponEffect := FALSE;
      end;
   end;

   if (CurrentAction = SM_WALK) or
      (CurrentAction = SM_BACKSTEP) or
      (CurrentAction = SM_RUN) or
      (CurrentAction = SM_RUSH) or
      (CurrentAction = SM_RUSHKUNG)
   then exit;

   msgmuch := FALSE;
   if self <> Myself then begin
      if MsgList.Count >= 2 then msgmuch := TRUE;
   end;

   //사운드 효과
   RunActSound (currentframe - startframe);
   RunFrameAction (currentframe - startframe);

   prv := currentframe;
   if CurrentAction <> 0 then begin
      if (currentframe < startframe) or (currentframe > endframe) then
         currentframe := startframe;

      if (self <> Myself) and (BoUseMagic) then begin
         frametimetime := Round(frametime / 1.8);
      end else begin
         if msgmuch then frametimetime := Round(frametime * 2 / 3)
         else frametimetime := frametime;
      end;

      if GetTickCount - starttime > frametimetime then begin
         if currentframe < endframe then begin

            //마법인 경우 서버의 신호를 받아, 성공/실패를 확인한후
            //마지막동작을 끝낸다.
            if BoUseMagic then begin
               if (CurEffFrame = SpellFrame-2) or (MagicTimeOut) then begin //기다림 끝
                  if (CurMagic.ServerMagicCode >= 0) or (MagicTimeOut) then begin //서버로 부터 받은 결과. 아직 안왔으면 기다림
                     Inc (currentframe);
                     Inc(CurEffFrame);
                     starttime := GetTickCount;
                  end;
               end else begin
                  if currentframe < endframe - 1 then Inc (currentframe);
                  Inc (CurEffFrame);
                  starttime := GetTickCount;
               end;
            end else begin
               Inc (currentframe);
               starttime := GetTickCount;
            end;

         end else begin
            if self = Myself then begin
               if FrmMain.ServerAcceptNextAction then begin
                  CurrentAction := 0;
                  BoUseMagic := FALSE;
               end;
            end else begin
               CurrentAction := 0; //동작 완료
               BoUseMagic := FALSE;
            end;
            BoHitEffect := FALSE;
         end;

         if BoUseMagic then begin
            if CurEffFrame = SpellFrame-1 then begin //마법 발사 시점
               //마법 발사
               if CurMagic.ServerMagicCode > 0 then begin
                  with CurMagic do
                     PlayScene.NewMagic (self,
                                      ServerMagicCode,
                                      EffectNumber,
                                      XX,
                                      YY,
                                      TargX,
                                      TargY,
                                      Target,
                                      EffectType,
                                      Recusion,
                                      AniTime,
                                      bofly);
                  if bofly then
                     PlaySound (magicfiresound)
                  else
                     PlaySound (magicexplosionsound);
               end;
               if self = Myself then
                  LatestSpellTime := GetTickCount;
               CurMagic.ServerMagicCode := 0;
            end;
         end;

      end;
      if Race = 0 then currentdefframe := 0
      else currentdefframe := -10;
      defframetime := GetTickCount;
   end else begin
      if GetTickCount - smoothmovetime > 200 then begin
         if GetTickCount - defframetime > 500 then begin
            defframetime := GetTickCount;
            Inc (currentdefframe);
            if currentdefframe >= defframecount then
               currentdefframe := 0;
         end;
         DefaultMotion;
      end;
   end;

   if prv <> currentframe then begin
      loadsurfacetime := GetTickCount;
      LoadSurface;
   end;

end;

{ THumActor.Light
  功能: 获取人类角色的光照等级
  返回值: 光照值
  实现原理: 施法或攻击特效时使用魔法光照 }
function   THumActor.Light: integer;
var
   l: integer;
begin
   l := ChrLight;
   if l < MagLight then begin
      if BoUseMagic or BoHitEffect then   // 施法或攻击特效时
         l := MagLight;                   // 使用魔法光照
   end;
   Result := l;
end;

{ THumActor.LoadSurface
  功能: 加载人类角色的图像资源
  实现原理: 分别加载身体、头发、武器图像 }
procedure  THumActor.LoadSurface;
begin
   BodySurface := WHumImg.GetCachedImage (BodyOffset + currentframe, px, py);  // 身体图像
   if HairOffset >= 0 then
      HairSurface := WHairImg.GetCachedImage (HairOffset + currentframe, hpx, hpy)  // 头发图像
   else HairSurface := nil;
   WeaponSurface := WWeapon.GetCachedImage (WeaponOffset + currentframe, wpx, wpy);  // 武器图像
end;

{ THumActor.DrawChr
  功能: 绘制人类角色
  参数: 
    dsurface - 目标表面
    dx, dy - 绘制位置
    blend - 是否混合绘制
  实现原理: 
    1. 根据WORDER数组确定武器绘制顺序
    2. 依次绘制武器(前)/身体/头发/武器(后)
    3. 绘制各种特效(防御罩/魔法/攻击/武器) }
procedure  THumActor.DrawChr (dsurface: TTexture; dx, dy: integer; blend: Boolean);
var
   idx, ax, ay: integer;
   d: TTexture;
   ceff: TColorEffect;
   wimg: TGameImages;
begin
   // 方向必须在0-7范围内
   if not (Dir in [0..7]) then exit;
   
   // 每60秒重新加载图像资源
   if GetTickCount - loadsurfacetime > 60 * 1000 then begin
      loadsurfacetime := GetTickCount;
      LoadSurface;
   end;
   
   // 获取颜色特效
   ceff := GetDrawEffectValue;

   // 人类角色绘制
   if Race = 0 then begin
      if (currentframe >= 0) and (currentframe <= 599) then
         wpord := WORDER[Sex, currentframe];  // 获取武器绘制顺序
         
      // 先绘制武器(当wpord=0时)
      if (wpord = 0) and (not blend) and (Weapon >= 2) and (WeaponSurface <> nil) and (not BoHideWeapon) then begin
         DrawEffSurface (dsurface, WeaponSurface, dx + wpx + ShiftX, dy + wpy + ShiftY, blend, ceNone);
         DrawWeaponGlimmer (dsurface, dx + ShiftX, dy + ShiftY);
      end;

      // 绘制身体
      if BodySurface <> nil then
         DrawEffSurface (dsurface, BodySurface, dx + px + ShiftX, dy + py + ShiftY, blend, ceff);

      // 绘制头发
      if HairSurface <> nil then
         DrawEffSurface (dsurface, HairSurface, dx + hpx + ShiftX, dy + hpy + ShiftY, blend, ceff);

      // 后绘制武器(当wpord=1时)
      if (wpord = 1) and (Weapon >= 2) and (WeaponSurface <> nil) and (not BoHideWeapon) then begin
         DrawEffSurface (dsurface, WeaponSurface, dx + wpx + ShiftX, dy + wpy + ShiftY, blend, ceNone);
         DrawWeaponGlimmer (dsurface, dx + ShiftX, dy + ShiftY);
      end;

      // 绘制防御罩特效
      if State and $00100000 <> 0 then begin
         if (CurrentAction = SM_STRUCK) and (CurBubbleStruck < 3) then
            idx := MAGBUBBLESTRUCKBASE + CurBubbleStruck
         else
            idx := MAGBUBBLEBASE + (GenAniCount mod 3);
         d := WMagic.GetCachedImage (idx, ax, ay);
         if d <> nil then
            DrawBlend (dsurface,
                             dx + ax + ShiftX,
                             dy + ay + ShiftY,
                             d);
      end;
   end;

   // 绘制魔法施放特效
   if BoUseMagic and (CurMagic.EffectNumber > 0) then begin
      if CurEffFrame in [0..SpellFrame-1] then begin
         GetEffectBase (CurMagic.EffectNumber-1, 0, wimg, idx);
         idx := idx + CurEffFrame;
         if wimg <> nil then
            d := wimg.GetCachedImage (idx, ax, ay);
         if d <> nil then
            DrawBlend (dsurface,
                             dx + ax + ShiftX,
                             dy + ay + ShiftY,
                             d);
      end;
   end;

   // 绘制攻击特效
   if BoHitEffect and (HitEffectNumber > 0) then begin
      GetEffectBase (HitEffectNumber-1, 1, wimg, idx);
      if (HitEffectNumber = 7) then       // 特殊攻击特效
         idx := idx + Dir*20 + (currentframe-startframe)
      else
         idx := idx + Dir*10 + (currentframe-startframe);
      d := wimg.GetCachedImage (idx, ax, ay);
      if d <> nil then
         DrawBlend (dsurface, dx + ax + ShiftX, dy + ay + ShiftY, d);

      // 特殊攻击特效的第二层
      if HitEffectNumber = 7 then begin
         GetEffectBase (HitEffectNumber-1, 1, wimg, idx);
         idx := idx+6 + Dir*20 + (currentframe-startframe);
         d := wimg.GetCachedImage (idx, ax, ay);
         if d <> nil then
            DrawBlend (dsurface,
                             dx + ax + ShiftX,
                             dy + ay + ShiftY,
                             d);
      end;
   end;

   // 绘制武器特效(强化/破碎)
   if BoWeaponEffect then begin
      idx := WPEFFECTBASE + Dir*10 + CurWpEffect;
      d := WMagic.GetCachedImage (idx, ax, ay);
      if d <> nil then
         DrawBlend (dsurface,
                     dx + ax + ShiftX,
                     dy + ay + ShiftY,
                     d);
   end;
end;

end.


