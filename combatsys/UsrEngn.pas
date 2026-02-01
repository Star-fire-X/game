{ ============================================================================
  单元名称: UsrEngn
  功能描述: 用户引擎单元 - 游戏服务器核心引擎
  
  主要功能:
  - 管理所有在线玩家的创建、运行、保存和销毁
  - 管理怪物的生成(Zen)、运行和销毁
  - 管理NPC和商人的初始化和运行
  - 处理玩家消息和游戏逻辑
  - 管理服务器间的玩家转移
  - 管理任务(Mission)系统
  - 管理物品、魔法等游戏数据
  
  作者: 传奇2开发团队
  创建日期: 2001
  修改日期: 2003/09/09
============================================================================ }
unit UsrEngn;

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Dialogs,
  ScktComp, syncobjs, MudUtil, HUtil32, ObjBase, Grobal2, EdCode,
  Envir, ObjMon, ObjMon2, ObjAxeMon, ObjNpc, ObjGuard, M2Share, RunDB,
  Guild, Mission, MFdbDef, InterServerMsg, InterMsgClient, Event;

type
   { TRefillCretInfo记录
     功能: 怪物重生信息记录结构
     用途: 存储怪物重生点的位置和数量信息 }
   TRefillCretInfo = record
      x:    integer;   // 重生点X坐标
      y:    integer;   // 重生点Y坐标
      size:  byte;     // 重生区域大小
      count: byte;     // 重生数量
      race:  byte;     // 怪物种族类型
   end;
   PTRefillCretInfo = ^TRefillCretInfo;  // 指向TRefillCretInfo的指针类型


   { TUserEngine类
     功能: 用户引擎 - 游戏服务器的核心管理类
     用途: 负责管理所有游戏对象(玩家、怪物、NPC)的生命周期和运行逻辑 }
   TUserEngine = class
   private
      ReadyList: TStringList;           // 准备登录的玩家列表(需要同步)
      RunUserList: TStringList;         // 正在运行的玩家列表
      OtherUserNameList: TStringList;   // 其他服务器上的用户列表
      ClosePlayers: TList;              // 待关闭的玩家列表
      SaveChangeOkList: TList;          // 保存变更完成的列表

      timer10min: longword;             // 10分钟定时器
      timer10sec: longword;             // 10秒定时器
      opendoorcheck: longword;          // 门状态检查定时器
      missiontime: longword;            // 任务定时器(每秒触发一次)
      onezentime: longword;             // 怪物生成定时器(分批生成)
      runonetime: longword;             // 单次运行时间记录
      hum200time: longword;             // 200毫秒玩家处理定时器
      usermgrcheck: longword;           // 用户管理检查定时器

      eventitemtime: longword;          // 唯一物品事件的时间变量

      GenCur: integer;                  // 当前怪物生成索引
      MonCur, MonSubCur: integer;       // 怪物处理游标和子游标
      HumCur, HumRotCount: integer;     // 玩家处理游标和轮转计数
      MerCur: integer;                  // 商人处理游标
      NpcCur: integer;                  // NPC处理游标

      { LoadRefillCretInfos
        功能: 加载怪物重生信息配置 }
      procedure LoadRefillCretInfos;
      
      { SendRefMsgEx
        功能: 向指定区域内的所有玩家发送消息
        参数:
          envir - 地图环境对象
          x, y - 中心坐标
          msg - 消息类型
          wparam - 字参数
          lParam1, lParam2, lParam3 - 长整型参数
          str - 字符串参数 }
      procedure SendRefMsgEx (envir: TEnvirnoment; x, y: integer; msg, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);
      
      { CheckOpenDoors
        功能: 检查并自动关闭超时的门 }
      procedure CheckOpenDoors;
   protected
      { ProcessUserHumans
        功能: 处理所有在线玩家的逻辑
        实现原理: 遍历玩家列表，执行玩家的运行逻辑、视野搜索、消息处理等 }
      procedure ProcessUserHumans;
      
      { ProcessMonsters
        功能: 处理所有怪物的逻辑
        实现原理: 遍历怪物列表，执行怪物AI、移动、攻击等逻辑 }
      procedure ProcessMonsters;
      
      { ProcessMerchants
        功能: 处理所有商人NPC的逻辑 }
      procedure ProcessMerchants;
      
      { ProcessNpcs
        功能: 处理所有普通NPC的逻辑 }
      procedure ProcessNpcs;
      
      { ProcessMissions
        功能: 处理所有任务的逻辑 }
      procedure ProcessMissions;
   public
      StdItemList: TList;       // 标准物品定义列表
      MonDefList: TList;        // 怪物定义列表
      MonList: TList;           // 怪物生成点(Zen)列表
      DefMagicList: TList;      // 魔法定义列表
      AdminList: TStringList;   // 管理员列表
      MerchantList: TList;      // 商人NPC列表
      NpcList: TList;           // 普通NPC列表
      MissionList: TList;       // 任务列表
      WaitServerList: TList;    // 等待服务器转移的玩家列表
      HolySeizeList: TList;     // 结界列表

      MonCount, MonCurCount, MonRunCount, MonCurRunCount: integer;  // 怪物统计计数
      BoUniqueItemEvent: Boolean;      // 是否启用唯一物品事件
      UniqueItemEventInterval: integer; // 唯一物品事件间隔时间
      // 2003/03/18 测试服务器人数限制
      FreeUserCount: integer;          // 免费用户数量

      constructor Create;      // 构造函数
      destructor Destroy; override;  // 析构函数
      
      { Initialize
        功能: 初始化用户引擎，加载怪物重生信息，初始化商人和NPC }
      procedure Initialize;
      
      { ExecuteRun
        功能: 执行引擎主循环，处理所有游戏对象的逻辑 }
      procedure ExecuteRun;
      
      { ProcessUserMessage
        功能: 处理玩家发送的消息
        参数:
          hum - 玩家对象
          pmsg - 消息结构指针
          pbody - 消息体内容 }
      procedure ProcessUserMessage (hum: TUserHuman; pmsg: PTDefaultMessage; pbody: PChar);
      
      //========== 标准物品相关方法 ==========
      { GetStdItemName: 根据物品索引获取物品名称 }
      function  GetStdItemName (itemindex: integer): string;
      { GetStdItemIndex: 根据物品名称获取物品索引 }
      function  GetStdItemIndex (itmname: string): integer;
      { GetStdItemWeight: 根据物品索引获取物品重量 }
      function  GetStdItemWeight (itemindex: integer): integer;
      { GetStdItem: 根据索引获取标准物品指针 }
      function  GetStdItem (index: integer): PTStdItem;
      { GetStdItemFromName: 根据名称获取标准物品指针 }
      function  GetStdItemFromName (itmname: string): PTStdItem;
      { CopyToUserItem: 根据物品索引创建用户物品 }
      function  CopyToUserItem (itmindex: integer; var uitem: TUserItem): Boolean;
      { CopyToUserItemFromName: 根据物品名称创建用户物品 }
      function  CopyToUserItemFromName (itmname: string; var uitem: TUserItem): Boolean;
      
      //========== 魔法定义相关方法 ==========
      { GetDefMagic: 根据魔法名称获取魔法定义 }
      function  GetDefMagic (magname: string): PTDefMagic;
      { GetDefMagicFromID: 根据魔法ID获取魔法定义 }
      function  GetDefMagicFromID (Id: integer): PTDefMagic;
      //========== 用户管理相关方法 ==========
      { AddNewUser: 添加新用户到准备列表(需要同步) }
      procedure AddNewUser (ui: PTUserOpenInfo);
      { ClosePlayer: 关闭玩家连接 }
      procedure ClosePlayer (hum: TUserHuman);
      { SavePlayer: 保存玩家数据 }
      procedure SavePlayer (hum: TUserHuman);
      { ChangeAndSaveOk: 变更并保存完成回调 }
      procedure ChangeAndSaveOk (pc: PTChangeUserInfo);
      { GetMyDegree: 获取用户权限等级 }
      function  GetMyDegree (uname: string): integer;
      { GetUserHuman: 根据名称获取在线玩家对象 }
      function  GetUserHuman (who: string): TUserHuman;
      { FindOtherServerUser: 查找是否在其他服务器上在线 }
      function  FindOtherServerUser (who: string; var svindex: integer): Boolean;
      { GetUserCount: 获取总用户数(包括其他服务器) }
      function  GetUserCount: integer;
      { GetRealUserCount: 获取本服务器实际用户数 }
      function  GetRealUserCount: integer;
      { GetAreaUserCount: 获取指定区域内的用户数量 }
      function  GetAreaUserCount (env: TEnvirnoment; x, y, wide: integer): integer;
      { GetAreaUsers: 获取指定区域内的用户列表 }
      function  GetAreaUsers (env: TEnvirnoment; x, y, wide: integer; ulist: TList): integer;
      { GetHumCount: 获取指定地图的玩家数量 }
      function  GetHumCount (mapname: string): integer;
      { CryCry: 向指定区域广播喊话消息 }
      procedure CryCry (msgtype: integer; env: TEnvirnoment; x, y, wide: integer; saying: string);
      { SysMsgAll: 向所有玩家发送系统消息 }
      procedure SysMsgAll (saying: string);
      { KickDoubleConnect: 踢出重复登录的用户 }
      procedure KickDoubleConnect (uname: string);
      { GuildMemberReLogin: 行会成员重新登录处理 }
      procedure GuildMemberReLogin (guild: TGuild);

      //========== 服务器转移相关方法 ==========
      { AddServerWaitUser: 添加等待服务器转移的用户 }
      function  AddServerWaitUser (psui: PTServerShiftUserInfo): Boolean;
      { GetServerShiftInfo: 获取服务器转移信息 }
      function  GetServerShiftInfo (uname: string; certify: integer): PTServerShiftUserInfo;
      { MakeServerShiftData: 创建服务器转移数据 }
      procedure MakeServerShiftData (hum: TUserHuman; var sui: TServerShiftUserInfo);
      { LoadServerShiftData: 加载服务器转移数据 }
      procedure LoadServerShiftData (psui: PTServerShiftUserInfo; var hum: TUserHuman);
      { ClearServerShiftData: 清除服务器转移数据 }
      procedure ClearServerShiftData (psui: PTServerShiftUserInfo);
      { WriteShiftUserData: 写入转移用户数据到文件 }
      function  WriteShiftUserData (psui: PTServerShiftUserInfo): string;
      { SendInterServerMsg: 发送服务器间消息 }
      procedure SendInterServerMsg (msgstr: string);
      { SendInterMsg: 发送服务器间消息(带标识和服务器索引) }
      procedure SendInterMsg (ident, svidx: integer; msgstr: string);
      { UserServerChange: 执行用户服务器转移 }
      function  UserServerChange (hum: TUserHuman; svindex: integer): Boolean;
      // 2003/06/12 从属怪物补丁
      { GetISMChangeServerReceive: 接收服务器转移确认 }
      procedure GetISMChangeServerReceive (flname: string);
      { DoUserChangeServer: 执行用户切换服务器 }
      function  DoUserChangeServer (hum: TUserHuman; svindex: integer): Boolean;
      { CheckServerWaitTimeOut: 检查服务器等待超时 }
      procedure CheckServerWaitTimeOut;
      { CheckHolySeizeValid: 检查结界是否有效 }
      procedure CheckHolySeizeValid;
      { OtherServerUserLogon: 其他服务器用户登录通知 }
      procedure OtherServerUserLogon (snum: integer; uname: string);
      { OtherServerUserLogout: 其他服务器用户登出通知 }
      procedure OtherServerUserLogout (snum: integer; uname: string);
      { AccountExpired: 账号过期处理 }
      procedure AccountExpired (uid: string);

      //========== 怪物和NPC相关方法 ==========
      { GetMonRace: 根据怪物名称获取怪物种族 }
      function  GetMonRace (monname: string): integer;
      { ApplyMonsterAbility: 应用怪物能力属性 }
      procedure ApplyMonsterAbility (cret: TCreature; monname: string);
      { RandomUpgradeItem: 随机升级物品属性 }
      procedure RandomUpgradeItem (pu: PTUserItem);
      { RandomSetUnknownItem: 随机设置未知物品属性 }
      procedure RandomSetUnknownItem (pu: PTUserItem);
      { GetUniqueEvnetItemName: 获取唯一事件物品名称 }
      function  GetUniqueEvnetItemName (var iname: string; var numb: integer): Boolean;
      { ReloadAllMonsterItems: 重新加载所有怪物掉落物品 }
      procedure ReloadAllMonsterItems;
      { MonGetRandomItems: 怪物获取随机掉落物品 }
      function  MonGetRandomItems (mon: TCreature): integer;
      { AddCreature: 在指定位置创建怪物 }
      function  AddCreature (map: string; x, y, race: integer; monname: string): TCreature;
      { AddCreatureSysop: 管理员命令创建怪物 }
      function  AddCreatureSysop (map: string; x, y: integer; monname: string): TCreature;
      { RegenMonsters: 重生怪物 }
      function  RegenMonsters (pz: PTZenInfo; zcount: integer): Boolean;
      { GetMonCount: 获取生成点的怪物数量 }
      function  GetMonCount (pz: PTZenInfo): integer;
      { GetGenCount: 获取地图上已生成的怪物数量 }
      function  GetGenCount (mapname: string): integer;
      { GetMapMons: 获取地图上的怪物列表 }
      function  GetMapMons (penvir: TEnvirnoment; list: TList): integer;
      
      //========== 商人NPC相关方法 ==========
      { GetMerchant: 根据NPC ID获取商人对象 }
      function  GetMerchant (npcid: integer): TCreature;
      { GetMerchantXY: 获取指定区域内的商人列表 }
      function  GetMerchantXY (envir: TEnvirnoment; x, y, wide: integer; npclist: TList): integer;
      { InitializeMerchants: 初始化所有商人 }
      procedure InitializeMerchants;
      { GetNpc: 根据NPC ID获取NPC对象 }
      function  GetNpc (npcid: integer): TCreature;
      { GetNpcXY: 获取指定区域内的NPC列表 }
      function  GetNpcXY (envir: TEnvirnoment; x, y, wide: integer; list: TList): integer;
      { InitializeNpcs: 初始化所有NPC }
      procedure InitializeNpcs;
      
      //========== 系统功能方法 ==========
      { OpenDoor: 打开门 }
      function  OpenDoor (envir: TEnvirnoment; dx, dy: integer): Boolean;
      { CloseDoor: 关闭门 }
      function  CloseDoor(envir: TEnvirnoment; pd: PTDoorInfo): Boolean;
      
      //========== 任务系统方法 ==========
      { LoadMission: 加载任务文件并激活任务 }
      function  LoadMission (flname: string): Boolean;
      { StopMission: 停止任务(自动结束) }
      function  StopMission (missionname: string): Boolean;
      
      //========== 起始位置方法 ==========
      { GetRandomDefStart: 获取随机默认起始位置 }
      procedure GetRandomDefStart (var map: string; var sx, sy: integer);
   end;

implementation

uses
   svMain, FrnEngn, IdSrvClient, LocalDB;

{ TUserEngine }

{ Create
  功能: TUserEngine构造函数
  实现原理:
    1. 创建所有内部列表对象
    2. 初始化所有定时器
    3. 初始化游标和计数器
    4. 设置唯一物品事件参数 }
constructor TUserEngine.Create;
begin
   // 创建用户管理相关列表
   RunUserList := TStringList.Create;        // 正在运行的用户列表
   OtherUserNameList := TStringList.Create;  // 其他服务器用户列表
   ClosePlayers := TList.Create;             // 待关闭玩家列表
   SaveChangeOkList := TList.Create;         // 保存变更完成列表

   // 创建怪物和物品相关列表
   MonList := TList.Create;                  // 怪物生成点列表
   MonDefList := TList.Create;               // 怪物定义列表
   ReadyList := TStringList.Create;          // 准备列表(需要同步)
   StdItemList := TList.Create;              // 标准物品列表(Index被TUserItem引用,顺序不能变更)
   DefMagicList := TList.Create;             // 魔法定义列表
   AdminList := TStringList.Create;          // 管理员列表
   MerchantList := TList.Create;             // 商人列表
   NpcList := TList.Create;                  // NPC列表
   MissionList := TList.Create;              // 任务列表
   WaitServerList := TList.Create;           // 等待服务器转移列表
   HolySeizeList := TList.Create;            // 结界列表

   // 初始化所有定时器为当前时间
   timer10min := GetTickCount;               // 10分钟定时器
   timer10sec := GetTickCount;               // 10秒定时器
   opendoorcheck := GetTickCount;            // 门检查定时器
   missiontime := GetTickCount;              // 任务定时器
   onezentime := GetTickCount;               // 怪物生成定时器
   hum200time := GetTickCount;               // 200毫秒玩家处理定时器
   usermgrcheck := GetTickCount;             // 用户管理检查定时器

   // 初始化游标为0
   GenCur := 0;                              // 怪物生成游标
   MonCur := 0;                              // 怪物处理游标
   MonSubCur := 0;                           // 怪物子游标
   HumCur := 0;                              // 玩家处理游标
   HumRotCount := 0;                         // 玩家轮转计数
   MerCur := 0;                              // 商人处理游标
   NpcCur := 0;                              // NPC处理游标
   // 2003/03/18 测试服务器人数限制
   FreeUserCount := 0;                       // 免费用户计数

   // 唯一物品事件设置
   BoUniqueItemEvent := FALSE;               // 默认不启用唯一物品事件
   UniqueItemEventInterval := 30 * 60 * 1000; // 事件间隔30分钟
   eventitemtime := GetTickCount;            // 事件时间初始化

   inherited Create;
end;

{ Destroy
  功能: TUserEngine析构函数
  实现原理:
    1. 释放所有用户对象
    2. 释放所有列表中的动态分配内存
    3. 释放所有列表对象 }
destructor TUserEngine.Destroy;
var
   i: integer;
begin
   // 释放所有运行中的用户对象
   for i:=0 to RunUserList.Count-1 do
      TUserHuman (RunUserList.Objects[i]).Free;
   RunUserList.Free;
   OtherUserNameList.Free;
   ClosePlayers.Free;
   SaveChangeOkList.Free;

   // 释放怪物生成点信息
   for i:=0 to MonList.Count-1 do
      Dispose (PTZenInfo (MonList[i]));
   MonList.Free;
   MonDefList.Free;
   
   // 释放魔法定义
   for i:=0 to DefMagicList.Count-1 do
      Dispose (PTDefMagic (DefMagicList[i]));
   DefMagicList.Free;
   ReadyList.Free;
   
   // 释放标准物品定义
   for i:=0 to StdItemList.Count-1 do
      Dispose (PTStdItem(StdItemList[i]));
   StdItemList.Free;

   // 释放其他列表
   AdminList.Free;
   MerchantList.Free;
   NpcList.Free;
   MissionList.Free;
   WaitServerList.Free;
   HolySeizeList.Free;
   inherited Destroy;
end;

{-------------------- 标准物品列表操作 ----------------------}

{ GetStdItemName
  功能: 根据物品索引获取物品名称
  参数:
    itemindex - 物品索引(从1开始,0表示空)
  返回值: 物品名称,如果索引无效则返回空字符串
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.GetStdItemName (itemindex: integer): string;
begin
   // TUserItem的Index是+1的值,0被视为空
   itemindex := itemindex - 1;
   if (itemindex >= 0) and (itemindex <= StdItemList.Count-1) then
      Result := PTStdItem (StdItemList[itemindex]).Name
   else Result := '';
end;

{ GetStdItemIndex
  功能: 根据物品名称获取物品索引
  参数:
    itmname - 物品名称
  返回值: 物品索引(从1开始),-1表示未找到
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.GetStdItemIndex (itmname: string): integer;
var
   i: integer;
begin
   Result := -1;
   if itmname = '' then exit;
   // 遍历物品列表查找匹配的名称
   for i:=0 to StdItemList.Count-1 do begin
      if CompareText(PTStdItem(StdItemList[i]).Name, itmname) = 0 then begin
         Result := i + 1;  // 返回索引+1
         break;
      end;
   end;
end;

{ GetStdItemWeight
  功能: 根据物品索引获取物品重量
  参数:
    itemindex - 物品索引(从1开始)
  返回值: 物品重量,如果索引无效则返回0
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.GetStdItemWeight (itemindex: integer): integer;
begin
   // TUserItem的Index是+1的值,0被视为空
   itemindex := itemindex - 1;
   if (itemindex >= 0) and (itemindex <= StdItemList.Count-1) then
      Result := PTStdItem (StdItemList[itemindex]).Weight
   else Result := 0;
end;

{ GetStdItem
  功能: 根据索引获取标准物品指针
  参数:
    index - 物品索引(从1开始)
  返回值: 标准物品指针,如果索引无效或物品名称为空则返回nil
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.GetStdItem (index: integer): PTStdItem;
begin
   index := index - 1;
   if (index >= 0) and (index < StdItemList.Count) then begin
      Result := PTStdItem(StdItemList[index]);
      // 名称为空的物品被视为已删除的物品
      if Result.Name = '' then Result := nil;
   end else Result := nil;
end;

{ GetStdItemFromName
  功能: 根据物品名称获取标准物品指针
  参数:
    itmname - 物品名称
  返回值: 标准物品指针,如果未找到则返回nil
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.GetStdItemFromName (itmname: string): PTStdItem;
var
   i: integer;
begin
   Result := nil;
   if itmname = '' then exit;
   // 遍历物品列表查找匹配的名称(不区分大小写)
   for i:=0 to StdItemList.Count-1 do begin
      if CompareText (PTStdItem(StdItemList[i]).Name, itmname) = 0 then begin
         Result := PTStdItem(StdItemList[i]);
         break;
      end;
   end;
end;

{ CopyToUserItem
  功能: 根据物品索引创建用户物品
  参数:
    itmindex - 物品索引(从1开始)
    uitem - 输出的用户物品结构
  返回值: 成功返回TRUE,失败返回FALSE
  实现原理:
    1. 设置物品索引
    2. 生成新的MakeIndex(服务器唯一标识)
    3. 设置耐久度为最大值
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.CopyToUserItem (itmindex: integer; var uitem: TUserItem): Boolean;
begin
   Result := FALSE;
   itmindex := itmindex - 1;
   if (itmindex >= 0) and (itmindex < StdItemList.Count) then begin
      uitem.Index := itmindex + 1;  // Index=0被视为空
      uitem.MakeIndex := GetItemServerIndex;  // 获取新的服务器物品索引
      uitem.Dura := PTStdItem(StdItemList[itmindex]).DuraMax;     // 当前耐久度
      uitem.DuraMax := PTStdItem(StdItemList[itmindex]).DuraMax;  // 最大耐久度
      Result := TRUE;
   end;
end;

{ CopyToUserItemFromName
  功能: 根据物品名称创建用户物品
  参数:
    itmname - 物品名称
    uitem - 输出的用户物品结构
  返回值: 成功返回TRUE,失败返回FALSE
  实现原理:
    1. 根据名称查找物品定义
    2. 清空用户物品结构
    3. 设置物品索引和MakeIndex
    4. 设置耐久度为最大值
  注意事项: 不能在其他线程中使用! }
function  TUserEngine.CopyToUserItemFromName (itmname: string; var uitem: TUserItem): Boolean;
var
   i: integer;
begin
   Result := FALSE;
   if itmname = '' then exit;
   // 遍历物品列表查找匹配的名称
   for i:=0 to StdItemList.Count-1 do begin
      if CompareText (PTStdItem(StdItemList[i]).Name, itmname) = 0 then begin
         FillChar(uitem, sizeof(TUserItem), #0);  // 清空结构
         uitem.Index := i + 1;  // Index=0被视为空
         uitem.MakeIndex := GetItemServerIndex;  // 发放新的MakeIndex
         uitem.Dura := PTStdItem(StdItemList[i]).DuraMax;     // 当前耐久度
         uitem.DuraMax := PTStdItem(StdItemList[i]).DuraMax;  // 最大耐久度
         Result := TRUE;
         break;
      end;
   end;
end;

{-------------------- 后台和系统功能 ----------------------}

{ SendRefMsgEx
  功能: 向指定区域内的所有玩家发送消息
  参数:
    envir - 地图环境对象
    x, y - 中心坐标
    msg - 消息类型
    wparam - 字参数
    lParam1, lParam2, lParam3 - 长整型参数
    str - 字符串参数
  实现原理:
    1. 计算以(x,y)为中心的搜索范围(24x24)
    2. 遍历范围内的所有地图格子
    3. 查找每个格子上的移动对象
    4. 如果是玩家则发送消息 }
procedure  TUserEngine.SendRefMsgEx (envir: TEnvirnoment; x, y: integer; msg, wparam: Word; lParam1, lParam2, lParam3: Longint; str: string);
var
	i, j, k, stx, sty, enx, eny: integer;
   cret: TCreature;
   pm: PTMapInfo;
   inrange: Boolean;
begin
   // 计算搜索范围: 中心点周围半径12的正方形区域
   stx := x-12;
   enx := x+12;
   sty := y-12;
   eny := y+12;
   // 遍历范围内的所有坐标
   for i:=stx to enx do
      for j:=sty to eny do begin
         inrange := envir.GetMapXY (i, j, pm);
         if inrange then
            if pm.ObjList <> nil then
               // 遍历该坐标上的所有对象
               for k:=0 to pm.ObjList.Count-1 do begin
                  // 检查是否为生物对象
                  if pm.ObjList[k] <> nil then begin
                     if PTAThing (pm.ObjList[k]).Shape = OS_MOVINGOBJECT then begin
                        cret := TCreature (PTAThing (pm.ObjList[k]).AObject);
                        if cret <> nil then
                           if (not cret.BoGhost) then begin
                              // 只向玩家发送消息
                              if cret.RaceServer = RC_USERHUMAN then
                                 cret.SendMsg (cret, msg, wparam, lparam1, lparam2, lparam3, str);
                           end;
                     end;
                  end;
               end;
      end;
end;

{ OpenDoor
  功能: 打开指定位置的门
  参数:
    envir - 地图环境对象
    dx, dy - 门的坐标
  返回值: 成功打开返回TRUE,否则返回FALSE
  实现原理:
    1. 查找指定位置的门
    2. 检查门是否已打开或已锁定
    3. 设置门的打开状态和打开时间
    4. 向周围玩家发送门打开消息 }
function  TUserEngine.OpenDoor (envir: TEnvirnoment; dx, dy: integer): Boolean;
var
   pd: PTDoorInfo;
begin
   Result := FALSE;
   pd := envir.FindDoor (dx, dy);
   if pd <> nil then begin
      // 检查门是否已打开或已锁定
      if (not pd.pCore.DoorOpenState) and (not pd.pCore.Lock) then begin
         pd.pCore.DoorOpenState := TRUE;      // 设置为打开状态
         pd.pCore.OpenTime := GetTickCount;   // 记录打开时间
         // 向周围玩家发送门打开消息
         SendRefMsgEx (envir, dx, dy, RM_OPENDOOR_OK, 0, dx, dy, 0, '');
         Result := TRUE;
      end;
   end;
end;

{ CloseDoor
  功能: 关闭指定的门
  参数:
    envir - 地图环境对象
    pd - 门信息指针
  返回值: 成功关闭返回TRUE,否则返回FALSE
  实现原理:
    1. 检查门是否已打开
    2. 设置门为关闭状态
    3. 向周围玩家发送门关闭消息 }
function  TUserEngine.CloseDoor (envir: TEnvirnoment; pd: PTDoorInfo): Boolean;
begin
   Result := FALSE;
   if pd <> nil then begin
      if pd.pCore.DoorOpenState then begin
         pd.pCore.DoorOpenState := FALSE;  // 设置为关闭状态
         // 向周围玩家发送门关闭消息
         SendRefMsgEx (envir, pd.DoorX, pd.DoorY, RM_CLOSEDOOR, 0, pd.DoorX, pd.DoorY, 0, '');
         Result := TRUE;
      end;
   end;
end;

{ CheckOpenDoors
  功能: 检查并自动关闭超时的门
  实现原理:
    1. 遍历所有地图环境
    2. 检查每个地图上的所有门
    3. 如果门已打开超过5秒则自动关闭 }
procedure TUserEngine.CheckOpenDoors;
var
   k, i: integer;
   pd: PTDoorInfo;
   e: TEnvirnoment;
begin
   // 遍历所有地图环境
   for k:=0 to GrobalEnvir.Count-1 do begin
      // 遍历该地图上的所有门
      for i:=0 to TEnvirnoment(GrobalEnvir[k]).DoorList.Count-1 do begin
         e := TEnvirnoment(GrobalEnvir[k]);
         if PTDoorInfo(e.DoorList[i]).pCore.DoorOpenState then begin
            pd := PTDoorInfo(e.DoorList[i]);
            // 如果门打开超过5秒则自动关闭
            if GetTickCount - pd.pCore.OpenTime > 5000 then
               CloseDoor (e, pd);
         end;
      end;
   end;
end;


{-------------------- NPC和怪物管理 ----------------------}

{ LoadRefillCretInfos
  功能: 加载怪物重生信息配置
  注意事项: 当前为空实现,预留用于未来扩展 }
procedure TUserEngine.LoadRefillCretInfos;
begin
   // 预留的空实现
end;

{ GetMerchant
  功能: 根据NPC ID获取商人对象
  参数:
    npcid - NPC的对象指针值(作为ID使用)
  返回值: 商人对象,如果未找到则返回nil }
function  TUserEngine.GetMerchant (npcid: integer): TCreature;
var
   i: integer;
begin
   Result := nil;
   // 遍历商人列表查找匹配的ID
   for i:=0 to MerchantList.Count-1 do begin
      if Integer(MerchantList[i]) = npcid then begin
         Result := TCreature(MerchantList[i]);
         break;
      end;
   end;
end;

{ GetMerchantXY
  功能: 获取指定区域内的商人列表
  参数:
    envir - 地图环境对象
    x, y - 中心坐标
    wide - 搜索范围
    npclist - 输出的商人列表
  返回值: 找到的商人数量 }
function  TUserEngine.GetMerchantXY (envir: TEnvirnoment; x, y, wide: integer; npclist: TList): integer;
var
   i: integer;
begin
   // 遍历商人列表,查找在指定范围内的商人
   for i:=0 to MerchantList.Count-1 do begin
      if (TCreature(MerchantList[i]).PEnvir = envir) and
         (abs(TCreature(MerchantList[i]).CX - x) <= wide) and
         (abs(TCreature(MerchantList[i]).CY - y) <= wide)
      then begin
         npclist.Add (MerchantList[i]);
      end;
   end;
   Result := npclist.Count;
end;

{ InitializeMerchants
  功能: 初始化所有商人NPC
  实现原理:
    1. 遍历商人列表
    2. 为每个商人设置地图环境
    3. 调用商人的初始化方法
    4. 加载商人信息和市场商品
    5. 初始化失败的商人会被删除 }
procedure TUserEngine.InitializeMerchants;
var
   i: integer;
   m: TMerchant;
   frmcap: string;
begin
   frmcap := FrmMain.Caption;  // 保存原始窗口标题

   // 倒序遍历商人列表(方便删除操作)
   for i:=MerchantList.Count-1 downto 0 do begin
      m := TMerchant (MerchantList[i]);
      m.Penvir := GrobalEnvir.GetEnvir (m.MapName);  // 获取地图环境
      if m.Penvir <> nil then begin
         m.Initialize;  // 初始化商人
         if m.ErrorOnInit then begin
            // 初始化失败,删除商人
            MainOutMessage ('Merchant Initalize fail... ' + m.UserName);
            m.Free;
            MerchantList.Delete (i);
         end else begin
            // 加载商人信息和市场商品
            m.LoadMerchantInfos;
            m.LoadMarketSavedGoods;
         end;
      end else begin
         // 地图环境不存在,删除商人
         MainOutMessage ('Merchant Initalize fail... (m.PEnvir=nil) ' + m.UserName);
         m.Free;
         MerchantList.Delete (i);
      end;

      // 更新窗口标题显示加载进度
      FrmMain.Caption := 'Merchant Loading.. ' + IntToStr(MerchantList.Count-i+1) + '/' + IntToStr(MerchantList.Count);
      FrmMain.RefreshForm;
   end;

   FrmMain.Caption := frmcap;  // 恢复原始窗口标题
end;

{ GetNpc
  功能: 根据NPC ID获取NPC对象
  参数:
    npcid - NPC的对象指针值(作为ID使用)
  返回值: NPC对象,如果未找到则返回nil }
function  TUserEngine.GetNpc (npcid: integer): TCreature;
var
   i: integer;
begin
   Result := nil;
   // 遍历NPC列表查找匹配的ID
   for i:=0 to NpcList.Count-1 do begin
      if Integer(NpcList[i]) = npcid then begin
         Result := TCreature(NpcList[i]);
         break;
      end;
   end;
end;

{ GetNpcXY
  功能: 获取指定区域内的NPC列表
  参数:
    envir - 地图环境对象
    x, y - 中心坐标
    wide - 搜索范围
    list - 输出的NPC列表
  返回值: 找到的NPC数量 }
function  TUserEngine.GetNpcXY (envir: TEnvirnoment; x, y, wide: integer; list: TList): integer;
var
   i: integer;
begin
   // 遍历NPC列表,查找在指定范围内的NPC
   for i:=0 to NpcList.Count-1 do begin
      if (TCreature(NpcList[i]).PEnvir = envir) and
         (abs(TCreature(NpcList[i]).CX - x) <= wide) and
         (abs(TCreature(NpcList[i]).CY - y) <= wide)
      then begin
         list.Add (NpcList[i]);
      end;
   end;
   Result := list.Count;
end;

{ InitializeNpcs
  功能: 初始化所有普通NPC
  实现原理:
    1. 遍历NPC列表
    2. 为每个NPC设置地图环境
    3. 调用NPC的初始化方法
    4. 加载NPC信息
    5. 初始化失败的NPC会被删除(隐藏NPC除外) }
procedure TUserEngine.InitializeNpcs;
var
   i: integer;
   npc: TNormNpc;
   frmcap: string;
begin
   frmcap := FrmMain.Caption;  // 保存原始窗口标题

   // 倒序遍历NPC列表(方便删除操作)
   for i:=NpcList.Count-1 downto 0 do begin
      npc := TNormNpc (NpcList[i]);
      npc.Penvir := GrobalEnvir.GetEnvir (npc.MapName);  // 获取地图环境
      if npc.Penvir <> nil then begin
         npc.Initialize;  // 初始化NPC
         // 初始化失败且不是隐藏NPC则删除
         if npc.ErrorOnInit and not npc.BoInvisible then begin
            MainOutMessage ('Npc Initalize fail... ' + npc.UserName);
            npc.Free;
            NpcList.Delete (i);
         end else begin
            npc.LoadNpcInfos;  // 加载NPC信息
         end;
      end else begin
         // 地图环境不存在,删除NPC
         MainOutMessage ('Npc Initalize fail... (npc.PEnvir=nil) ' + npc.UserName);
         npc.Free;
         NpcList.Delete (i);
      end;

      // 更新窗口标题显示加载进度
      FrmMain.Caption := 'Npc loading.. ' + IntToStr(NpcList.Count - i+1) + '/' + IntToStr(NpcList.Count);
      FrmMain.RefreshForm;
   end;

   FrmMain.Caption := frmcap;  // 恢复原始窗口标题
end;

{ GetMonRace
  功能: 根据怪物名称获取怪物种族类型
  参数:
    monname - 怪物名称
  返回值: 怪物种族类型,-1表示未找到 }
function  TUserEngine.GetMonRace (monname: string): integer;
var
   i: integer;
begin
   Result := -1;
   // 遍历怪物定义列表查找匹配的名称
   for i:=0 to MonDefList.Count-1 do begin
      if CompareText (PTMonsterInfo(MonDefList[i]).Name, monname) = 0 then begin
         Result := PTMonsterInfo(MonDefList[i]).Race;
         break;
      end;
   end;
end;

{ ApplyMonsterAbility
  功能: 应用怪物能力属性到生物对象
  参数:
    cret - 生物对象
    monname - 怪物名称
  实现原理:
    1. 根据怪物名称查找怪物定义
    2. 将怪物定义中的属性复制到生物对象 }
procedure TUserEngine.ApplyMonsterAbility (cret: TCreature; monname: string);
var
   i: integer;
   pm: PTMonsterInfo;
begin
   // 遍历怪物定义列表查找匹配的名称
   for i:=0 to MonDefList.Count-1 do begin
      if CompareText (PTMonsterInfo(MonDefList[i]).Name, monname) = 0 then begin
         pm := PTMonsterInfo(MonDefList[i]);
         // 设置基本属性
         cret.RaceServer := pm.Race;        // 服务器种族类型
         cret.RaceImage := pm.RaceImg;      // 图像种族类型
         cret.Appearance := pm.Appr;        // 外观
         cret.Abil.Level := pm.Level;       // 等级
         cret.LifeAttrib := pm.LifeAttrib;  // 生命属性
         cret.CoolEye := pm.CoolEye;        // 透视能力
         cret.FightExp := pm.Exp;           // 经验值
         // 设置HP/MP
         cret.Abil.HP := pm.HP;
         cret.Abil.MaxHP := pm.HP;
         cret.Abil.MP := pm.MP;
         cret.Abil.MaxMP := pm.MP;
         // 设置攻击/防御属性
         cret.Abil.AC := makeword(pm.AC, pm.AC);      // 物理防御
         cret.Abil.MAC := makeword(pm.MAC, pm.MAC);   // 魔法防御
         cret.Abil.DC := makeword(pm.DC, pm.MaxDC);   // 物理攻击
         cret.Abil.MC := makeword(pm.MC, pm.MC);      // 魔法攻击
         cret.Abil.SC := makeword(pm.SC, pm.SC);      // 道术攻击
         cret.SpeedPoint := pm.Speed;       // 速度
         cret.AccuracyPoint := pm.Hit;      // 命中率
         // 设置行动参数
         cret.NextWalkTime := pm.WalkSpeed;   // 行走速度
         cret.WalkStep := pm.WalkStep;        // 行走步数
         cret.WalkWaitTime := pm.WalkWait;    // 行走等待时间
         cret.NextHitTime := pm.AttackSpeed;  // 攻击速度
         break;
      end;
   end;
end;

{ RandomUpgradeItem
  功能: 随机升级物品属性
  参数:
    pu - 用户物品指针
  实现原理:
    根据物品类型(StdMode)调用不同的随机升级方法 }
procedure TUserEngine.RandomUpgradeItem (pu: PTUserItem);
var
   pstd: PTStdItem;
begin
   pstd := GetStdItem (pu.Index);
   if pstd <> nil then begin
      case pstd.StdMode of
         5, 6:       // 武器
            ItemMan.UpgradeRandomWeapon (pu);
         10, 11:     // 男装/女装
            ItemMan.UpgradeRandomDress (pu);
         19:         // 项链(魔法闪避,幸运)
            ItemMan.UpgradeRandomNecklace19 (pu);
         20, 21, 24: // 项链/手镯
            ItemMan.UpgradeRandomNecklace (pu);
         26:         // 手镯
            ItemMan.UpgradeRandomBarcelet (pu);
         22:         // 戒指
            ItemMan.UpgradeRandomRings (pu);
         23:         // 戒指
            ItemMan.UpgradeRandomRings23 (pu);
         15:         // 头盔
            ItemMan.UpgradeRandomHelmet (pu);
      end;
   end;
end;

{ RandomSetUnknownItem
  功能: 随机设置未知物品的属性
  参数:
    pu - 用户物品指针
  实现原理:
    根据物品类型(StdMode)调用不同的随机设置方法
  注意事项:
    未知系列物品在被鉴定前属性是随机的 }
procedure  TUserEngine.RandomSetUnknownItem (pu: PTUserItem);
var
   pstd: PTStdItem;
begin
   pstd := GetStdItem (pu.Index);
   if pstd <> nil then begin
      case pstd.StdMode of
         15:     // 头盔
            ItemMan.RandomSetUnknownHelmet (pu);
         22,23:  // 戒指
            ItemMan.RandomSetUnknownRing (pu);
         24,26:  // 手镯
            ItemMan.RandomSetUnknownBracelet (pu);
      end;
   end;
end;

{ GetUniqueEvnetItemName
  功能: 获取唯一事件物品名称
  参数:
    iname - 输出的物品名称
    numb - 输出的物品数量
  返回值: 成功获取返回TRUE,否则返回FALSE
  实现原理:
    1. 检查是否到达事件间隔时间
    2. 从事件物品列表中随机选取一个物品
    3. 从列表中删除已选取的物品 }
function  TUserEngine.GetUniqueEvnetItemName (var iname: string; var numb: integer): Boolean;
var
   n: integer;
begin
   Result := FALSE;
   // 检查是否到达事件间隔时间且事件物品列表不为空
   if (GetTickCount - eventitemtime > longword(UniqueItemEventInterval)) and (EventItemList.Count > 0) then begin
      eventitemtime := GetTickCount;  // 更新事件时间
      n := Random(EventItemList.Count);  // 随机选取索引
      iname := EventItemList[n];         // 获取物品名称
      numb := Integer(EventItemList.Objects[n]);  // 获取物品数量
      EventItemList.Delete(n);           // 从列表中删除
      Result := TRUE;
   end;
end;

{ ReloadAllMonsterItems
  功能: 重新加载所有怪物的掉落物品配置
  实现原理:
    遍历怪物定义列表,为每个怪物重新加载掉落物品配置 }
procedure TUserEngine.ReloadAllMonsterItems;
var
   i: integer;
   list: TList;
begin
   list := nil;
   // 遍历怪物定义列表,重新加载每个怪物的掉落物品
   for i:=0 to MonDefList.Count-1 do begin
      FrmDB.LoadMonItems (PTMonsterInfo(MonDefList[i]).Name, PTMonsterInfo(MonDefList[i]).Itemlist);
   end;
end;

{ MonGetRandomItems
  功能: 为怪物生成随机掉落物品
  参数:
    mon - 怪物对象
  返回值: 始终返回1
  实现原理:
    1. 根据怪物名称查找掉落物品列表
    2. 遍历掉落物品列表,根据概率决定是否掉落
    3. 如果是金币则增加怪物金币
    4. 否则创建物品并添加到怪物的物品列表
    5. 物品有一定概率被随机升级
    6. 未知系列物品会被随机设置属性 }
function  TUserEngine.MonGetRandomItems (mon: TCreature): integer;
var
   i, numb: integer;
   list: TList;
   iname: string;
   pmi: PTMonItemInfo;
   pu: PTUserItem;
   pstd: PTStdItem;
begin
   list := nil;
   // 根据怪物名称查找掉落物品列表
   for i:=0 to MonDefList.Count-1 do begin
      if CompareText (PTMonsterInfo(MonDefList[i]).Name, mon.UserName) = 0 then begin
         list := PTMonsterInfo(MonDefList[i]).Itemlist;
         break;
      end;
   end;
   if list <> nil then begin
      // 遍历掉落物品列表
      for i:=0 to list.Count-1 do begin
         pmi := PTMonItemInfo(list[i]);
         // 根据概率决定是否掉落
         if pmi.SelPoint >= Random(pmi.MaxPoint) then begin
            // 检查是否为金币
            if CompareText(pmi.ItemName, '') = 0 then begin
               // 增加怪物金币
               mon.Gold := mon.Gold + (pmi.Count div 2) + Random(pmi.Count);
            end else begin
               // 唯一物品事件处理(当前被注释掉)
               iname := '';
               ////if (BoUniqueItemEvent) and (not mon.BoAnimal) then begin
               ////   if GetUniqueEvnetItemName (iname, numb) then begin
                     //numb; //iname
               ////   end;
               ////end;
               if iname = '' then
                  iname := pmi.ItemName;

               // 创建用户物品
               new (pu);
               if CopyToUserItemFromName (iname, pu^) then begin
                  // 设置随机耐久度(20%-100%)
                  pu.Dura := Round ((pu.DuraMax / 100) * (20+Random(80)));

                  pstd := GetStdItem (pu.Index);
                  ////if pstd <> nil then
                  ////   if pstd.StdMode = 50 then begin  // 商品券
                  ////      pu.Dura := numb;
                  ////   end;

                  // 10%概率随机升级物品属性
                  if Random(10) = 0 then
                     RandomUpgradeItem (pu);

                  // 未知系列物品处理
                  if pstd.StdMode in [15,19,20,21,22,23,24,26,52,53,54] then begin
                     if (pstd.Shape = RING_OF_UNKNOWN) or
                        (pstd.Shape = BRACELET_OF_UNKNOWN) or
                        (pstd.Shape = HELMET_OF_UNKNOWN)
                     then begin
                        // 随机设置未知物品属性
                        UserEngine.RandomSetUnknownItem (pu);
                     end;
                  end;

                  // 添加到怪物的物品列表
                  mon.ItemList.Add (pu)
               end else
                  Dispose (pu);  // 创建失败,释放内存
            end;
         end;
      end;
   end;
   Result := 1;
end;

{ AddCreature
  功能: 在指定位置创建怪物
  参数:
    map - 地图名称
    x, y - 创建坐标
    race - 怪物种族类型
    monname - 怪物名称
  返回值: 创建的怪物对象,失败返回nil
  实现原理:
    1. 根据种族类型创建对应的怪物对象
    2. 应用怪物能力属性
    3. 设置怪物位置和方向
    4. 生成怪物掉落物品
    5. 初始化怪物并添加到地图 }
function  TUserEngine.AddCreature (map: string; x, y, race: integer; monname: string): TCreature;
var
   env: TEnvirnoment;
   cret: TCreature;
   i, stepx, edge: integer;
   outofrange: pointer;
begin
   Result := nil;
   cret := nil;
   env := GrobalEnvir.GetEnvir (map);  // 获取地图环境
   if env = nil then exit;

   // 根据怪物种族类型创建对应的怪物对象
   case race of
      RC_DOORGUARD:
         begin
            cret := TSuperGuard.Create;
         end;
      RC_ANIMAL+1:  // 鸡
         begin
            cret := TMonster.Create;
            cret.BoAnimal := TRUE;
            cret.MeatQuality := 3000 + Random(3500); // 默认值
            cret.BodyLeathery := 50; // 默认值
         end;
      RC_DEER:  // 鹿
         begin
            if Random(30) = 0 then begin
               cret := TChickenDeer.Create; // 胆小的鹿,会逃跑
               cret.BoAnimal := TRUE;
               cret.MeatQuality := 10000 + Random(20000);
               cret.BodyLeathery := 150; // 默认值
            end else begin
               cret := TMonster.Create;
               cret.BoAnimal := TRUE;
               cret.MeatQuality := 8000 + Random(8000); // 默认值
               cret.BodyLeathery := 150; // 默认值
            end;
         end;
      RC_WOLF:
         begin
            cret := TATMonster.Create;
            cret.BoAnimal := TRUE;
            cret.MeatQuality := 8000 + Random(8000); // 默认值
            cret.BodyLeathery := 150; // 默认值
         end;
      RC_TRAINER:  // 训练师
         begin
            cret := TTrainer.Create;
            cret.RaceServer := RC_TRAINER;
         end;
      RC_MONSTER:
         begin
            cret := TMonster.Create;
         end;
      RC_OMA:
         begin
            cret := TATMonster.Create;
         end;
      RC_BLACKPIG:
         begin
            cret := TATMonster.Create;
            if Random(2) = 0 then cret.BoFearFire := TRUE;
         end;
      RC_SPITSPIDER:
         begin
            cret := TSpitSpider.Create;
         end;
      RC_SLOWMONSTER:
         begin
            cret := TSlowATMonster.Create;
         end;
      RC_SCORPION:  // 蝎子
         begin
            cret := TScorpion.Create;
         end;
      RC_KILLINGHERB:
         begin
            cret := TStickMonster.Create;
         end;
      RC_SKELETON: // 骷髅
         begin
            cret := TATMonster.Create;
         end;
      RC_DUALAXESKELETON: // 双斧骷髅
         begin
            cret := TDualAxeMonster.Create;
         end;
      RC_HEAVYAXESKELETON: // 大斧骷髅
         begin
            cret := TATMonster.Create;
         end;
      RC_KNIGHTSKELETON: // 骷髅战士
         begin
            cret := TATMonster.Create;
         end;
      RC_BIGKUDEKI: // 大型蛆虫
         begin
            cret := TGasAttackMonster.Create;
         end;

      RC_COWMON:  // 牛面鬼
         begin
            cret := TCowMonster.Create;
            if Random(2) = 0 then cret.BoFearFire := TRUE;
         end;
      RC_MAGCOWFACEMON:
         begin
            cret := TMagCowMonster.Create;
         end;
      RC_COWFACEKINGMON:
         begin
            cret := TCowKingMonster.Create;
         end;

      RC_THORNDARK:
         begin
            cret := TThornDarkMonster.Create;
         end;

      RC_LIGHTINGZOMBI:
         begin
            cret := TLightingZombi.Create;
         end;

      RC_DIGOUTZOMBI:
         begin
            cret := TDigOutZombi.Create;
            if Random(2) = 0 then cret.BoFearFire := TRUE;
         end;

      RC_ZILKINZOMBI:
         begin
            cret := TZilKinZombi.Create;
            if Random(4) = 0 then cret.BoFearFire := TRUE;
         end;

      RC_WHITESKELETON:
         begin
            cret := TWhiteSkeleton.Create; // 召唤白骷
         end;

      RC_SCULTUREMON:
         begin
            cret := TScultureMonster.Create;
            cret.BoFearFire := TRUE;
         end;

      RC_SCULKING:
         begin
            cret := TScultureKingMonster.Create;
         end;
      RC_SCULKING_2:
         begin
            cret := TScultureKingMonster.Create;
            TScultureKingMonster(cret).BoCallFollower := FALSE;
         end;

      RC_BEEQUEEN:
         begin
            cret := TBeeQueen.Create;   // 蜂箱
         end;

      RC_ARCHERMON:
         begin
            cret := TArcherMonster.Create; // 魔弓手
         end;

      RC_GASMOTH:  // 喷毒比蝉
         begin
            cret := TGasMothMonster.Create;
         end;

      RC_DUNG:    // 麻痹毒气怪
         begin
            cret := TGasDungMonster.Create;
         end;

      RC_CENTIPEDEKING:  // 触龙神,蚣蚓王
         begin
            cret := TCentipedeKingMonster.Create;
         end;

      RC_BIGHEARTMON:
         begin
            cret := TBigHeartMonster.Create;  // 血巨王,心脏
         end;

      RC_BAMTREE:
         begin
            cret := TBamTreeMonster.Create;
         end;

      RC_SPIDERHOUSEMON:
         begin
            cret := TSpiderHouseMonster.Create;  // 蛛蛛巢,爆眼蛛蛛
         end;

      RC_EXPLOSIONSPIDER:
         begin
            cret := TExplosionSpider.Create;  // 爆走蛛蛛
         end;

      RC_HIGHRISKSPIDER:
         begin
            cret := THighRiskSpider.Create
         end;

      RC_BIGPOISIONSPIDER:
         begin
            cret := TBigPoisionSpider.Create;
         end;

      RC_BLACKSNAKEKING:   // 黑蛇王,双重攻击
         begin
            cret := TDoubleCriticalMonster.Create;
         end;

      RC_NOBLEPIGKING:     // 贵猪王,强力攻击(非双重)
         begin
            cret := TATMonster.Create;
         end;

      RC_FEATHERKINGOFKING:  // 黑天魔王
         begin
            cret := TDoubleCriticalMonster.Create;
         end;

      // 2003/02/11 骷髅半王,腐蚀鬼,骷髅兵卒
      RC_SKELETONKING:  // 骷髅半王
         begin
            cret := TSkeletonKingMonster.Create;
         end;
      RC_TOXICGHOST:  // 腐蚀鬼
         begin
            cret := TGasAttackMonster.Create;
         end;
      RC_SKELETONSOLDIER:  // 骷髅兵卒
         begin
            cret := TSkeletonSoldier.Create;
         end;
      // 2003/03/04 般若左使,右使,死牛天王
      RC_BANYAGUARD:  // 般若左/右使
         begin
            cret := TBanyaGuardMonster.Create;
         end;
      RC_DEADCOWKING:  // 死牛天王
         begin
            cret := TDeadCowKingMonster.Create;
         end;

      RC_CASTLEDOOR:   // 城门
         begin
            cret := TCastleDoor.Create;
         end;

      RC_WALL:
         begin
            cret := TWallStructure.Create;
         end;

      RC_ARCHERGUARD:  // 弓箭守卫
         begin
            cret := TArcherGuard.Create;
         end;

      RC_ARCHERPOLICE:  // 弓箭警察
         begin
            cret := TArcherPolice.Create;
         end;

      RC_ELFMON:
         begin
            cret := TElfMonster.Create;  // 神兽变身前
         end;

      RC_ELFWARRIORMON:
         begin
            cret := TElfWarriorMonster.Create;  // 神兽变身后
         end;

      RC_SOCCERBALL:
         begin
            cret := TSoccerBall.Create;
         end;



   end;
   if cret <> nil then begin
      ApplyMonsterAbility (cret, monname);
      cret.Penvir := env;
      cret.MapName := map;
      cret.CX := x;
      cret.CY := y;
      cret.Dir := Random(8);
      cret.UserName := monname;
      cret.WAbil := cret.Abil;

      // 隐身概率
      if Random (100) < cret.CoolEye then begin
         cret.BoViewFixedHide := TRUE;
      end;

      MonGetRandomItems (cret);

      cret.Initialize;
      if cret.ErrorOnInit then begin // 生成位置无法移动
         outofrange := nil;

         if cret.PEnvir.MapWidth < 50 then stepx := 2
         else stepx := 3;
         if cret.PEnvir.MapHeight < 250 then begin
            if cret.PEnvir.MapHeight < 30 then edge := 2
            else edge := 20;
         end else edge := 50;

         for i:=0 to 30 do begin
            // 允许重叠生成
            if not cret.PEnvir.CanWalk (cret.CX, cret.CY, TRUE) then begin
               if cret.CX < cret.PEnvir.MapWidth-edge-1 then Inc (cret.CX, stepx)
               else begin
                  cret.CX := edge + Random(cret.PEnvir.MapWidth div 2);
                  if cret.CY < cret.PEnvir.MapHeight-edge-1 then Inc (cret.CY, stepx)
                  else cret.CY := edge + Random(cret.PEnvir.MapHeight div 2);
               end;
            end else begin
               outofrange := cret.PEnvir.AddToMap (cret.CX, cret.CY, OS_MOVINGOBJECT, cret);
               break;
            end;
         end;
         if outofrange = nil then begin
            cret.Free;
            cret := nil;
         end;
      end;
   end;
   Result := cret;
end;

function  TUserEngine.AddCreatureSysop (map: string; x, y: integer; monname: string): TCreature;
var
   cret: TCreature;
   n, race: integer;
begin
   race := UserEngine.GetMonRace (monname);
   cret := AddCreature (map, x, y, race, monname);
   if cret <> nil then begin
      n := MonList.Count-1;
      if n < 0 then n := 0;
      PTZenInfo(MonList[n]).Mons.Add (cret);
   end;
   Result := cret;
end;

function  TUserEngine.RegenMonsters (pz: PTZenInfo; zcount: integer): Boolean;
var
   i, n, zzx, zzy: integer;
   start: longword;
   cret: TCreature;
   str : string;
begin
   Result := TRUE;
   start := GetTickCount;
   try
      n := zcount; //pz.Count - pz.Mons.Count;
      //race := GetMonRace (pz.MonName);
      if pz.MonRace > 0 then begin
         if Random(100) < pz.SmallZenRate then begin // 集中生成
            zzx := pz.X - pz.Area + Random(pz.Area*2+1);
            zzy := pz.Y - pz.Area + Random(pz.Area*2+1);
            for i:=0 to n-1 do begin
               cret := AddCreature (pz.MapName,
                                    zzx - 10 + Random(20),
                                    zzy - 10 + Random(20),
                                    pz.MonRace,
                                    pz.MonName);
               if cret <> nil then
                  pz.Mons.Add (cret);
               if GetTickCount - start > ZenLimitTime then begin
                  Result := FALSE; // 生成量过多,下次继续
                  break;
               end;
            end;
         end else begin
            for i:=0 to n-1 do begin
               cret := AddCreature (pz.MapName,
                                    pz.X - pz.Area + Random(pz.Area*2+1),
                                    pz.Y - pz.Area + Random(pz.Area*2+1),
                                    pz.MonRace,
                                    pz.MonName);
               if cret <> nil then
                  pz.Mons.Add (cret);
               if GetTickCount - start > ZenLimitTime then begin
                  Result := FALSE; // 生成量过多,下次继续
                  break;
               end;
            end;
         end;
      end;
   except
      MainOutMessage ('[TUserEngine] RegenMonsters exception');
   end;
end;

function  TUserEngine.GetMonCount (pz: PTZenInfo): integer;
var
   i, n: integer;
begin
   n := 0;
   for i:=0 to pz.Mons.Count-1 do begin
      if not TCreature(pz.Mons[i]).Death and not TCreature(pz.Mons[i]).BoGhost then
         Inc (n);
   end;
   Result := n;
end;

function  TUserEngine.GetGenCount (mapname: string): integer;
var
   i, count: integer;
   pz: PTZenInfo;
begin
   count := 0;
   for i:=0 to MonList.Count-1 do begin
      pz := PTZenInfo(MonList[i]);
      if pz <> nil then begin
         if CompareText(pz.MapName, mapname) = 0 then
            count := count + GetMonCount (pz);
      end;
   end;
   Result := count;
end;

// 如果list为nil则只返回数量
function  TUserEngine.GetMapMons (penvir: TEnvirnoment; list: TList): integer;
var
   i, k, count: integer;
   pz: PTZenInfo;
   cret: TCreature;
begin
   count := 0;
   Result := 0;
   if penvir = nil then exit;
   for i:=0 to MonList.Count-1 do begin
      pz := PTZenInfo(MonList[i]);
      if pz <> nil then begin
         for k:=0 to pz.Mons.Count-1 do begin
            cret := TCreature (pz.Mons[k]);
            if not cret.BoGhost and not cret.Death and (cret.PEnvir = penvir) then begin
               if list <> nil then
                  list.Add (cret);
               Inc (count);
            end;
         end;
      end;
   end;
   Result := count;
end;


{---------------------------------------------------------}

{ GetDefMagic
  功能: 根据魔法名称获取魔法定义
  参数:
    magname - 魔法名称
  返回值: 魔法定义指针,如果未找到则返回nil }
function  TUserEngine.GetDefMagic (magname: string): PTDefMagic;
var
   i: integer;
begin
   Result := nil;
   // 遍历魔法定义列表查找匹配的名称
   for i:=0 to DefMagicList.Count-1 do begin
      if CompareText (PTDefMagic(DefMagicList[i]).MagicName, magname) = 0 then begin
         Result := PTDefMagic(DefMagicList[i]);
         break;
      end;
   end;
end;

{ GetDefMagicFromId
  功能: 根据魔法ID获取魔法定义
  参数:
    Id - 魔法ID
  返回值: 魔法定义指针,如果未找到则返回nil }
function  TUserEngine.GetDefMagicFromId (Id: integer): PTDefMagic;
var
   i: integer;
begin
   Result := nil;
   // 遍历魔法定义列表查找匹配的ID
   for i:=0 to DefMagicList.Count-1 do begin
      if PTDefMagic(DefMagicList[i]).MagicId = Id then begin
         Result := PTDefMagic(DefMagicList[i]);
         break;
      end;
   end;
end;


{---------------------------------------------------------}

{ AddNewUser
  功能: 添加新用户到准备列表
  参数:
    ui - 用户开启信息指针
  实现原理:
    使用临界区锁保证线程安全地添加用户到准备列表 }
procedure TUserEngine.AddNewUser (ui: PTUserOpenInfo);
begin
   try
      usLock.Enter;  // 进入临界区
      ReadyList.AddObject (ui.Name, TObject(ui));
   finally
      usLock.Leave;  // 离开临界区
   end;
end;

{ ClosePlayer
  功能: 关闭玩家连接
  参数:
    hum - 玩家对象
  实现原理:
    记录幽灵时间并添加到待关闭列表,等待5分钟后释放 }
procedure TUserEngine.ClosePlayer (hum: TUserHuman);
begin
   hum.GhostTime := GetTickCount;  // 记录幽灵时间
   ClosePlayers.Add (hum);         // 添加到待关闭列表
end;

{ SavePlayer
  功能: 保存玩家数据
  参数:
    hum - 玩家对象
  实现原理:
    1. 创建保存记录结构
    2. 填充用户ID、名称、认证等信息
    3. 生成玩家数据记录
    4. 添加到前端引擎的保存队列 }
procedure TUserEngine.SavePlayer (hum: TUserHuman);
var
   p: PTSaveRcd;
begin
   new (p);
   FillChar (p^, sizeof(TSaveRcd), 0);  // 清空结构
   p.uId := hum.UserId;                 // 用户ID
   p.uName := hum.UserName;             // 用户名
   p.Certify := hum.Certification;      // 认证码
   p.hum := hum;                        // 玩家对象引用
   FDBMakeHumRcd (hum, @p.rcd);         // 生成玩家数据记录
   FrontEngine.AddSavePlayer (p);       // 添加到保存队列
end;

{ ChangeAndSaveOk
  功能: 变更并保存完成回调
  参数:
    pc - 变更用户信息指针
  实现原理:
    复制变更信息并线程安全地添加到完成列表 }
procedure TUserEngine.ChangeAndSaveOk (pc: PTChangeUserInfo);
var
   pcu: PTChangeUserInfo;
begin
   new (pcu);
   pcu^ := pc^;  // 必须复制一份新的
   try
      usLock.Enter;  // 进入临界区
      SaveChangeOkList.Add (pcu);
   finally
      usLock.Leave;  // 离开临界区
   end;
end;

{ GetMyDegree
  功能: 获取用户权限等级
  参数:
    uname - 用户名
  返回值: 用户权限等级,默认为UD_USER(普通用户) }
function  TUserEngine.GetMyDegree (uname: string): integer;
var
   i: integer;
begin
   Result := UD_USER;  // 默认为普通用户
   // 遍历管理员列表查找匹配的用户名
   for i:=0 to AdminList.Count-1 do begin
      if CompareText (AdminList[i], uname) = 0 then begin
         Result := integer(AdminList.Objects[i]);  // 返回权限等级
         break;
      end;
   end;
end;

{ GetUserHuman
  功能: 根据名称获取在线玩家对象
  参数:
    who - 玩家名称
  返回值: 玩家对象,如果未找到或已是幽灵状态则返回nil }
function  TUserEngine.GetUserHuman (who: string): TUserHuman;
var
   i: integer;
begin
   Result := nil;
   // 遍历运行中的用户列表
   for i:=0 to RunUserList.Count-1 do begin
      if CompareText (RunUserList[i], who) = 0 then begin
         // 检查是否为幽灵状态
         if not TUserHuman(RunUserList.Objects[i]).BoGhost then begin
            Result := TUserHuman(RunUserList.Objects[i]);
            break;
         end;
      end;
   end;
end;

{ FindOtherServerUser
  功能: 查找用户是否在其他服务器上在线
  参数:
    who - 用户名
    svindex - 输出的服务器索引
  返回值: 找到返回TRUE,否则返回FALSE }
function  TUserEngine.FindOtherServerUser (who: string; var svindex: integer): Boolean;
var
   i: integer;
begin
   Result := FALSE;
   // 遍历其他服务器用户列表
   for i:=0 to OtherUserNameList.Count-1 do begin
      if CompareText (OtherUserNameList[i], who) = 0 then begin
         svindex := integer(OtherUserNameList.Objects[i]);  // 获取服务器索引
         Result := TRUE;
         break;
      end;
   end;
end;

{ GetUserCount
  功能: 获取总用户数(包括本服务器和其他服务器)
  返回值: 总用户数 }
function  TUserEngine.GetUserCount: integer;
begin
   Result := RunUserList.Count + OtherUserNameList.Count;
end;

{ GetRealUserCount
  功能: 获取本服务器实际用户数
  返回值: 本服务器用户数 }
function  TUserEngine.GetRealUserCount: integer;
begin
   Result := RunUserList.Count;
end;

{ GetAreaUserCount
  功能: 获取指定区域内的用户数量
  参数:
    env - 地图环境对象
    x, y - 中心坐标
    wide - 搜索范围
  返回值: 区域内的用户数量 }
function  TUserEngine.GetAreaUserCount (env: TEnvirnoment; x, y, wide: integer): integer;
var
   i, n: integer;
   hum: TUserhuman;
begin
   n := 0;
   // 遍历所有运行中的用户
   for i:=0 to RunUserList.Count-1 do begin
      hum := TUserHuman(RunUserList.Objects[i]);
      // 检查是否在指定地图和范围内
      if (not hum.BoGhost) and (hum.PEnvir = env) then begin
         if (Abs(hum.CX-x) < wide) and (Abs(hum.CY-y) < wide) then
            Inc (n);
      end;
   end;
   Result := n;
end;

{ GetAreaUsers
  功能: 获取指定区域内的用户列表(包括躺下的玩家)
  参数:
    env - 地图环境对象
    x, y - 中心坐标
    wide - 搜索范围
    ulist - 输出的用户列表
  返回值: 找到的用户数量 }
function  TUserEngine.GetAreaUsers (env: TEnvirnoment; x, y, wide: integer; ulist: TList): integer;
var
   i, n: integer;
   hum: TUserhuman;
begin
   n := 0;
   // 遍历所有运行中的用户
   for i:=0 to RunUserList.Count-1 do begin
      hum := TUserHuman(RunUserList.Objects[i]);
      // 检查是否在指定地图和范围内
      if (not hum.BoGhost) and (hum.PEnvir = env) then begin
         if (Abs(hum.CX-x) < wide) and (Abs(hum.CY-y) < wide) then begin
            ulist.Add (hum);
            Inc (n);
         end;
      end;
   end;
   Result := n;
end;

{ GetHumCount
  功能: 获取指定地图的玩家数量
  参数:
    mapname - 地图名称
  返回值: 该地图上的玩家数量 }
function  TUserEngine.GetHumCount (mapname: string): integer;
var
   i, n: integer;
   hum: TUserhuman;
begin
   n := 0;
   // 遍历所有运行中的用户
   for i:=0 to RunUserList.Count-1 do begin
      hum := TUserHuman(RunUserList.Objects[i]);
      // 检查是否在指定地图上
      if (not hum.BoGhost) and (CompareText (hum.PEnvir.MapName, mapname) = 0) then begin
         Inc (n);
      end;
   end;
   Result := n;
end;

{ CryCry
  功能: 向指定区域广播喊话消息
  参数:
    msgtype - 消息类型
    env - 地图环境对象
    x, y - 中心坐标
    wide - 广播范围
    saying - 喊话内容 }
procedure TUserEngine.CryCry (msgtype: integer; env: TEnvirnoment; x, y, wide: integer; saying: string);
var
   i: integer;
   hum: TUserhuman;
begin
   // 遍历所有运行中的用户
   for i:=0 to RunUserList.Count-1 do begin
      hum := TUserHuman(RunUserList.Objects[i]);
      // 检查是否在指定范围内且开启了喊话监听
      if (not hum.BoGhost) and (hum.PEnvir = env) and (hum.BoHearCry) then begin
         if (Abs(hum.CX-x) < wide) and (Abs(hum.CY-y) < wide) then
            hum.SendMsg (nil, msgtype{RM_CRY}, 0, clBlack, clYellow, 0, saying);
      end;
   end;
end;

{ SysMsgAll
  功能: 向所有玩家发送系统消息
  参数:
    saying - 消息内容 }
procedure TUserEngine.SysMsgAll (saying: string);
var
   i: integer;
   hum: TUserhuman;
begin
   // 遍历所有运行中的用户
   for i:=0 to RunUserList.Count-1 do begin
      hum := TUserHuman(RunUserList.Objects[i]);
      if not hum.BoGhost then begin
         hum.SysMsg (saying, 0);  // 发送系统消息
      end;
   end;
end;


{ KickDoubleConnect
  功能: 踢出重复登录的用户
  参数:
    uname - 用户名
  实现原理:
    设置用户的关闭请求标志为TRUE }
procedure TUserEngine.KickDoubleConnect (uname: string);
var
   i: integer;
begin
   // 遍历运行中的用户列表
   for i:=0 to RunUserList.Count-1 do begin
      if CompareText (RunUserList[i], uname) = 0 then begin
         TUserHuman (RunUserList.Objects[i]).UserRequestClose := TRUE;  // 设置关闭标志
         break;
      end;
   end;
end;

{ GuildMemberReLogin
  功能: 行会成员重新登录处理
  参数:
    guild - 行会对象
  实现原理:
    遍历所有在线玩家,为属于该行会的成员重新登录 }
procedure TUserEngine.GuildMemberReLogin (guild: TGuild);
var
   i, n: integer;
begin
   // 遍历所有运行中的用户
   for i:=0 to RunUserList.Count-1 do begin
      if TUserHuman (RunUserList.Objects[i]).MyGuild = guild then begin
         guild.MemberLogin (TUserHuman (RunUserList.Objects[i]), n);  // 行会成员登录
      end;
   end;
end;


{ AddServerWaitUser
  功能: 添加等待服务器转移的用户(从其他服务器接收)
  参数:
    psui - 服务器转移用户信息指针
  返回值: 始终返回TRUE }
function  TUserEngine.AddServerWaitUser (psui: PTServerShiftUserInfo): Boolean;
begin
   psui.waittime := GetTickCount;  // 记录等待开始时间
   WaitServerList.Add (psui);      // 添加到等待列表
   Result := TRUE;
end;

{ CheckServerWaitTimeOut
  功能: 检查服务器等待超时
  实现原理:
    遍历等待列表,删除等待超过30秒的记录 }
procedure TUserEngine.CheckServerWaitTimeOut;
var
   i: integer;
begin
   // 倒序遍历等待列表
   for i:=WaitServerList.Count-1 downto 0 do begin
      // 检查是否超过30秒
      if GetTickCount - PTServerShiftUserInfo(WaitServerList[i]).waittime > 30 * 1000 then begin
         Dispose (PTServerShiftUserInfo(WaitServerList[i]));  // 释放内存
         WaitServerList.Delete (i);  // 从列表删除
      end;
   end;
end;

{ CheckHolySeizeValid
  功能: 检查结界是否有效
  实现原理:
    1. 遍历结界列表
    2. 检查结界中的怪物是否死亡或解除
    3. 如果结界中没有怪物或超过3分钟则删除结界 }
procedure TUserEngine.CheckHolySeizeValid;
var
   i, k: integer;
   phs: PTHolySeizeInfo;
   cret: TCreature;
begin
   // 倒序遍历结界列表
   for i:=HolySeizeList.Count-1 downto 0 do begin
      phs := PTHolySeizeInfo (HolySeizeList[i]);
      if phs <> nil then begin
         // 检查结界中的怪物是否死亡或解除
         for k:=phs.seizelist.Count-1 downto 0 do begin
            cret := phs.seizelist[k];
            if (cret.Death) or (cret.BoGhost) or (not cret.BoHolySeize) then begin
               phs.seizelist.Delete (k);
            end;
         end;
         // 检查结界是否应该删除:没有怪物或超过3分钟
         if (phs.seizelist.Count <= 0) or
            (GetTickCount - phs.OpenTime > phs.SeizeTime) or
            (GetTickCount - phs.OpenTime > 3 * 60 * 1000) then begin
            phs.seizelist.Free;  // 释放怪物列表
            // 关闭所有事件
            for k:=0 to 7 do begin
               if phs.earr[k] <> nil then
                  TEvent(phs.earr[k]).Close;
            end;
            Dispose (phs);  // 释放结界信息
            HolySeizeList.Delete (i);  // 从列表删除
         end;
      end;
   end;
end;

{ GetServerShiftInfo
  功能: 获取服务器转移用户信息
  参数:
    uname - 用户名
    certify - 认证码
  返回值: 服务器转移用户信息指针,未找到返回nil }
function  TUserEngine.GetServerShiftInfo (uname: string; certify: integer): PTServerShiftUserInfo;
var
   i: integer;
begin
   Result := nil;
   // 遍历等待列表查找匹配的用户
   for i:=0 to WaitServerList.Count-1 do begin
      if (CompareText (PTServerShiftUserInfo(WaitServerList[i]).UserName, uname) = 0) and
         (PTServerShiftUserInfo(WaitServerList[i]).Certification = certify) then begin
         Result := PTServerShiftUserInfo(WaitServerList[i]);
         break;
      end;
   end;
end;

{ MakeServerShiftData
  功能: 创建服务器转移数据
  参数:
    hum - 玩家对象
    sui - 输出的服务器转移用户信息
  实现原理:
    将玩家的各种数据复制到转移结构中,包括:
    - 基本信息和认证
    - 队伍信息
    - 各种开关设置
    - 私聊屏蔽列表
    - 召唤兽信息
    - 额外能力和时间 }
procedure TUserEngine.MakeServerShiftData (hum: TUserHuman; var sui: TServerShiftUserInfo);
var
   i: integer;
   cret: TCreature;
begin
   FillChar (sui, sizeof(TServerShiftUserInfo), #0);  // 清空结构
   sui.UserName := hum.UserName;
   FDBMakeHumRcd (hum, @sui.rcd);  // 创建玩家记录
   sui.Certification := hum.Certification;
   // 复制队伍信息
   if hum.GroupOwner <> nil then begin
      sui.GroupOwner := hum.GroupOwner.UserName;
      for i:=0 to hum.GroupOwner.GroupMembers.Count-1 do
         sui.GroupMembers[i] := hum.GroupOwner.GroupMembers[i];
   end;
   // 复制各种开关设置
   sui.BoHearCry := hum.BoHearCry;
   sui.BoHearWhisper := hum.BoHearWhisper;
   sui.BoHearGuildMsg := hum.BoHearGuildMsg;
   sui.BoSysopMode := hum.BoSysopMode;
   sui.BoSuperviserMode := hum.BoSuperviserMode;
   // 复制私聊屏蔽列表
   for i:=0 to hum.WhisperBlockList.Count-1 do
      if i <= 9 then sui.WhisperBlockNames[i] := hum.WhisperBlockList[i];
   // 复制召唤兽信息
   for i:=0 to hum.SlaveList.Count-1 do begin
      cret := hum.SlaveList[i];
      if i <= 4 then begin
         sui.Slaves[i].SlaveName := cret.UserName;
         sui.Slaves[i].SlaveExp := cret.SlaveExp;
         sui.Slaves[i].SlaveExpLevel := cret.SlaveExpLevel;
         sui.Slaves[i].SlaveMakeLevel := cret.SlaveMakeLevel;
         sui.Slaves[i].RemainRoyalty := (cret.MasterRoyaltyTime - GetTickCount) div 1000;  // 秒为单位
         sui.Slaves[i].HP := cret.WAbil.HP;
         sui.Slaves[i].MP := cret.WAbil.MP;
      end;
   end;
   // 复制额外能力和剩余时间
   for i:=0 to 5 do begin
      sui.ExtraAbil[i] := hum.ExtraAbil[i];
      if hum.ExtraAbilTimes[i] > GetTickCount then
      sui.ExtraAbilTimes[i] :=  hum.ExtraAbilTimes[i]- GetTickCount  // 只保存剩余时间
      else sui.ExtraAbilTimes[i] := 0;
   end;
end;

{ LoadServerShiftData
  功能: 加载服务器转移数据到玩家对象
  参数:
    psui - 服务器转移用户信息指针
    hum - 玩家对象
  实现原理:
    将转移数据中的各种设置恢复到玩家对象 }
procedure TUserEngine.LoadServerShiftData (psui: PTServerShiftUserInfo; var hum: TUserHuman);
var
   i: integer;
   pslave: PTSlaveInfo;
begin
   // 队伍处理延后(较复杂)
   if psui.GroupOwner <> '' then begin
   end;
   // 恢复各种开关设置
   hum.BoHearCry := psui.BoHearCry;
   hum.BoHearWhisper := psui.BoHearWhisper;
   hum.BoHearGuildMsg := psui.BoHearGuildMsg;
   hum.BoSysopMode := psui.BoSysopMode;
   hum.BoSuperviserMode := psui.BoSuperviserMode;
   // 恢复私聊屏蔽列表
   for i:=0 to 9 do
      if psui.WhisperBlockNames[i] <> '' then begin
         hum.WhisperBlockList.Add (psui.WhisperBlockNames[i]);
         break;
      end;
   // 恢复召唤兽信息
   for i:=0 to 4 do
      if psui.Slaves[i].SlaveName <> '' then begin
         new (pslave);
         pslave^ := psui.Slaves[i];
            // 2003/06/12 召唤兽补丁
            hum.PrevServerSlaves.Add (pslave);  // 线程不安全
      end;
   // 恢复额外能力和时间
   for i:=0 to 5 do begin
      hum.ExtraAbil[i] := psui.ExtraAbil[i];
      if psui.ExtraAbilTimes[i] > 0 then
      hum.ExtraAbilTimes[i] := psui.ExtraAbilTimes[i] + GetTickCount  // 保存的是剩余时间
      else
      hum.ExtraAbilTimes[i] := 0 ;
   end;
end;

{ ClearServerShiftData
  功能: 清除服务器转移数据
  参数:
    psui - 要清除的服务器转移用户信息指针
  实现原理:
    从等待列表中查找并删除指定的转移数据 }
procedure TUserEngine.ClearServerShiftData (psui: PTServerShiftUserInfo);
var
   i: integer;
begin
   // 遍历等待列表查找匹配的记录
   for i:=0 to WaitServerList.Count-1 do begin
      if PTServerShiftUserInfo(WaitServerList[i]) = psui then begin
         Dispose (PTServerShiftUserInfo(WaitServerList[i]));  // 释放内存
         WaitServerList.Delete (i);  // 从列表删除
         break;
      end;
   end;
end;

{ WriteShiftUserData
  功能: 将服务器转移数据写入文件
  参数:
    psui - 服务器转移用户信息指针
  返回值: 成功返回文件名,失败返回空字符串
  实现原理:
    1. 生成唯一文件名
    2. 计算校验和
    3. 写入数据和校验和到文件 }
function  TUserEngine.WriteShiftUserData (psui: PTServerShiftUserInfo): string;
var
   flname: string;
   i, fhandle, checksum: integer;
begin
   Result := '';
   // 生成唯一文件名
   flname := '$_' + IntToStr(ServerIndex) + '_$_' + IntToStr(ShareFileNameNum) + '.shr';
   Inc (ShareFileNameNum);
   try
      // 计算校验和
      checksum := 0;
      for i:=0 to sizeof(TServerShiftUserInfo)-1 do
         checksum := checksum + pbyte(integer(psui)+i)^;
      // 创建文件并写入数据
      fhandle := FileCreate (ShareBaseDir + flname);
      if fhandle > 0 then begin
         FileWrite (fhandle, psui^, sizeof(TServerShiftUserInfo));  // 写入用户数据
         FileWrite (fhandle, checksum, sizeof(integer));            // 写入校验和
         FileClose (fhandle);
         Result := flname;
      end;
   except
      MainOutMessage ('[Exception] WriteShiftUserData..');
   end;
end;

{ SendInterServerMsg
  功能: 发送服务器间消息
  参数:
    msgstr - 消息内容
  实现原理:
    根据服务器类型选择发送方式:
    - 主服务器通过FrmSrvMsg发送
    - 从服务器通过FrmMsgClient发送 }
procedure TUserEngine.SendInterServerMsg (msgstr: string);
begin
   if ServerIndex = 0 then begin  // 主服务器
      FrmSrvMsg.SendServerSocket (msgstr);
   end else begin  // 从服务器
      FrmMsgClient.SendSocket (msgstr);
   end;
end;

{ SendInterMsg
  功能: 在同一服务器组内发送服务器间消息
  参数:
    ident - 消息标识
    svidx - 目标服务器索引
    msgstr - 消息内容
  实现原理:
    将消息编码后发送到目标服务器 }
procedure TUserEngine.SendInterMsg (ident, svidx: integer; msgstr: string);
begin
   if ServerIndex = 0 then begin  // 主服务器
      FrmSrvMsg.SendServerSocket (IntToStr(ident) + '/' + EncodeString(IntToStr(svidx)) + '/' +
                               EncodeString(msgstr));
   end else begin  // 从服务器
      FrmMsgClient.SendSocket (IntToStr(ident) + '/' + EncodeString(IntToStr(svidx)) + '/' +
                               EncodeString(msgstr));
   end;
end;

{ UserServerChange
  功能: 用户服务器转移
  参数:
    hum - 玩家对象
    svindex - 目标服务器索引
  返回值: 成功返回TRUE,失败返回FALSE
  实现原理:
    1. 创建转移数据
    2. 写入文件
    3. 发送服务器间消息通知目标服务器 }
function  TUserEngine.UserServerChange (hum: TUserHuman; svindex: integer): Boolean;
var
   flname: string;
   sui: TServerShiftUserInfo;
begin
   Result := FALSE;
   MakeServerShiftData (hum, sui);  // 创建转移数据
   flname := WriteShiftUserData (@sui);  // 写入文件
   if flname <> '' then begin
      // 保存文件名用于后续确认目标服务器是否成功接收
      hum.TempStr := flname;
      // 发送服务器转移消息
      SendInterServerMsg (IntToStr (ISM_USERSERVERCHANGE) + '/' +
                              EncodeString(IntToStr(svindex)) + '/' +
                              EncodeString(flname));
      Result := TRUE;
   end;
end;

{ GetISMChangeServerReceive
  功能: 处理服务器转移接收确认
  参数:
    flname - 转移数据文件名
  实现原理:
    查找匹配的玩家并设置转移成功标志 }
procedure TUserEngine.GetISMChangeServerReceive (flname: string);
var
    i: integer;
    hum: TUserHuman;
begin
    // 遍历关闭玩家列表查找匹配的文件名
    for i := 0 to ClosePlayers.Count - 1 do begin
        hum := TUserHuman(ClosePlayers[i]);
        if hum.TempStr = flname then begin
            hum.BoChangeServerOK := TRUE;  // 设置转移成功标志
            break;
        end;
    end;
end;


{ DoUserChangeServer
  功能: 执行用户服务器转移
  参数:
    hum - 玩家对象
    svindex - 目标服务器索引
  返回值: 成功返回true,失败返回false
  实现原理:
    获取目标服务器地址和端口,发送重连消息给客户端 }
function  TUserEngine.DoUserChangeServer (hum: TUserHuman; svindex: integer): Boolean;
var
   naddr: string;
   nport: integer;
begin
   Result := false;
   // 获取目标服务器地址和端口,引导客户端重新连接
   if GetMultiServerAddrPort (byte(svindex), naddr, nport) then begin
      hum.SendDefMessage (SM_RECONNECT, 0, 0, 0, 0, naddr + '/' + IntToStr(nport));
      Result := true;
   end;
end;

{ OtherServerUserLogon
  功能: 处理其他服务器用户登录
  参数:
    snum - 服务器编号
    uname - 用户名(包含模式信息)
  实现原理:
    1. 解析用户名和模式
    2. 删除已存在的同名记录
    3. 添加新记录到其他服务器用户列表
    4. 如果是免费用户则增加计数 }
procedure TUserEngine.OtherServerUserLogon (snum: integer; uname: string);
var
   i: integer;
   str, name, apmode: string;
begin
   apmode := GetValidStr3 (uname, name, [':']);  // 解析用户名和模式
   // 删除已存在的同名记录
   for i:=OtherUserNameList.Count-1 downto 0 do begin
      if CompareText (OtherUserNameList[i], name) = 0 then begin
         OtherUserNameList.Delete (i);
      end;
   end;
   // 添加新记录
   OtherUserNameList.AddObject (name, TObject(snum));
   // 如果是免费用户则增加计数
   if StrToInt(apmode) = 1 then begin
      Inc(FreeUserCount);
   end;
end;

{ OtherServerUserLogout
  功能: 处理其他服务器用户登出
  参数:
    snum - 服务器编号
    uname - 用户名(包含模式信息)
  实现原理:
    1. 解析用户名和模式
    2. 从其他服务器用户列表中删除记录
    3. 如果是免费用户则减少计数 }
procedure TUserEngine.OtherServerUserLogout (snum: integer; uname: string);
var
   i: integer;
   str, name, apmode: string;
begin
   apmode := GetValidStr3 (uname, name, [':']);  // 解析用户名和模式
   // 从列表中删除匹配的记录
   for i:=0 to OtherUserNameList.Count-1 do begin
      if (CompareText (OtherUserNameList[i], name) = 0) and (integer(OtherUserNameList.Objects[i]) = snum) then begin
         OtherUserNameList.Delete (i);
         break;
      end;
   end;
   // 如果是免费用户则减少计数
   if StrToInt(apmode) = 1 then begin
      Dec(FreeUserCount);
   end;
end;

{ AccountExpired
  功能: 处理账号过期
  参数:
    uid - 用户ID
  实现原理:
    查找匹配的在线用户并设置账号过期标志 }
procedure TUserEngine.AccountExpired (uid: string);
var
   i: integer;
begin
   // 遍历运行中的用户列表
   for i:=0 to RunUserList.Count-1 do begin
      if CompareText (TUserHuman(RunUserList.Objects[i]).UserId, uid) = 0 then begin
         TUserHuman(RunUserList.Objects[i]).BoAccountExpired := TRUE;  // 设置过期标志
         break;
      end;
   end;
end;


{------------------------ 处理玩家逻辑 --------------------------}

{ ProcessUserHumans
  功能: 处理所有玩家的逻辑
  实现原理:
    1. 处理等待列表中的新玩家
    2. 处理关闭玩家列表中的服务器转移
    3. 遍历运行中的玩家执行逻辑
    4. 处理玩家的视野搜索、操作、保存等
  内部函数:
    OnUse - 检查用户是否在使用中
    MakeNewHuman - 创建新玩家对象 }
procedure TUserEngine.ProcessUserHumans;
   // OnUse: 检查用户是否在使用中(正在保存或已在线)
   function OnUse (uname: string): Boolean;
   var
      k: integer;
   begin
      Result := FALSE;
      // 检查是否正在保存
      if FrontEngine.IsDoingSave (uname) then begin
         Result := TRUE;
         exit;
      end;
      // 检查是否已在线
      for k:=0 to RunUserList.Count-1 do begin
         if CompareText (RunUserList[k], uname) = 0 then begin
            Result := TRUE;
            break;
         end;
      end;
   end;
   // MakeNewHuman: 创建新玩家对象
   function MakeNewHuman (pui: PTUserOpenInfo): TUserHuman;
   var
      i: integer;
      mapenvir: TEnvirnoment;
      hum: TUserHuman;
      hmap: string;
      pshift: PTServerShiftUserInfo;
   label
      ERROR_MAP;
   begin
      Result := nil;
      try
         hum := TUserHuman.Create;
         if not BoVentureServer then begin
            // 如果有服务器转移数据则获取
            pshift := GetServerShiftInfo (pui.Name, pui.ReadyInfo.Certification);
         end else begin
            pshift := nil;
            // 读取冒险服务器的转移信息
         end;
         if pshift = nil then begin // 不是服务器转移
            FDBLoadHuman (@pui.Rcd, hum);
            hum.RaceServer := RC_USERHUMAN;
            if hum.HomeMap = '' then begin // 没有设置任何内容
               ERROR_MAP:
               GetRandomDefStart (hmap, hum.HomeX, hum.HomeY);
               hum.HomeMap := hmap;

               hum.MapName := hum.HomeMap;    // 以HomeMap为基准
               hum.CX := hum.GetStartX;
               hum.CY := hum.GetStartY;

               if hum.Abil.Level = 0 then begin  // 首次创建角色
                  with hum.Abil do begin
                     Level := 1;
                     AC    := 0;
                     MAC   := 0;
                     DC    := MakeWord(1,2);
                     MC    := MakeWord(1,2);
                     SC    := MakeWord(1,2);
                     MP    := 15;
                     HP    := 15;
                     MaxHP := 15;
                     MaxMP := 15;
                     Exp   := 0;
                     MaxExp := 100;
                     Weight := 0;
                     MaxWeight := 30;
                  end;
                  hum.FirstTimeConnection := TRUE;
               end;
            end;

            mapenvir := GrobalEnvir.ServerGetEnvir (ServerIndex, hum.MapName);
            if mapenvir <> nil then begin
               // 检查是否在门派对练事件房间
               if mapenvir.Fight3Zone then begin  // 在门派对练事件房间
                  // 如果死亡
                  if hum.Abil.HP <= 0 then begin
                     if hum.FightZoneDieCount < 3 then begin
                        hum.Abil.HP := hum.Abil.MaxHP;
                        hum.Abil.MP := hum.Abil.MaxMP;
                        hum.MustRandomMove := TRUE;
                     end;
                  end;
               end else
                  hum.FightZoneDieCount := 0;

            end;

            hum.MyGuild := GuildMan.GetGuildFromMemberName (hum.UserName);
            if (mapenvir <> nil) then begin
               // 检查是否在城堡内城重连
               if (UserCastle.CorePEnvir = mapenvir) or
                  (UserCastle.BoCastleUnderAttack and UserCastle.IsCastleWarArea (mapenvir, hum.CX, hum.CY))
               then begin
                  if not UserCastle.IsCastleMember (hum) then begin
                     // 非城堡成员
                     hum.MapName := hum.HomeMap;
                     hum.CX := hum.HomeX - 2 + Random(5);
                     hum.CY := hum.HomeY - 2 + Random(5);
                  end else begin
                     // 城堡成员
                     if UserCastle.CorePEnvir = mapenvir then begin
                        // 城堡成员在内城重连时,移动到城堡入口
                        hum.MapName := UserCastle.GetCastleStartMap;
                        hum.CX := UserCastle.GetCastleStartX;
                        hum.CY := UserCastle.GetCastleStartY;
                     end;
                  end;
               end;

            end;

            // 2001-03-21 PK值、经验值调整的DB批量修改
            if (hum.DBVersion <= 1) and (hum.Abil.Level >= 1) then begin
               // 将红名改为黄名
               //if hum.PKLevel >= 2 then hum.PlayerKillingPoint := 150;
               // 重新设置经验值
               //hum.Abil.Exp := Round((hum.Abil.Exp / hum.Abil.MaxExp) * hum.GetNextLevelExp (hum.Abil.Level));
               //hum.Reset_6_28_bugitems;
               hum.DBVersion := 2;
            end;

{$IFDEF FOR_ABIL_POINT}
// 4/16开始应用
            // 检查是否应用了加点系统
            if hum.BonusApply <= 3 then begin
               hum.BonusApply := 4;
               hum.BonusPoint := GetLevelBonusSum (hum.Job, hum.Abil.Level);
               FillChar (hum.BonusAbil, sizeof(TNakedAbility), #0);
               FillChar (hum.CurBonusAbil, sizeof(TNakedAbility), #0);
               hum.MapName := hum.HomeMap;  // 从村庄开始(因为体力可能不足)
               hum.CX := hum.HomeX - 2 + Random(5);
               hum.CY := hum.HomeY - 2 + Random(5);
            end;
{$ENDIF}

            // 地图错误的情况
            if GrobalEnvir.GetEnvir (hum.MapName) = nil then begin
               hum.Abil.HP := 0;  // 处理为死亡
            end;

            // 如果死亡
            if hum.Abil.HP <= 0  then begin
               hum.ResetCharForRevival;
               if hum.PKLevel < 2 then begin
                  // 城堡战期间城堡成员复活在城堡
                  if UserCastle.BoCastleUnderAttack and UserCastle.IsCastleMember (hum) then begin
                     hum.MapName := UserCastle.CastleMap;
                     hum.CX := UserCastle.GetCastleStartX;
                     hum.CY := UserCastle.GetCastleStartY;
                  end else begin
                     hum.MapName := hum.HomeMap;
                     hum.CX := hum.HomeX - 2 + Random(5);
                     hum.CY := hum.HomeY - 2 + Random(5);
                  end;
               end else begin
                  // PK红名在流放地复活
                  hum.MapName := BADMANHOMEMAP;
                  hum.CX := BADMANSTARTX - 6 + Random(13);   // 流放地
                  hum.CY := BADMANSTARTY - 6 + Random(13);
               end;
               hum.Abil.HP := 14;
            end;
            hum.InitValues;  // WAbil := Abil

            mapenvir := GrobalEnvir.ServerGetEnvir (ServerIndex, hum.MapName);
            if mapenvir = nil then begin
               // 该地图在其他服务器上(需要服务器转移)
               hum.Certification := pui.ReadyInfo.Certification;
               hum.UserHandle := pui.ReadyInfo.shandle;
               hum.GateIndex := pui.ReadyInfo.GateIndex;
               hum.UserGateIndex := pui.ReadyInfo.UserGateIndex;
               hum.WAbil := hum.Abil; // 基本初始化
               hum.ChangeToServerNumber := GrobalEnvir.GetServer (hum.MapName);

               // 测试日志
               if hum.Abil.HP <> 14 then  // 不是死亡后进入
                  MainOutMessage ('chg-server-fail-1 [' + IntToStr(ServerIndex) + '] -> [' +
                           IntToStr(hum.ChangeToServerNumber) + '] [' + hum.MapName);

               UserServerChange (hum, hum.ChangeToServerNumber);
               DoUserChangeServer (hum, hum.ChangeToServerNumber);
               hum.Free;
               exit;
            end else begin
               // 连接到当前服务器
               for i:=0 to 4 do begin
                  if not mapenvir.CanWalk (hum.CX, hum.CY, TRUE) then begin
                     hum.CX := hum.CX - 3 + Random (6);
                     hum.CY := hum.CY - 3 + Random (6);
                  end else
                     break;
               end;
               if not mapenvir.CanWalk (hum.CX, hum.CY, TRUE) then begin
                  // 测试日志
                  MainOutMessage ('chg-server-fail-2 [' + IntToStr(ServerIndex) + '] ' +
                           IntToStr(hum.CX) + ':' + IntToStr(hum.CY) + ' [' + hum.MapName);

                  // 无法行走的地图(错误坐标)
                  hum.MapName := DefHomeMap;    // 必须是本服务器存在的地图
                  mapenvir := GrobalEnvir.GetEnvir (DefHomeMap);  // 必定存在
                  hum.CX := DefHomeX;
                  hum.CY := DefHomeY;
               end;
            end;
            hum.PEnvir := mapenvir;
            if hum.PEnvir = nil then begin
               MainOutMessage ('[Error] hum.PEnvir = nil');
               goto ERROR_MAP;
            end;

            hum.ReadyRun := FALSE; // 标记需要初始化

         end else begin
            // pui: 从DB服务器读取的数据
            // pshift: 从服务器转移读取的数据
//          FDBLoadHuman (@pui.Rcd, hum);
            FDBLoadHuman (@pshift.Rcd, hum);

            // map, hp等使用服务器转移的数据
            // 如果读取失败可能会出错
            hum.MapName := pshift.rcd.Block.DBHuman.MapName;
            hum.CX := pshift.rcd.Block.DBHuman.CX;
            hum.CY := pshift.rcd.Block.DBHuman.CY;
            // TO PDS
            // hum.Abil := pshift.rcd.Block.DBHuman.Abil;
            hum.Abil.Level := pshift.rcd.Block.DBHuman.Abil_Level;
            hum.Abil.HP    := pshift.rcd.Block.DBHuman.Abil_HP;
            hum.Abil.MP    := pshift.rcd.Block.DBHuman.Abil_MP;
            hum.Abil.EXP   := pshift.rcd.Block.DBHuman.Abil_EXP;

            // 读取服务器转移前的数据
            LoadServerShiftData (pshift, hum);
            ClearServerShiftData (pshift);

            mapenvir := GrobalEnvir.ServerGetEnvir (ServerIndex, hum.MapName);
            if mapenvir = nil then begin
               // 测试日志
               MainOutMessage ('chg-server-fail-3 [' + IntToStr(ServerIndex) + ']  ' +
                           IntToStr(hum.CX) + ':' + IntToStr(hum.CY) + ' [' + hum.MapName);

               hum.MapName := DefHomeMap;
               mapenvir := GrobalEnvir.GetEnvir (DefHomeMap);  // 必定存在
               hum.CX := DefHomeX;
               hum.CY := DefHomeY;
            end else begin
               if not mapenvir.CanWalk (hum.CX, hum.CY, TRUE) then begin
                  // 测试日志
                  MainOutMessage ('chg-server-fail-4 [' + IntToStr(ServerIndex) + ']  ' +
                           IntToStr(hum.CX) + ':' + IntToStr(hum.CY) + ' [' + hum.MapName);

                  hum.MapName := DefHomeMap;
                  mapenvir := GrobalEnvir.GetEnvir (DefHomeMap);  // 必定存在
                  hum.CX := DefHomeX;
                  hum.CY := DefHomeY;
               end;
            end;
            hum.InitValues;
            hum.PEnvir := mapenvir;
            if hum.PEnvir = nil then begin
               MainOutMessage ('[Error] hum.PEnvir = nil');
               goto ERROR_MAP;
            end;

            hum.ReadyRun := FALSE; // 标记需要初始化
            hum.LoginSign := TRUE; // 服务器转移不显示公告
            hum.BoServerShifted := TRUE;
         end;

         hum.UserId             := pui.ReadyInfo.UserId;
         hum.UserAddress        := pui.ReadyInfo.UserAddress;
         hum.UserHandle         := pui.ReadyInfo.shandle;
         hum.UserGateIndex      := pui.ReadyInfo.UserGateIndex;
         hum.GateIndex          := pui.ReadyInfo.GateIndex;
         hum.Certification      := pui.ReadyInfo.Certification;
         hum.ApprovalMode       := pui.ReadyInfo.ApprovalMode;
         hum.AvailableMode      := pui.ReadyInfo.AvailableMode;
         hum.UserConnectTime    := pui.ReadyInfo.ReadyStartTime;
         hum.ClientVersion      := pui.ReadyInfo.ClientVersion;
         hum.LoginClientVersion := pui.ReadyInfo.LoginClientVersion;
         hum.ClientCheckSum     := pui.ReadyInfo.ClientCheckSum;

         Result := hum;
      except
         MainOutMessage ('[TUserEngine] MakeNewHuman exception');
      end;
   end;
var
   i, k: integer;
   start: longword;
   tcount: integer;
   pui: PTUserOpenInfo;
   pc: PTChangeUserInfo;
   hum: TUserHuman;
   newlist, cuglist, cuhlist: TList;
   bugcount: integer;
   lack: Boolean;
begin
   bugcount := 0;
   start := GetTickCount;
   if GetTickCount - hum200time > 200 then begin
      try
         hum200time := GetTickCount;
         newlist := nil;
         cuglist := nil;
         cuhlist := nil;
         try
            usLock.Enter;
            // 处理已准备好的用户
            for i:=0 to ReadyList.Count-1 do begin
               if not FrontEngine.HasServerHeavyLoad and not OnUse(ReadyList[i]) then begin
                  pui := PTUserOpenInfo (ReadyList.Objects[i]);
                  hum := MakeNewHuman (pui);
                  if hum <> nil then begin
                     RunUserList.AddObject (ReadyList[i], hum);
                     SendInterMsg (ISM_USERLOGON, ServerIndex, hum.UserName+ ':' + IntToStr(hum.ApprovalMode));

                     if hum.ApprovalMode = 1 then Inc(UserEngine.FreeUserCount);

                     if newlist = nil then newlist := TList.Create;
                     newlist.Add (hum);
                  end;
               end else begin
                  KickDoubleConnect (ReadyList[i]);
                  ////MainOutMessage ('[Dup] ' + ReadyList[i]); // 重复连接
                  if cuglist = nil then begin
                     cuglist := TList.Create;
                     cuhlist := TList.Create;
                  end;
                  cuglist.Add (pointer(TUserHuman(ReadyList.Objects[i]).GateIndex)); // 避免线程锁定
                  cuhlist.Add (pointer(TUserHuman(ReadyList.Objects[i]).UserHandle));
               end;
               Dispose (PTUserOpenInfo (ReadyList.Objects[i]));
            end;
            ReadyList.Clear;

            // 处理已完成变更的列表
            for i:=0 to SaveChangeOkList.Count-1 do begin
               pc := PTChangeUserInfo (SaveChangeOkList[i]);
               hum := GetUserHuman (pc.CommandWho);
               if hum <> nil then begin
                  hum.RCmdUserChangeGoldOk (pc.UserName, pc.ChangeGold);
               end;
               Dispose (pc);
            end;
            SaveChangeOkList.Clear;
         finally
            usLock.Leave;
         end;

         if newlist <> nil then begin
            for i:=0 to newlist.Count-1 do begin
               hum := TUserHuman(newlist[i]);
               RunSocket.UserLoadingOk (hum.GateIndex, hum.UserHandle, hum);
            end;
            newlist.Free;
         end;
         if cuglist <> nil then begin
            for i:=0 to cuglist.Count-1 do
               RunSocket.CloseUser (integer(cuglist[i]){GateIndex}, integer(cuhlist[i]){UserHandle});
            cuglist.Free;
            cuhlist.Free;
         end;

      except
         MainOutMessage ('[UsrEngn] Exception Ready, Save, Load... ');
      end;
   end;

   try
      // 5分钟后释放关闭的玩家
      for i:=0 to ClosePlayers.Count-1 do begin
         hum := TUserHuman(ClosePlayers[i]);
         if GetTickCount - hum.GhostTime > 5 * 60 * 1000 then begin
            try
               TUserHuman(ClosePlayers[i]).Free;  // 如果有残留引用可能会出错
            except
               MainOutMessage ('[UsrEngn] ClosePlayer.Delete - Free');
            end;
            ClosePlayers.Delete (i);
            break;
         end else begin
            if hum.BoChangeServer then begin
               if hum.BoSaveOk then begin   // 保存完成后进行服务器转移
                  if UserServerChange (hum, hum.ChangeToServerNumber) or (hum.WriteChangeServerInfoCount > 20) then begin
                     hum.BoChangeServer := FALSE;
                     hum.BoChangeServerOK := FALSE;
                     hum.BoChangeServerNeedDelay := TRUE;
                     hum.ChangeServerDelayTime := GetTickCount;
                  end else
                     Inc (hum.WriteChangeServerInfoCount);
               end;
            end;
            if hum.BoChangeServerNeedDelay then begin
               if (hum.BoChangeServerOK) or (GetTickCount - hum.ChangeServerDelayTime > 10 * 1000) then begin
                  hum.ClearAllSlaves;  // 清除所有召唤兽
                  hum.BoChangeServerNeedDelay := FALSE;
                  DoUserChangeServer (hum, hum.ChangeToServerNumber);
               end;
            end;
         end;
      end;
   except
      MainOutMessage ('[UsrEngn] ClosePlayer.Delete');
   end;

   lack := FALSE;
   try
      tcount := GetCurrentTime;
      i := HumCur;
      while TRUE do begin
         if i >= RunUserList.Count then break;
         hum := TUserHuman (RunUserList.Objects[i]);
         if tcount - hum.RunTime > hum.RunNextTick then begin
            hum.RunTime := tcount;
            if not hum.BoGhost then begin
               if not hum.LoginSign then begin
                  try
                     //pvDecodeSocketData (hum);
                     hum.RunNotice; // 发送公告
                  except
                     MainOutMessage ('[UsrEngn] Exception RunNotice in ProcessHumans');
                  end;
               end else
                  try
                     if not hum.ReadyRun then begin
                        hum.ReadyRun := TRUE;
                        hum.Initialize;  // 检查角色设置并登录
                     end else begin
                        if GetTickCount - hum.SearchTime > hum.SearchRate then begin
                           hum.SearchTime := GetTickCount;
                           hum.SearchViewRange;
                           hum.ThinkEtc;
                        end;

                        // 每5分钟发送一次滚动公告
                        if GetTickCount - hum.LineNoticeTime > 5 * 60 * 1000 then begin
                           hum.LineNoticeTime := GetTickCount;
                           if hum.LineNoticeNumber < LineNoticeList.Count then begin
                              // LineNoticeList和Hum在同一线程,无需加锁
                              // 但如果在不同线程则必须加锁
                              hum.SysMsg (LineNoticeList[hum.LineNoticeNumber], 2);
                           end;
                           Inc (hum.LineNoticeNumber);
                           if hum.LineNoticeNumber >= LineNoticeList.Count then
                              hum.LineNoticeNumber := 0;
                        end;

                        hum.Operate;

                        // 每15分钟自动保存一次(从10分钟改为15分钟)
                        if ( not FrontEngine.HasServerHeavyLoad ) and
                           ( GetTickCount > ( 15*60*1000 + hum.LastSaveTime) ) then
                        begin
                           hum.LastSaveTime := GetTickCount;
                           hum.ReadySave;
                           SavePlayer (hum);
                        end;
                     end;
                  except
                     MainOutMessage ('[UsrEngn] Exception Hum.Operate in ProcessHumans');
                  end;
            end else begin
               try
                  RunUserList.Delete (i);          bugcount := 2;
                  hum.Finalize;                 bugcount := 3;
               except
                  MainOutMessage ('[UsrEngn] Exception Hum.Finalize in ProcessHumans ' + IntToStr(bugcount));
               end;
               try
                  ClosePlayer (hum);            bugcount := 4;
                  hum.ReadySave;
                  SavePlayer (hum);
                  RunSocket.CloseUser (hum.GateIndex, hum.UserHandle);
               except
                  MainOutMessage ('[UsrEngn] Exception RunSocket.CloseUser in ProcessHumans ' + IntToStr(bugcount));
               end;
               SendInterMsg (ISM_USERLOGOUT, ServerIndex, hum.UserName+ ':' + IntToStr(hum.ApprovalMode));
               continue;
            end;
         end;
         Inc (i);

         if GetTickCount - start > HumLimitTime then begin
            // 超时,延迟到下次处理
            lack := TRUE;
            HumCur := i;
            break;
         end;
      end;
      if not lack then HumCur := 0;

   except
      MainOutMessage ('[UsrEngn] ProcessHumans');
   end;

   // 记录循环统计
   Inc (HumRotCount);
   if HumCur = 0 then begin  // 完成一轮循环
      HumRotCount := 0;
      humrotatecount := HumRotCount;
      k := GetTickCount - humrotatetime;  // 计算一轮耗时
      curhumrotatetime := k;
      humrotatetime := GetTickCount;
      if maxhumrotatetime < k then begin
         maxhumrotatetime := k;  // 更新最大耗时
      end;
   end;

   // 记录本次处理耗时
   curhumtime := GetTickCount - start;
   if maxhumtime < curhumtime then begin
      maxhumtime := curhumtime;
   end;
end;

{ ProcessMonsters
  功能: 处理所有怪物的逻辑
  实现原理:
    1. 处理怪物重生
    2. 遍历怪物列表执行逻辑
    3. 清理死亡怪物
  内部函数:
    GetZenTime - 根据用户数调整重生时间 }
procedure TUserEngine.ProcessMonsters;
   // GetZenTime: 根据用户数调整怪物重生时间
   // 每增加200人,怪物重生速度提高10%
   function GetZenTime (ztime: longword): longword;
   var
      r: Real;
   begin
      if ztime < 30 * 60 * 1000 then begin
         r := (GetUserCount - UserFullCount) / ZenFastStep;
         if r > 0 then begin
            if r > 6 then r := 6;  // 最多加速60%
            Result := ztime - Round ((ztime/10) * r);
         end else
            Result := ztime;
      end else
         Result := ztime;
   end;
var
   i, k, zcount: integer;
   start: longword;
   tcount: integer;
   cret: TCreature;
   pz: PTZenInfo;
   lack, goodzen: Boolean;
begin
   start := GetTickCount;
   pz    := nil;
   try
      lack := FALSE;
      tcount := GetCurrentTime;

      // 每200毫秒处理一次怪物重生
      pz := nil;
      if GetTickCount - onezentime > 200 then begin
         onezentime := GetTickCount;
         if GenCur < MonList.Count then
            pz := PTZenInfo (MonList[GenCur]);
         if GenCur < MonList.Count-1 then Inc(GenCur)
         else GenCur := 0;
         if pz <> nil then  begin
            // 冒险服务器不重生怪物
            if (pz.MonName <> '') and (not BoVentureServer) then begin
               // 检查是否到达重生时间
               if (pz.StartTime = 0) or (GetTickCount - pz.StartTime > GetZenTime(pz.ZenTime)) then begin
                  zcount := GetMonCount (pz);  // 获取当前怪物数量
                  goodzen := TRUE;
                  // 如果怪物数不足则重生
                  if pz.Count > zcount then
                     goodzen := RegenMonsters (pz, pz.Count - zcount);
                  if goodzen then
                     pz.StartTime := GetTickCount;
               end;
               LatestGenStr := pz.MonName + ',' + IntToStr(GenCur) + '/' + IntToStr(MonList.Count);
            end;
         end;
      end;

      MonCurRunCount := 0;

      // 遍历怪物列表执行逻辑
      for i:=MonCur to MonList.Count-1 do begin
         pz := PTZenInfo (MonList[i]);
         if MonSubCur < pz.Mons.Count then k := MonSubCur
         else k := 0;
         MonSubCur := 0;
         while TRUE do begin
            if k >= pz.Mons.Count then break;
            cret := TCreature (pz.Mons[k]);
            if not cret.BoGhost then begin
               if tcount - cret.RunTime > cret.RunNextTick then begin
                  cret.RunTime := tcount;
                  // 视野搜索
                  if GetTickCount  > ( cret.SearchRate + cret.SearchTime )then begin
                     cret.SearchTime := GetTickCount;
                     // 2003/03/18 优化:只有有引用对象或隐身模式才搜索
                     if(cret.RefObjCount > 0) or (cret.HideMode) then
                        cret.SearchViewRange
                     else
                        cret.RefObjCount := 0;
                  end;

                  // 执行怪物逻辑,出错时删除怪物
                  try
                      cret.Run;
                      Inc (MonCurRunCount);
                  except
                      pz.Mons.Delete (k);
                      cret.Free;
                  end;


               end;
               Inc (MonCurCount);
            end else begin
               // 5分钟后释放死亡怪物
               if( GetTickCount > ( 5 * 60 * 1000 + cret.GhostTime ))then begin
                  pz.Mons.Delete (k);
                  cret.Free;
                  continue;
               end;
            end;
            Inc (k);
            if GetTickCount - start > MonLimitTime then begin
               // 超时,怪物移动优先级较低
               LatestMonStr := cret.UserName + '/' + IntToStr(i) + '/' + IntToStr(k);
               lack := TRUE;
               MonSubCur := k;
               break;
            end;
         end;
         if lack then break;
      end;

      if i >= MonList.Count then begin
         MonCur := 0;
         MonCount := MonCurCount;
         MonCurCount := 0;
         MonRunCount := (MonRunCount + MonCurRunCount) div 2;
      end;

      if not lack then MonCur := 0
      else MonCur := i;

   except
      if pz <> nil then
         MainOutMessage ('[UsrEngn] ProcessMonsters : ' + pz.MonName)
      else
         MainOutMessage ('[UsrEngn] ProcessMonsters');
   end;

   curmontime := GetTickCount - start;
   if maxmontime < curmontime then begin
      maxmontime := curmontime;
   end;
end;

{ ProcessMerchants
  功能: 处理所有商人NPC的逻辑
  实现原理:
    1. 遍历商人列表
    2. 为每个商人执行视野搜索和运行逻辑
    3. 如果超时则下次继续处理 }
procedure TUserEngine.ProcessMerchants;
var
   i: integer;
   start: longword;
   tcount: integer;
   cret: TCreature;
   lack: Boolean;
begin
   start := GetTickCount;
   lack := FALSE;
   try
      tcount := GetCurrentTime;
      // 遍历商人列表
      for i:=MerCur to MerchantList.Count-1 do begin
         cret := TCreature (MerchantList[i]);
         if not cret.BoGhost then begin
            if (tcount - cret.RunTime > cret.RunNextTick) then begin
               // 视野搜索
               if GetTickCount - cret.SearchTime > cret.SearchRate then begin
                  cret.SearchTime := GetTickCount;
                  cret.SearchViewRange;
               end;
               // 执行运行逻辑
               if tcount - cret.RunTime > cret.RunNextTick then begin
                  cret.RunTime := tcount;
                  cret.Run;
               end;
            end;
         end;
         // 检查是否超时
         if GetTickCount - start > NpcLimitTime then begin
            MerCur := i;  // 记录当前位置,下次继续
            lack := TRUE;
            break;
         end;
      end;
      if not lack then
         MerCur := 0;  // 处理完成,重置游标
   except
      MainOutMessage ('[UsrEngn] ProcessMerchants');
   end;
end;

{ ProcessNpcs
  功能: 处理所有普通NPC的逻辑
  实现原理:
    1. 遍历NPC列表
    2. 为每个NPC执行视野搜索和运行逻辑
    3. 如果超时则下次继续处理 }
procedure TUserEngine.ProcessNpcs;
var
   i, tcount: integer;
   start: longword;
   cret: TCreature;
   lack: Boolean;
begin
   start := GetTickCount;
   lack := FALSE;
   try
      tcount := GetCurrentTime;
      // 遍历NPC列表
      for i:=NpcCur to NpcList.Count-1 do begin
         cret := TCreature (NpcList[i]);
         if not cret.BoGhost then begin
            if (tcount - cret.RunTime > cret.RunNextTick) then begin
               // 视野搜索
               if GetTickCount - cret.SearchTime > cret.SearchRate then begin
                  cret.SearchTime := GetTickCount;
                  cret.SearchViewRange;
               end;
               // 执行运行逻辑
               if tcount - cret.RunTime > cret.RunNextTick then begin
                  cret.RunTime := tcount;
                  cret.Run;
               end;
            end;
         end;
         // 检查是否超时
         if GetTickCount - start > NpcLimitTime then begin
            NpcCur := i;  // 记录当前位置,下次继续
            lack := TRUE;
            break;
         end;
      end;
      if not lack then
         NpcCur := 0;  // 处理完成,重置游标
   except
      MainOutMessage ('[UsrEngn] ProcessNpcs');
   end;
end;

{-------------------------- 任务系统 ----------------------------}

{ LoadMission
  功能: 加载任务文件并激活任务
  参数:
    flname - 任务文件名
  返回值: 加载成功返回TRUE,失败返回FALSE }
function  TUserEngine.LoadMission (flname: string): Boolean;
var
   mission: TMission;
begin
   mission := TMission.Create (flname);  // 创建任务对象
   if not mission.BoPlay then begin
      mission.Free;  // 加载失败,释放对象
      Result := FALSE;
   end else begin
      MissionList.Add (mission);  // 添加到任务列表
      Result := TRUE;
   end;
end;

{ StopMission
  功能: 停止指定任务
  参数:
    missionname - 任务名称
  返回值: 始终返回TRUE }
function  TUserEngine.StopMission (missionname: string): Boolean;
var
   i: integer;
begin
   // 遍历任务列表查找匹配的任务
   for i:=0 to MissionList.Count-1do begin
      if TMission(MissionList[i]).MissionName = missionname then begin
         TMission(MissionList[i]).BoPlay := FALSE;  // 设置为停止状态
         break;
      end;
   end;
   Result := TRUE;
end;

{ GetRandomDefStart
  功能: 获取随机默认起始位置
  参数:
    map - 输出的地图名称
    sx, sy - 输出的起始坐标
  实现原理:
    从起始点列表中随机选取一个,如果列表为空则使用默认值 }
procedure TUserEngine.GetRandomDefStart (var map: string; var sx, sy: integer);
var
   n: integer;
begin
   if StartPoints.Count > 0 then begin
      // 随机选取起始点
      if StartPoints.Count > 1 then n := Random(2)
      else n := 0;
      map := StartPoints[n];
      sx := Loword(integer(StartPoints.Objects[n]));  // 获取X坐标
      sy := Hiword(integer(StartPoints.Objects[n]));  // 获取Y坐标
   end else begin
      // 使用默认起始位置
      map := DefHomeMap;
      sx := DefHomeX;
      sy := DefHomeY;
   end;
end;

{ ProcessMissions
  功能: 处理所有任务的逻辑
  实现原理:
    1. 遍历任务列表
    2. 如果任务正在运行则执行任务逻辑
    3. 否则释放任务并从列表删除 }
procedure TUserEngine.ProcessMissions;
var
   i: integer;
begin
   try
      // 倒序遍历任务列表
      for i:=MissionList.Count-1 downto 0 do begin
         if TMission(MissionList[i]).BoPlay then begin
            TMission(MissionList[i]).Run;  // 执行任务逻辑
         end else begin
            TMission(MissionList[i]).Free;  // 释放任务
            MissionList.Delete (i);         // 从列表删除
         end;
      end;
   except
      MainOutMessage ('[UsrEngn] ProcessMissions');
   end;
end;


{----------------------- 主执行循环 --------------------------}

{ Initialize
  功能: 初始化用户引擎
  实现原理:
    1. 加载怪物重生信息
    2. 初始化商人和NPC
    3. 为每个怪物生成点设置怪物种族类型 }
procedure TUserEngine.Initialize;
var
   i: integer;
   pz: PTZenInfo;
begin
   LoadRefillCretInfos;   // 加载怪物重生信息
   InitializeMerchants;   // 初始化商人
   InitializeNpcs;        // 初始化NPC

   // 为每个怪物生成点设置怪物种族类型
   for i:=0 to MonList.Count-1 do begin
      pz := PTZenInfo (MonList[i]);
      if pz <> nil then begin
         pz.MonRace := GetMonRace (pz.MonName);
      end;
   end;

end;

{ ExecuteRun
  功能: 执行引擎主循环
  实现原理:
    1. 处理玩家逻辑
    2. 处理怪物逻辑
    3. 处理商人和NPC逻辑
    4. 每秒处理任务、服务器等待超时、结界检查
    5. 每500毫秒检查门状态
    6. 每10分钟刷新公告和保存城堡数据
    7. 每10秒发送用户数、检查行会战超时、检查禁言时间 }
procedure TUserEngine.ExecuteRun;
var
   i: integer;
begin
   runonetime := GetTickCount;  // 记录开始时间
   try
      // 处理玩家逻辑
      ProcessUserHumans;

      // 处理怪物逻辑
      ProcessMonsters;

      // 处理商人逻辑
      ProcessMerchants;

      // 处理NPC逻辑
      ProcessNpcs;

      // 每秒执行一次
      if GetTickCount - missiontime > 1000 then begin
         missiontime := GetTickCount;

         ProcessMissions;          // 处理任务
         CheckServerWaitTimeOut;   // 检查服务器等待超时
         CheckHolySeizeValid;      // 检查结界有效性
      end;

      // 每500毫秒检查门状态
      if GetTickCount - opendoorcheck > 500 then begin
         opendoorcheck := GetTickCount;
         CheckOpenDoors;
      end;

      // 每10分钟执行一次
      if GetTickCount - timer10min > 10 * 60 * 1000 then begin
         timer10min := GetTickCount;
         NoticeMan.RefreshNoticeList;  // 刷新公告列表
         MainOutMessage (TimeToStr(Time) + ' User = ' + IntToStr(GetUserCount));
         UserCastle.SaveAll;           // 保存城堡数据
      end;

      // 每10秒执行一次
      if GetTickCount - timer10sec > 10 * 1000 then begin
         timer10sec := GetTickCount;
         FrmIDSoc.SendUserCount (GetRealUserCount);  // 发送用户数
         GuildMan.CheckGuildWarTimeOut;              // 检查行会战超时
         UserCastle.Run;                             // 城堡逻辑

         // 检查禁言时间是否结束
         for i:=ShutUpList.Count-1 downto 0 do begin
            if GetCurrentTime > integer(ShutUpList.Objects[i]) then
               ShutUpList.Delete(i);
         end;
      end;

   except
      MainOutMessage ('[UsrEngn] Raise Exception..');
   end;

   // 记录执行时间统计
   curusrcount := GetTickCount - runonetime;
   if maxusrtime < curusrcount then begin
      maxusrtime := curusrcount;
   end;
end;

{ ProcessUserMessage
  功能: 处理玩家发送的消息
  参数:
    hum - 玩家对象
    pmsg - 消息结构指针
    pbody - 消息体内容
  实现原理:
    1. 根据消息类型分发到不同的处理逻辑
    2. 移动/攻击类消息直接转发
    3. 带字符串参数的消息需要解码
    4. 如果玩家已准备好,某些消息会立即处理 }
procedure TUserEngine.ProcessUserMessage (hum: TUserHuman; pmsg: PTDefaultMessage; pbody: PChar);
var
   head, body, desc : string;
begin
   try
      if pmsg = nil then exit;
      // 获取消息体内容
      if pbody = nil then body := ''
      else body := StrPas(pbody);

      // 根据消息类型分发处理
      case pmsg.Ident of
         // 移动和攻击类消息
         CM_TURN,      // 转向
         CM_WALK,      // 行走
         CM_RUN,       // 跑步
         CM_HIT,       // 普通攻击
         CM_POWERHIT,  // 攻杀剑法
         CM_LONGHIT,   // 刺杀剑法
         CM_WIDEHIT,   // 半月弯刀
         CM_HEAVYHIT,  // 野蛮冲撞
         CM_BIGHIT,    // 烈火剑法
         CM_FIREHIT,   // 火焰刀
         // 2003/03/15 新武功
         CM_CROSSHIT,  // 翻天斩
         CM_SITDOWN:   // 坐下
            begin
               hum.SendMsg (hum, pmsg.Ident, pmsg.Tag, LoWord(pmsg.Recog), HiWord(pmsg.Recog), 0, '');
            end;
         CM_SPELL:     // 施法
            begin
               hum.SendMsg (hum, pmsg.Ident, pmsg.Tag, LoWord(pmsg.Recog), HiWord(pmsg.Recog), MakeLong(pmsg.Param, pmsg.Series), '');
            end;

         CM_QUERYUSERNAME:  // 查询用户名
            begin
               hum.SendMsg (hum, pmsg.Ident, 0, pmsg.Recog, pmsg.Param{x}, pmsg.Tag{y}, '');
            end;

         CM_SAY:       // 说话
            begin
               hum.SendMsg (hum, CM_SAY, 0, 0, 0, 0, DecodeString(body));
            end;
         // 带字符串参数的消息
         CM_DROPITEM,           // 丢弃物品
         CM_TAKEONITEM,         // 穿戴物品
         CM_TAKEOFFITEM,        // 脱下物品
         CM_EXCHGTAKEONITEM,    // 交换穿戴物品
         CM_MERCHANTDLGSELECT,  // 商人对话选择
         CM_MERCHANTQUERYSELLPRICE,   // 查询出售价格
         CM_MERCHANTQUERYREPAIRCOST,  // 查询修理费用
         CM_USERSELLITEM,       // 出售物品
         CM_USERREPAIRITEM,     // 修理物品
         CM_USERSTORAGEITEM,    // 存储物品
         CM_USERBUYITEM,        // 购买物品
         CM_USERGETDETAILITEM,  // 获取物品详情
         CM_CREATEGROUP,        // 创建队伍
         CM_ADDGROUPMEMBER,     // 添加队员
         CM_DELGROUPMEMBER,     // 删除队员
         CM_DEALTRY,            // 尝试交易
         CM_DEALADDITEM,        // 添加交易物品
         CM_DEALDELITEM,        // 删除交易物品
         CM_USERTAKEBACKSTORAGEITEM,  // 取回存储物品
         CM_USERMAKEDRUGITEM,   // 制作药品
         CM_GUILDADDMEMBER,     // 添加行会成员
         CM_GUILDDELMEMBER,     // 删除行会成员
         CM_GUILDUPDATENOTICE,  // 更新行会公告
         CM_GUILDUPDATERANKINFO // 更新行会联盟信息
         :
            begin
               hum.SendMsg (hum, pmsg.Ident, pmsg.Series, pmsg.Recog, pmsg.Param, pmsg.Tag, DecodeString(body));
            end;
         CM_ADJUST_BONUS        // 调整加点
         :
            begin
               hum.SendMsg (hum, pmsg.Ident, pmsg.Series, pmsg.Recog, pmsg.Param, pmsg.Tag, body);
            end;
         else
            // 其他消息直接转发
            hum.SendMsg (hum, pmsg.Ident, pmsg.Series, pmsg.Recog, pmsg.Param, pmsg.Tag, '');
      end;

      // 如果玩家已准备好,某些消息立即处理(多线程环境不能使用)
      if hum.ReadyRun then begin
         case pmsg.Ident of
            CM_TURN,
            CM_WALK,
            CM_RUN,
            CM_HIT,
            CM_POWERHIT,
            CM_LONGHIT,
            CM_WIDEHIT,
            CM_HEAVYHIT,
            CM_BIGHIT,
            CM_FIREHIT,
            // 2003/03/15 新武功
            CM_CROSSHIT,
            CM_SITDOWN:
               // 减少运行时间以立即处理
               hum.RunTime := hum.RunTime - 100;
         end;
      end;
   except
      MainOutMessage ('[Exception] ProcessUserMessage..');
   end;
end;

end.
