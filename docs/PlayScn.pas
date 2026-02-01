{ ============================================================================
  单元名称: PlayScn
  功能描述: 游戏场景管理单元，负责游戏主场景的渲染、角色管理和交互处理
  
  主要功能:
  - 游戏场景的初始化、打开、关闭和刷新
  - 地图瓦片和对象的绘制渲染
  - 角色(Actor)的创建、管理和销毁
  - 魔法效果的创建和渲染
  - 小地图的绘制显示
  - 屏幕坐标与地图坐标的转换
  - 角色选择和物品拾取检测
  - 消息系统的处理和分发
  
  作者: 传奇开发团队
  创建日期: 2003
  修改日期: 2003/03/15
============================================================================ }
unit PlayScn;    // 游戏场景单元

interface

uses
  Windows, Messages, SysUtils, Classes, Graphics, Controls, Forms, Dialogs,
  IntroScn, Grobal2, CliUtil, HUtil32,  Actor, HerbActor, AxeMon, SoundUtil,
  ClEvent, StdCtrls, clFunc, magiceff, extctrls,Textures;


const
   { 地图渲染相关常量 }
   MAPSURFACEWIDTH = 800;      // 地图表面宽度（像素）
   MAPSURFACEHEIGHT = 445;     // 地图表面高度（像素）
   LONGHEIGHT_IMAGE = 35;      // 高大物体的额外绘制行数
   FLASHBASE = 410;            // 物品闪光动画的基础图像索引
   AAX = 16;                   // 地图绘制X轴偏移量
   SOFFX = 0;                  // 屏幕X轴偏移量
   SOFFY = 0;                  // 屏幕Y轴偏移量



type
   { TPlayScene类
     功能: 游戏主场景管理类，负责游戏中所有视觉元素的渲染和管理
     用途: 
       - 管理地图的绘制和更新
       - 管理所有角色(Actor)的生命周期
       - 处理魔法效果的显示
       - 处理用户输入和交互 }
   TPlayScene = class (TScene)
   private
      MapSurface: TTexture;       // 地图背景绘制表面
      ObjSurface: TTexture;       // 对象绘制表面
      MoveTime: longword;         // 移动计时器，用于控制移动频率
      MoveStepCount: integer;     // 移动步数计数器
      AniTime: longword;          // 动画计时器，用于控制动画帧率
      DefXX, DefYY: integer;      // 默认绘制偏移坐标
      MainSoundTimer: TTimer;     // 主背景音乐定时器
      MsgList: TList;             // 消息队列列表
      
      { DrawTileMap
        功能: 绘制地图瓦片层（背景层和中间层） }
      procedure DrawTileMap;
      
      { EdChatKeyPress
        功能: 聊天输入框的按键事件处理
        参数:
          Sender - 事件发送者
          Key - 按下的按键字符 }
      procedure EdChatKeyPress (Sender: TObject; var Key: Char);
      
      { SoundOnTimer
        功能: 背景音乐定时器事件，循环播放主题音乐
        参数:
          Sender - 事件发送者 }
      procedure SoundOnTimer (Sender: TObject);
   public
      EdChat: TEdit;                   // 聊天输入框控件
      ActorList,  TempList: TList;     // ActorList: 角色列表; TempList: 临时列表
      GroundEffectList: TList;         // 地面魔法效果列表（铺在地上的魔法）
      EffectList: TList;               // 魔法效果列表
      FlyList: TList;                  // 飞行物体列表（投掷的斧头、枪、箭等）

      { 构造函数: 创建场景对象并初始化各个列表和控件 }
      constructor Create;
      
      { 析构函数: 释放场景对象及其所有资源 }
      destructor Destroy; override;
      
      { 初始化: 创建地图和对象绘制表面 }
      procedure Initialize; override;
      
      { 结束化: 释放地图和对象绘制表面 }
      procedure Finalize; override;
      
      { 打开场景: 清除缓存并显示底部界面框 }
      procedure OpenScene; override;
      
      { 关闭场景: 停止声音并隐藏聊天框 }
      procedure CloseScene; override;
      
      { 场景开场动画: 预留的开场动画处理（当前未实现） }
      procedure OpeningScene; override;
      
      { 绘制小地图
        参数:
          surface - 绘制目标表面
          transparent - 是否半透明显示 }
      procedure DrawMiniMap (surface: TTexture; transparent: Boolean);
      
      { 运行: 场景逻辑更新，处理角色移动、魔法效果等 }
      procedure Run();
      
      { 播放场景: 渲染整个游戏场景到指定表面
        参数:
          MSurface - 渲染目标表面 }
      procedure PlayScene (MSurface: TTexture); override;
      
      { 屠宰动物: 查找指定位置附近的动物尸体
        参数:
          x, y - 地图坐标
        返回值: 找到的动物尸体角色，未找到返回nil }
      function  ButchAnimal (x, y: integer): TActor;

      { 查找角色: 通过角色ID查找角色
        参数:
          id - 角色识别ID
        返回值: 找到的角色对象，未找到返回nil }
      function  FindActor (id: integer): TActor;
      
      { 通过坐标查找角色: 通过地图坐标查找角色
        参数:
          x, y - 地图坐标
        返回值: 找到的角色对象 }
      function  FindActorXY (x, y: integer): TActor;
      
      { 验证角色有效性: 检查角色是否在角色列表中
        参数:
          actor - 要验证的角色
        返回值: 角色是否有效 }
      function  IsValidActor (actor: TActor): Boolean;
      
      { 创建新角色: 根据参数创建并添加新角色
        参数:
          chrid - 角色ID
          cx, cy - 初始坐标
          cdir - 方向
          cfeature - 外观特征（种族、发型、服装、武器）
          cstate - 状态
        返回值: 创建的角色对象 }
      function  NewActor (chrid: integer; cx, cy, cdir: word; cfeature, cstate: integer): TActor;
      
      { 角色死亡处理: 将死亡角色移动到列表最前面（优先绘制）
        参数:
          actor - 死亡的角色对象 }
      procedure ActorDied (actor: TObject);
      
      { 设置角色绘制层级: 调整角色在列表中的位置以改变绘制顺序
        参数:
          actor - 角色对象
          level - 绘制层级（0表示最先绘制） }
      procedure SetActorDrawLevel (actor: TObject; level: integer);
      
      { 清除所有角色: 登出时清除所有角色和魔法效果 }
      procedure ClearActors;
      
      { 删除角色: 通过ID删除角色
        参数:
          id - 角色ID
        返回值: 被删除的角色对象 }
      function  DeleteActor (id: integer): TActor;
      
      { 删除角色: 直接删除指定角色对象
        参数:
          actor - 要删除的角色对象 }
      procedure DelActor (actor: TObject);
      
      { 发送消息: 向场景发送各类游戏消息
        参数:
          ident - 消息标识
          chrid - 角色ID
          x, y - 坐标
          cdir - 方向
          feature - 外观特征
          state - 状态
          param - 额外参数
          str - 字符串参数 }
      procedure SendMsg (ident, chrid, x, y, cdir, feature, state, param: integer; str: string);

      { 创建新魔法效果: 创建并添加魔法效果到场景
        参数:
          aowner - 魔法施放者
          magid - 魔法服务器ID
          magnumb - 魔法编号
          cx, cy - 起始坐标
          tx, ty - 目标坐标
          targetcode - 目标角色代码
          mtype - 魔法类型
          Recusion - 是否循环
          anitime - 动画时间
          bofly - 返回是否为飞行魔法 }
      procedure NewMagic (aowner: TActor;
                          magid, magnumb, cx, cy, tx, ty, targetcode: integer;
                          mtype: TMagicType;
                          Recusion: Boolean;
                          anitime: integer;
                          var bofly: Boolean);
      
      { 删除魔法效果: 通过ID删除魔法效果
        参数:
          magid - 魔法服务器ID }
      procedure DelMagic (magid: integer);
      
      { 创建新飞行物体: 创建箭矢、斧头等飞行物体
        参数:
          aowner - 所有者
          cx, cy - 起始坐标
          tx, ty - 目标坐标
          targetcode - 目标代码
          mtype - 魔法类型
        返回值: 创建的魔法效果对象 }
      function  NewFlyObject (aowner: TActor; cx, cy, tx, ty, targetcode: integer;  mtype: TMagicType): TMagicEff;

      { 地图坐标转屏幕坐标: 将地图格子坐标转换为屏幕像素坐标
        参数:
          cx, cy - 地图坐标
          sx, sy - 输出的屏幕坐标 }
      procedure ScreenXYfromMCXY (cx, cy: integer; var sx, sy: integer);
      
      { 鼠标坐标转地图坐标: 将屏幕鼠标位置转换为地图格子坐标
        参数:
          mx, my - 鼠标屏幕坐标
          ccx, ccy - 输出的地图坐标 }
      procedure CXYfromMouseXY (mx, my: integer; var ccx, ccy: integer);
      
      { 获取角色: 通过屏幕坐标精确选择角色（像素级别）
        参数:
          x, y - 屏幕坐标
          wantsel - 想要选择的索引
          nowsel - 返回当前选中索引
          liveonly - 是否只选择活着的角色
        返回值: 选中的角色 }
      function  GetCharacter (x, y, wantsel: integer; var nowsel: integer; liveonly: Boolean): TActor;
      
      { 获取攻击焦点角色: 通过屏幕坐标选择角色（范围更大）
        参数:
          x, y - 屏幕坐标
          wantsel - 想要选择的索引
          nowsel - 返回当前选中索引
          liveonly - 是否只选择活着的角色
        返回值: 选中的角色 }
      function  GetAttackFocusCharacter (x, y, wantsel: integer; var nowsel: integer; liveonly: Boolean): TActor;
      
      { 是否选中自己: 检查屏幕坐标是否选中了自己的角色
        参数:
          x, y - 屏幕坐标
        返回值: 是否选中自己 }
      function  IsSelectMyself (x, y: integer): Boolean;
      
      { 获取掉落物品: 通过屏幕坐标获取地上的物品
        参数:
          x, y - 屏幕坐标
          inames - 返回物品名称列表
        返回值: 掉落物品指针 }
      function  GetDropItems (x, y: integer; var inames: string): PTDropItem;
      
      { 能否跑步: 检查从起点到终点是否可以跑步
        参数:
          sx, sy - 起始坐标
          ex, ey - 终点坐标
        返回值: 是否可以跑步 }
      function  CanRun (sx, sy, ex, ey: integer): Boolean;
      
      { 能否行走: 检查指定位置是否可以行走
        参数:
          mx, my - 地图坐标
        返回值: 是否可以行走 }
      function  CanWalk (mx, my: integer): Boolean;
      
      { 碰撞检测: 检查指定位置是否有其他角色
        参数:
          mx, my - 地图坐标
        返回值: 是否有角色占位 }
      function  CrashMan (mx, my: integer): Boolean;
      
      { 能否飞越: 检查指定位置是否可以飞越
        参数:
          mx, my - 地图坐标
        返回值: 是否可以飞越 }
      function  CanFly (mx, my: integer): Boolean;
      
      { 刷新场景: 重新加载所有角色的图像资源 }
      procedure RefreshScene;
      
      { 清理对象: 切换地图时清理除自己以外的所有对象 }
      procedure CleanObjects;
   end;


implementation

uses
   ClMain, FState, Path, GameImages;


{ TPlayScene.Create
  功能: 构造函数，创建游戏场景对象
  实现原理:
    1. 初始化地图和对象绘制表面为nil
    2. 创建各类管理列表（消息、角色、效果等）
    3. 创建并配置聊天输入框控件
    4. 初始化移动和动画计时器
    5. 创建背景音乐定时器 }
constructor TPlayScene.Create;
begin
   MapSurface := nil;                        // 地图表面初始化为空
   ObjSurface := nil;                        // 对象表面初始化为空
   MsgList := TList.Create;                  // 创建消息队列列表
   ActorList := TList.Create;                // 创建角色列表
   TempList := TList.Create;                 // 创建临时列表
   GroundEffectList := TList.Create;         // 创建地面效果列表
   EffectList := TList.Create;               // 创建魔法效果列表
   FlyList := TList.Create;                  // 创建飞行物体列表

   // 创建聊天输入框控件
   EdChat := TEdit.Create (FrmMain.Owner);
   with EdChat do begin
      Parent := FrmMain;                     // 父控件为主窗体
      BorderStyle := bsNone;                 // 无边框样式
      OnKeyPress := EdChatKeyPress;          // 设置按键事件处理
      Visible := FALSE;                      // 初始隐藏
      MaxLength := 70;                       // 最大输入长度70字符
      Ctl3D := FALSE;                        // 禁用3D效果
      Left   := 208;                         // X坐标位置
      Top    := SCREENHEIGHT - 19;           // Y坐标位置（屏幕底部）
      Height := 12;                          // 高度
      Width  := 387;                         // 宽度
      Color := clSilver;                     // 背景色为银色
   end;
   MoveTime := GetTickCount;                 // 初始化移动计时
   AniTime := GetTickCount;                  // 初始化动画计时
   MainAniCount := 0;                        // 主动画计数器清零
   MoveStepCount := 0;                       // 移动步数计数器清零
   
   // 创建背景音乐定时器
   MainSoundTimer := TTimer.Create (FrmMain.Owner);
   with MainSoundTimer do begin
      OnTimer := SoundOnTimer;               // 设置定时器事件
      Interval := 1;                         // 初始间隔1毫秒
      Enabled := FALSE;                      // 初始禁用
   end;
end;

{ TPlayScene.Destroy
  功能: 析构函数，释放场景对象及其所有资源
  实现原理:
    1. 释放所有管理列表对象
    2. 调用父类析构函数 }
destructor TPlayScene.Destroy;
begin
   MsgList.Free;              // 释放消息列表
   ActorList.Free;            // 释放角色列表
   TempList.Free;             // 释放临时列表
   GroundEffectList.Free;     // 释放地面效果列表
   EffectList.Free;           // 释放魔法效果列表
   FlyList.Free;              // 释放飞行物体列表
   inherited Destroy;         // 调用父类析构
end;

{ TPlayScene.SoundOnTimer
  功能: 背景音乐定时器事件，循环播放游戏主题音乐
  参数:
    Sender - 事件发送者（定时器对象）
  实现原理:
    1. 播放主题音乐
    2. 设置下次播放间隔为46秒（音乐时长） }
procedure TPlayScene.SoundOnTimer (Sender: TObject);
begin
   PlaySound (s_main_theme);              // 播放主题音乐
   MainSoundTimer.Interval := 46 * 1000;  // 设置间隔46秒后再次播放
end;

{ TPlayScene.EdChatKeyPress
  功能: 聊天输入框按键事件处理
  参数:
    Sender - 事件发送者
    Key - 按下的按键字符
  实现原理:
    1. 按回车键(#13)时发送聊天内容并隐藏输入框
    2. 按ESC键(#27)时清空内容并隐藏输入框
    3. 关闭输入法以恢复游戏操作 }
procedure TPlayScene.EdChatKeyPress (Sender: TObject; var Key: Char);
begin
   // 处理回车键 - 发送聊天内容
   if Key = #13 then begin
      FrmMain.SendSay (EdChat.Text);      // 发送聊天内容到服务器
      EdChat.Text := '';                   // 清空输入框
      EdChat.Visible := FALSE;             // 隐藏输入框
      Key := #0;                           // 消耗按键事件
      SetImeMode (EdChat.Handle, imClose); // 关闭输入法
   end;
   // 处理ESC键 - 取消输入
   if Key = #27 then begin
      EdChat.Text := '';                   // 清空输入框
      EdChat.Visible := FALSE;             // 隐藏输入框
      Key := #0;                           // 消耗按键事件
      SetImeMode (EdChat.Handle, imClose); // 关闭输入法
   end;
end;

{ TPlayScene.Initialize
  功能: 初始化场景，创建绘制表面
  实现原理:
    1. 创建地图背景绘制表面（比显示区域稍大以支持滚动）
    2. 创建对象绘制表面 }
procedure TPlayScene.Initialize;
var
   i: integer;
begin
  // 创建地图表面，尺寸为显示区域加上边缘缓冲
  MapSurface := TTexture.Create();
  MapSurface.SetSize (MAPSURFACEWIDTH+UNITX*4+30, MAPSURFACEHEIGHT+UNITY*4);
  // 创建对象表面
  ObjSurface := TTexture.Create();
  ObjSurface.SetSize (MAPSURFACEWIDTH-SOFFX*2, MAPSURFACEHEIGHT);
end;

{ TPlayScene.Finalize
  功能: 结束化场景，释放绘制表面资源
  实现原理:
    1. 安全释放地图和对象绘制表面
    2. 将指针置空防止悬挂引用 }
procedure TPlayScene.Finalize;
begin
   if MapSurface <> nil then
      MapSurface.Free;           // 释放地图表面
   if ObjSurface <> nil then
      ObjSurface.Free;           // 释放对象表面
   MapSurface := nil;            // 指针置空
   ObjSurface := nil;            // 指针置空
end;

{ TPlayScene.OpenScene
  功能: 打开场景时的初始化操作
  实现原理:
    1. 清除图像缓存以释放内存
    2. 显示底部界面控制框 }
procedure TPlayScene.OpenScene;
begin
   WProgUse.ClearCache;          // 清除图像缓存
   FrmDlg.ViewBottomBox (TRUE);  // 显示底部界面框
end;

{ TPlayScene.CloseScene
  功能: 关闭场景时的清理操作
  实现原理:
    1. 停止所有声音播放
    2. 隐藏聊天输入框
    3. 隐藏底部界面框 }
procedure TPlayScene.CloseScene;
begin
   SilenceSound;                 // 停止所有声音
   EdChat.Visible := FALSE;      // 隐藏聊天输入框
   FrmDlg.ViewBottomBox (FALSE); // 隐藏底部界面框
end;

{ TPlayScene.OpeningScene
  功能: 场景开场动画处理（预留接口，当前未实现） }
procedure TPlayScene.OpeningScene;
begin
end;

{ TPlayScene.RefreshScene
  功能: 刷新场景，重新加载所有角色的图像资源
  实现原理:
    1. 重置地图客户区域标记强制重绘
    2. 遍历所有角色重新加载其图像表面 }
procedure TPlayScene.RefreshScene;
var
   i: integer;
begin
   Map.OldClientRect.Left := -1;           // 重置标记强制重绘地图
   for i:=0 to ActorList.Count-1 do
      TActor (ActorList[i]).LoadSurface;   // 重新加载角色图像
end;

{ TPlayScene.CleanObjects
  功能: 清理场景对象，切换地图时调用
  实现原理:
    1. 删除除玩家自己外的所有角色
    2. 清空消息队列
    3. 重置目标和焦点角色引用
    4. 清理所有魔法效果 }
procedure TPlayScene.CleanObjects;
var
   i: integer;
begin
   // 从后往前遍历删除非玩家角色
   for i := ActorList.Count-1 downto 0 do begin
      if TActor(ActorList[i]) <> Myself then begin
         TActor(ActorList[i]).Free;        // 释放角色对象
         ActorList.Delete (i);             // 从列表删除
      end;
   end;
   MsgList.Clear;                          // 清空消息队列
   TargetCret := nil;                      // 清空目标角色
   FocusCret := nil;                       // 清空焦点角色
   MagicTarget := nil;                     // 清空魔法目标
   
   // 清理地面魔法效果
   for i:=0 to GroundEffectList.Count-1 do
      TMagicEff (GroundEffectList[i]).Free;
   GroundEffectList.Clear;
   
   // 清理普通魔法效果
   for i:=0 to EffectList.Count-1 do
      TMagicEff (EffectList[i]).Free;
   EffectList.Clear;
end;

{---------------------- Draw Map -----------------------}
{ TPlayScene.DrawTileMap
  功能: 绘制地图瓦片层（背景层和中间层）
  实现原理:
    1. 检查地图客户区域是否发生变化，无变化则跳过绘制
    2. 清空地图表面
    3. 绘制背景层瓦片（2x2格子为单位的大瓦片）
    4. 绘制中间层瓦片（小型装饰物）
  注意事项:
    - 背景瓦片只在偶数坐标位置绘制
    - 使用逻辑地图单元进行边界检查 }
procedure TPlayScene.DrawTileMap;
var
   i,j, m,n, imgnum:integer;
   DSurface: TTexture;
begin
   with Map do
      if (ClientRect.Left = OldClientRect.Left) and (ClientRect.Top = OldClientRect.Top) then exit; // 如果客户区域没有变化，则退出
   Map.OldClientRect := Map.ClientRect;  // 记录当前客户区域
   MapSurface.Fill(0);                    // 清空地图表面
   
   // 绘制背景层瓦片
   with Map.ClientRect do begin
      m := -UNITY*2;                      // 初始化Y坐标偏移
      for j:=(Top - Map.BlockTop-1) to (Bottom - Map.BlockTop+1) do begin
         n := AAX + 14 -UNITX;            // 初始化X坐标偏移
         // 遍历地图从左到右
         for i:=(Left - Map.BlockLeft-2) to (Right - Map.BlockLeft+1) do begin
            // 检查坐标是否在有效范围内
            if (i >= 0) and (i < LOGICALMAPUNIT*3) and (j >= 0) and (j < LOGICALMAPUNIT*3) then begin
               imgnum := (Map.MArr[i, j].BkImg and $7FFF);  // 获取背景图像索引
               if imgnum > 0 then begin
                  // 背景瓦片只在2x2格子的偶数位置绘制
                  if (i mod 2 = 0) and (j mod 2 = 0) then begin
                     imgnum := imgnum - 1;
                     imgnum := ClampImageIndex(WTiles, imgnum);
                     DSurface := nil;
                     if imgnum >= 0 then
                       DSurface := WTiles.Images[imgnum];
                     if Dsurface <> nil then
                        MapSurface.Draw (n, m, DSurface.ClientRect, DSurface, FALSE); // 绘制背景瓦片
                  end;
               end;
            end;
            Inc (n, UNITX);                                 // X坐标前进一个单元
         end;
         Inc (m, UNITY);                                    // Y坐标前进一个单元
      end;
   end;
   
   // 绘制中间层瓦片（小型装饰物）
   with Map.ClientRect do begin
      m := -UNITY;                                          // 中间层Y偏移不同
      for j:=(Top - Map.BlockTop-1) to (Bottom - Map.BlockTop+1) do begin
         n := AAX + 14 -UNITX;
         for i:=(Left - Map.BlockLeft-2) to (Right - Map.BlockLeft+1) do begin
            if (i >= 0) and (i < LOGICALMAPUNIT*3) and (j >= 0) and (j < LOGICALMAPUNIT*3) then begin
               imgnum := Map.MArr[i, j].MidImg;             // 获取中间层图像索引
               if imgnum > 0 then begin
                  imgnum := imgnum - 1;
                  imgnum := ClampImageIndex(WSmTiles, imgnum);
                  DSurface := nil;
                  if imgnum >= 0 then
                    DSurface := WSmTiles.Images[imgnum];
                  if Dsurface <> nil then
                     MapSurface.Draw (n, m, DSurface.ClientRect, DSurface, TRUE); // 绘制中间层瓦片
               end;
            end;
            Inc (n, UNITX);
         end;
         Inc (m, UNITY);
      end;
   end;

end;

{-----------------------------------------------------------------------}
{ TPlayScene.DrawMiniMap
  功能: 绘制小地图显示
  参数:
    surface - 绘制目标表面
    transparent - 是否使用半透明模式绘制
  实现原理:
    1. 获取小地图图像资源
    2. 计算玩家在小地图上的位置
    3. 绘制小地图背景
    4. 在小地图上标记各类角色位置
       - 自己(白色255)、NPC/商人(颜色251)、怪物(颜色249)
    5. 绘制组队成员位置(颜色252)
    6. 清理过期的组队成员视图数据 }
procedure TPlayScene.DrawMiniMap (surface: TTexture; transparent: Boolean);
var
   d: TTexture;           // 小地图纹理
   v: Boolean;
   i, cl, ix, mx, my: integer;  // cl: 颜色值, mx/my: 小地图坐标
   rc: TRect;             // 裁剪区域
   actor: TActor;
begin

   d := WMMap.Images[MiniMapIndex];  // 获取当前地图的小地图图像
   if d <> nil then begin
      mx := (Myself.XX*48) div 32;
      my := (Myself.YY*32) div 32;
      rc.Left := _MAX(0, mx-60);
      rc.Top := _MAX(0, my-60);
      rc.Right := _MIN(d.ClientRect.Right, rc.Left + 120);
      rc.Bottom := _MIN(d.ClientRect.Bottom, rc.Top + 120);

      if transparent then
         DrawBlendEx (surface, (SCREENWIDTH-120), 0, d, rc.Left, rc.Top, 120, 120)
      else
         surface.Draw ((SCREENWIDTH-120), 0, rc, d, FALSE);

//    if ViewBlink then begin
         ix := (SCREENWIDTH-120) - rc.Left;
         // 2003/02/11 미니맵상에 다른 오브잭트들 출력
         if ActorList.Count > 0 then begin
            for i:=0 to ActorList.Count-1 do begin
                mx := ix + (TActor(ActorList[i]).XX*48) div 32;
                my := (TActor(ActorList[i]).YY*32) div 32 - rc.Top;
                cl := 0;
                case TActor(ActorList[i]).Race of
                   RC_USERHUMAN: if ( TActor(ActorList[i]) = Myself ) then cl := 255
                                 else           cl := 0;  // 사람 출력하지 않음...그룹원은 ViewList에서 출력
                   RCC_GUARD,
                   RCC_GUARD2,
                   RCC_MERCHANT:                cl := 251;
                   54, 55:                      cl := 0;  // 신수 출력하지 않음...ViewList에서 출력...250
                   98, 99:                      cl := 0;
                   else if ( (TActor(ActorList[i]).Visible) and (not TActor(ActorList[i]).Death) and (pos('(', TActor(ActorList[i]).UserName) = 0)) then cl := 249;
                end;
                if cl > 0 then begin
                    surface.Pixels[mx-1, my-1] := cl;
                    surface.Pixels[mx,   my-1] := cl;
                    surface.Pixels[mx+1, my-1] := cl;
                    surface.Pixels[mx-1, my]   := cl;
                    surface.Pixels[mx,   my]   := cl;
                    surface.Pixels[mx+1, my]   := cl;
                    surface.Pixels[mx-1, my+1] := cl;
                    surface.Pixels[mx,   my+1] := cl;
                    surface.Pixels[mx+1, my+1] := cl;
                end;
            end;
         end;
         if ViewListCount > 0 then begin
            for i:=1 to ViewListCount do begin
                if((abs(ViewList[i].x - Myself.XX) < 40) and (abs(ViewList[i].y - Myself.YY) < 40)) then begin
                   mx := ix + (ViewList[i].x*48) div 32;
                   my := (ViewList[i].y*32) div 32 - rc.Top;
                   cl := 252;
                   surface.Pixels[mx-1, my-1] := cl;
                   surface.Pixels[mx,   my-1] := cl;
                   surface.Pixels[mx+1, my-1] := cl;
                   surface.Pixels[mx-1, my]   := cl;
                   surface.Pixels[mx,   my]   := cl;
                   surface.Pixels[mx+1, my]   := cl;
                   surface.Pixels[mx-1, my+1] := cl;
                   surface.Pixels[mx,   my+1] := cl;
                   surface.Pixels[mx+1, my+1] := cl;
                end;
                // 오래됐으니 지우자...
                if(((GetTickCount - ViewList[i].LastTick) > 5000) and (ViewList[i].Index > 0)) then begin
                   // 2003/03/04 그룹원 탐기파연 설정
                   actor := FindActor (ViewList[i].Index);
                   if actor <> nil then begin
                      actor.BoOpenHealth := FALSE;
                   end;
                   // 아직 남은게 있다면 이동
                   if(ViewListCount > 0) then begin
                       ViewList[i].Index    := ViewList[ViewListCount].Index;
                       ViewList[i].x        := ViewList[ViewListCount].x;
                       ViewList[i].y        := ViewList[ViewListCount].y;
                       ViewList[i].LastTick := ViewList[ViewListCount].LastTick;
                       ViewList[ViewListCount].Index    := 0;
                       ViewList[ViewListCount].x        := 0;
                       ViewList[ViewListCount].y        := 0;
                       ViewList[ViewListCount].LastTick := 0;
                   end;
                   Dec(ViewListCount);
                end;
            end;
         end;
//    end;
   end;
end;


{-----------------------------------------------------------------------}
procedure TPlayScene.Run();
var
   i, j, k: integer;
   movetick: Boolean;      // 是否到达移动时间点
   pd: PTDropItem;         // 掉落物品指针
   evn: TClEvent;          // 客户端事件
   actor: TActor;          // 角色对象
   meff: TMagicEff;        // 魔法效果对象
begin
  if (MySelf = nil) then begin
    Exit;                  // 如果玩家角色不存在则退出
  end;
  DoFastFadeOut := FALSE;

  // 移动计时检查 - 每100ms允许一次移动
  movetick := FALSE;
  if GetTickCount - MoveTime >= 100 then begin
    MoveTime := GetTickCount;   // 重置移动计时
    movetick := TRUE;           // 标记可以移动
    Inc (MoveStepCount);
    if MoveStepCount > 1 then MoveStepCount := 0;
  end;

  // 动画计时检查 - 每50ms更新一次动画帧
  if GetTickCount - AniTime >= 50 then begin
    AniTime := GetTickCount;
    Inc (MainAniCount);
    if MainAniCount > 1000000 then MainAniCount := 0;  // 防止溢出
  end;

  // 处理所有角色的更新和消息
  try
    i := 0;
    while TRUE do begin              
      if i >= ActorList.Count then break;
      actor := ActorList[i];
      if movetick then actor.LockEndFrame := FALSE;  // 解锁帧限制
      if not actor.LockEndFrame then begin           // 如果没有锁定帧
        actor.ProcMsg;                               // 处理角色消息
        if movetick then
        if actor.Move(MoveStepCount) then begin      // 角色移动
          Inc (i);
          continue;
        end;
        actor.Run;                                   // 运行角色逻辑
        if actor <> Myself then actor.ProcHurryMsg;  // 处理紧急消息
      end;
      if actor = Myself then actor.ProcHurryMsg;
      
      // 处理角色变身
      if actor.WaitForRecogId <> 0 then begin
        if actor.IsIdle then begin                   // 如果角色处于空闲状态
          DelChangeFace (actor.WaitForRecogId);
          NewActor (actor.WaitForRecogId, actor.XX, actor.YY, actor.Dir, actor.WaitForFeature, actor.WaitForStatus);
          actor.WaitForRecogId := 0;
          actor.BoDelActor := TRUE;
        end;
      end;
      
      // 处理需要删除的角色
      if actor.BoDelActor then begin
        FreeActorList.Add (actor);                   // 添加到释放列表
        ActorList.Delete (i);                        // 从角色列表删除
        if TargetCret = actor then TargetCret := nil;
        if FocusCret = actor then FocusCret := nil;
        if MagicTarget = actor then MagicTarget := nil;
      end else Inc (i);
    end;
  except
    DebugOutStr ('101');
  end;

  try
    // 更新地面魔法效果
    i := 0;
    while TRUE do begin
      if i >= GroundEffectList.Count then break;
      meff := GroundEffectList[i];
      if meff.Active then begin
        if not meff.Run then begin                   // 如果效果结束
          meff.Free;
          GroundEffectList.Delete (i);
          continue;
        end;
      end;
      Inc (i);
    end;
    
    // 更新普通魔法效果
    i := 0;
    while TRUE do begin
      if i >= EffectList.Count then break;
      meff := EffectList[i];
      if meff.Active then begin
        if not meff.Run then begin                   // 魔法效果结束
          meff.Free;
          EffectList.Delete (i);
          continue;
        end;
      end;
      Inc (i);
    end;
    
    // 更新飞行物体（箭、斧头等）
    i := 0;
    while TRUE do begin
      if i >= FlyList.Count then break;
      meff := FlyList[i];
      if meff.Active then begin
        if not meff.Run then begin                   // 飞行物体到达目标
          meff.Free;
          FlyList.Delete (i);
          continue;
        end;
      end;
      Inc (i);
    end;
   
    EventMan.Execute;                               // 执行事件管理器
  except
    DebugOutStr ('102');
  end;

  try
    // 清理超出视野范围的掉落物品
    for k:=0 to DropedItemList.Count-1 do begin
      pd := PTDropItem (DropedItemList[k]);
      if pd <> nil then begin
        // 如果物品距离玩家超过30格则删除
        if (Abs(pd.x-Myself.XX) > 30) and (Abs(pd.y-Myself.YY) > 30) then begin
          Dispose (PTDropItem (DropedItemList[k]));
          DropedItemList.Delete (k);
          break;
        end;
      end;
    end;
     
    // 清理超出视野范围的地图事件
    for k:=0 to EventMan.EventList.Count-1 do begin
      evn := TClEvent (EventMan.EventList[k]);
      if (Abs(evn.X-Myself.XX) > 30) and (Abs(evn.Y-Myself.YY) > 30) then begin
        evn.Free;
        EventMan.EventList.Delete (k);
        break;
      end;
    end;
  except
    DebugOutStr ('103');
  end;


end;

{ TPlayScene.PlayScene
  功能: 渲染整个游戏场景到指定表面
  参数:
    MSurface - 渲染目标表面
  实现原理:
    1. 计算地图客户区域（以玩家为中心的18x17格范围）
    2. 更新地图位置并绘制地图瓦片
    3. 绘制48x32尺寸的小型对象
    4. 绘制地面魔法效果
    5. 绘制大型地图对象和动画对象
    6. 绘制掉落物品
    7. 按Y坐标顺序绘制所有角色
    8. 绘制飞行物体
    9. 绘制玩家自己（高亮显示）
    10. 绘制魔法效果
    11. 绘制物品闪光效果
    12. 如果玩家死亡则添加灰度效果
    13. 绘制小地图 }
procedure TPlayScene.PlayScene (MSurface: TTexture);
var
   i, j, k, n, m, mmm, ix, iy, line, defx, defy, wunit, fridx, ani, anitick, ax, ay, idx, drawingbottomline: integer;
   DSurface, d: TTexture;     // 绘制表面
   blend, movetick: Boolean;  // blend: 是否混合绘制
   pd: PTDropItem;            // 掉落物品指针
   evn: TClEvent;             // 客户端事件
   actor: TActor;             // 角色对象
   meff: TMagicEff;           // 魔法效果对象
   msgstr: string;            // 消息字符串
begin
  // 如果玩家角色不存在，显示等待消息
  if (Myself = nil) then begin
    msgstr := '正在连接游戏，请稍候...';
    with MSurface do
    BoldTextOut ((SCREENWIDTH-TextWidth(msgstr)) div 2, 200, msgstr, clWhite);
    exit;
  end;
  
  try
    // 计算地图可视区域（以玩家为中心）
    // 800x600分辨率下，地图范围为左右各9格，上下各8-9格
    with Map.ClientRect do begin
      Left   := MySelf.Rx - 9;
      Top    := MySelf.Ry - 9;
      Right  := MySelf.Rx + 9;                         
      Bottom := MySelf.Ry + 8;
    end;
    
    // 更新地图块位置
    Map.UpdateMapPos (Myself.Rx, Myself.Ry);

    if not g_boCanDraw then Exit;  // 如果不允许绘制则退出

    drawingbottomline := 450;      // 绘制底部边界线

    ObjSurface.Fill(0);            // 清空对象表面（0为透明色）

    DrawTileMap;                   // 绘制地图瓦片

    // 将地图表面绘制到对象表面上
    ObjSurface.Draw (0, 0,
                     Rect(UNITX*3 + Myself.ShiftX,UNITY*2 + Myself.ShiftY,
                          UNITX*3 + Myself.ShiftX + MAPSURFACEWIDTH,
                          UNITY*2 + Myself.ShiftY + MAPSURFACEHEIGHT),
                     MapSurface,
                     FALSE);
  except
    DebugOutStr ('104');
  end;

  // 计算默认绘制偏移坐标
  defx := -UNITX*2 - Myself.ShiftX + AAX + 14;
  defy := -UNITY*2 - Myself.ShiftY;
  DefXX := defx;
  DefYY := defy;
  
  // 绘制地图对象层（48x32尺寸的小物体）
  try
    m := defy - UNITY;
    for j:=(Map.ClientRect.Top - Map.BlockTop) to (Map.ClientRect.Bottom - Map.BlockTop + LONGHEIGHT_IMAGE) do begin
      if j < 0 then begin Inc (m, UNITY); continue; end;
      n := defx-UNITX*2;
      //*** 48*32 角寧몸膠竟鬼暠튬돨댕鬼
      for i:=(Map.ClientRect.Left - Map.BlockLeft-2) to (Map.ClientRect.Right - Map.BlockLeft+2) do begin
        if (i >= 0) and (i < LOGICALMAPUNIT*3) and (j >= 0) and (j < LOGICALMAPUNIT*3) then begin
          fridx := (Map.MArr[i, j].FrImg) and $7FFF;
          if fridx > 0 then begin
            ani := Map.MArr[i, j].AniFrame;
            wunit := Map.MArr[i, j].Area;
            if (ani and $80) > 0 then begin
              blend := TRUE;
              ani := ani and $7F;
            end;
            if ani > 0 then begin
              anitick := Map.MArr[i, j].AniTick;
              fridx := fridx + (MainAniCount mod (ani + (ani*anitick))) div (1+anitick);
            end;
            if (Map.MArr[i, j].DoorOffset and $80) > 0 then begin //열림
              if (Map.MArr[i, j].DoorIndex and $7F) > 0 then  //문으로 표시된 것만
                  fridx := fridx + (Map.MArr[i, j].DoorOffset and $7F); //열린 문
            end;
            fridx := fridx - 1;
            // 혤暠튬
            DSurface := GetObjs (wunit, fridx);
            if DSurface <> nil then begin
              if (DSurface.Width=48) and (DSurface.Height=32) then begin
                mmm := m + UNITY - DSurface.Height;
                if (n+DSurface.Width > 0) and (n <= SCREENWIDTH) and (mmm + DSurface.Height > 0) and (mmm < drawingbottomline) then begin
                  ObjSurface.Draw (n, mmm, DSurface.ClientRect, Dsurface, TRUE)
                end else begin
                  if mmm < drawingbottomline then begin //불필요하게 그리는 것을 피함
                    ObjSurface.Draw (n, mmm, DSurface.ClientRect, DSurface, TRUE)
                  end;
                end;
              end;
            end;
          end;
        end;
        Inc (n, UNITX);
      end;
      Inc (m, UNITY);
    end;

   //뺌뒈충膠竟槻벎
    for k:=0 to GroundEffectList.Count-1 do begin
      meff := TMagicEff(GroundEffectList[k]);
      meff.DrawEff (ObjSurface);
    end;

  except
    DebugOutStr ('105');
  end;

  try
    m := defy - UNITY;
    for j:=(Map.ClientRect.Top - Map.BlockTop) to (Map.ClientRect.Bottom - Map.BlockTop + LONGHEIGHT_IMAGE) do begin
      if j < 0 then begin Inc (m, UNITY); continue; end;
      n := defx-UNITX*2;
      //*** 배경오브젝트 그리기
      for i:=(Map.ClientRect.Left - Map.BlockLeft-2) to (Map.ClientRect.Right - Map.BlockLeft+2) do begin
        if (i >= 0) and (i < LOGICALMAPUNIT*3) and (j >= 0) and (j < LOGICALMAPUNIT*3) then begin
          fridx := (Map.MArr[i, j].FrImg) and $7FFF;
          if fridx > 0 then begin
            blend := FALSE;
            wunit := Map.MArr[i, j].Area;
            //에니메이션
            ani := Map.MArr[i, j].AniFrame;
            if (ani and $80) > 0 then begin
              blend := TRUE;
              ani := ani and $7F;
            end;
            if ani > 0 then begin
              anitick := Map.MArr[i, j].AniTick;
              fridx := fridx + (MainAniCount mod (ani + (ani*anitick))) div (1+anitick);
            end;
            if (Map.MArr[i, j].DoorOffset and $80) > 0 then begin //열림
              if (Map.MArr[i, j].DoorIndex and $7F) > 0 then  //문으로 표시된 것만
              fridx := fridx + (Map.MArr[i, j].DoorOffset and $7F); //열린 문
            end;
            fridx := fridx - 1;
            // 물체 그림
            if not blend then begin
              DSurface := GetObjs (wunit, fridx);
              if DSurface <> nil then begin
                if (DSurface.Width<>48) or (DSurface.Height<>32) then begin
                  mmm := m + UNITY - DSurface.Height;
                  if (n+DSurface.Width > 0) and (n <= SCREENWIDTH) and (mmm + DSurface.Height > 0) and (mmm < drawingbottomline) then begin
                    ObjSurface.Draw (n, mmm, DSurface.ClientRect, Dsurface, TRUE)
                  end else begin
                    if mmm < drawingbottomline then begin //불필요하게 그리는 것을 피함
                     ObjSurface.Draw (n, mmm, DSurface.ClientRect, DSurface, TRUE)
                    end;
                  end;
                end;
              end;
            end else begin
              //鞫刻됐밟돨뒈렘
              DSurface := GetObjsEx (wunit, fridx, ax, ay);
              if DSurface <> nil then begin
                mmm := m + ay - 68; //UNITY - DSurface.Height;
                if (n > 0) and (mmm + DSurface.Height > 0) and (n + Dsurface.Width < SCREENWIDTH) and (mmm < drawingbottomline) then begin
                 DrawBlend (ObjSurface, n+ax-2, mmm, DSurface);
                end else begin
                  if mmm < drawingbottomline then begin //불필요하게 그리는 것을 피함
                   DrawBlend (ObjSurface, n+ax-2, mmm, DSurface);
                  end;
                end;
              end;
            end;
          end;

        end;
        Inc (n, UNITX);
      end;

      if (j <= (Map.ClientRect.Bottom - Map.BlockTop)) and (not BoServerChanging) then begin

        //*** 바닥에 변경된 흙의 흔적
        for k:=0 to EventMan.EventList.Count-1 do begin
          evn := TClEvent (EventMan.EventList[k]);
          if j = (evn.Y - Map.BlockTop) then begin
            evn.DrawEvent (ObjSurface,(evn.X-Map.ClientRect.Left)*UNITX + defx, m);
          end;
        end;

        //鞫刻뒈충膠틔棍近
        for k:=0 to DropedItemList.Count-1 do begin
          pd := PTDropItem (DropedItemList[k]);
          if pd <> nil then begin
            if j = (pd.y - Map.BlockTop) then begin
              d := WDnItem.Images[pd.Looks];
              if d <> nil then begin
                ix := (pd.x-Map.ClientRect.Left)*UNITX+defx + SOFFX; 
                iy := m;
                if pd = FocusItem then begin
                  ImgMixSurface.SetSize(d.Width, d.Height);
                  ImgMixSurface.Draw (0, 0, d.ClientRect, d, FALSE);
                  DrawEffect (ImgMixSurface, ceBright);
                  ObjSurface.Draw (ix + HALFX-(d.Width div 2),
                  iy + HALFY-(d.Height div 2),
                  d.ClientRect,
                  ImgMixSurface, TRUE);
                end else
                ObjSurface.Draw (ix + HALFX-(d.Width div 2),
                iy + HALFY-(d.Height div 2),
                d.ClientRect,
                d, TRUE);
              end;
            end;
          end;
        end;
        
        //鞫刻훙膠綱뺐斤口
        for k:=0 to ActorList.Count-1 do begin
          actor := ActorList[k];
          if (j = actor.Ry-Map.BlockTop-actor.DownDrawLevel) then begin
            actor.SayX := (actor.Rx-Map.ClientRect.Left)*UNITX + defx + actor.ShiftX + 24;
            if actor.Death then
              actor.SayY := m + UNITY + actor.ShiftY + 16 - 60  + (actor.DownDrawLevel * UNITY)
            else actor.SayY := m + UNITY + actor.ShiftY + 16 - 95  + (actor.DownDrawLevel * UNITY);
              actor.DrawChr (ObjSurface, (actor.Rx-Map.ClientRect.Left)*UNITX + defx,
              m + (actor.DownDrawLevel * UNITY),
              FALSE);
          end;
        end;
        
        //뺌령契침랬뒈렘
        for k:=0 to FlyList.Count-1 do begin
        meff := TMagicEff(FlyList[k]);
        if j = (meff.Ry - Map.BlockTop) then
        meff.DrawEff (ObjSurface);
        end;
      end;
      Inc (m, UNITY);
    end;
  except
    DebugOutStr ('106');
  end;

  if not BoServerChanging then begin
    try
      //뺌茶�榴檄
      if not CheckBadMapMode then
         if Myself.State and $00800000 = 0 then //투명이 아니면
            Myself.DrawChr (ObjSurface,
                           (Myself.Rx-Map.ClientRect.Left)*UNITX+defx,
                           (Myself.Ry-Map.ClientRect.Top-1)*UNITY+defy,
                           TRUE);

      //뎠품柑깃寧蕨돨실�
      if (FocusCret <> nil) then begin
         if IsValidActor (FocusCret) and (FocusCret <> Myself) then
            if FocusCret.State and $00800000 = 0 then //투명이 아니면
               FocusCret.DrawChr (ObjSurface,
                           (FocusCret.Rx-Map.ClientRect.Left)*UNITX+defx,
                           (FocusCret.Ry-Map.ClientRect.Top-1)*UNITY+defy, TRUE);
      end;
      //렴침랬돨珂빅밍膠멕좋鞫刻
      if (MagicTarget <> nil) then begin
         if IsValidActor (MagicTarget) and (MagicTarget <> Myself) then
            if MagicTarget.State and $00800000 = 0 then //투명이 아니면
               MagicTarget.DrawChr (ObjSurface,
                           (MagicTarget.Rx-Map.ClientRect.Left)*UNITX+defx,
                           (MagicTarget.Ry-Map.ClientRect.Top-1)*UNITY+defy, TRUE);
      end;
    except
       DebugOutStr ('108');
    end;
  end;
   
  try
    //뺌실�槻벎
    for k:=0 to ActorList.Count-1 do begin
      actor := ActorList[k];
      actor.DrawEff (ObjSurface,
                 (actor.Rx-Map.ClientRect.Left)*UNITX + defx,
                 (actor.Ry-Map.ClientRect.Top-1)*UNITY + defy);
    end;
    //뺌침랬槻벎
    for k:=0 to EffectList.Count-1 do begin
      meff := TMagicEff(EffectList[k]);
      meff.DrawEff (ObjSurface);
    end;
  except
    DebugOutStr ('109');
  end;

  //뒈충膠틔�좋
  try
    for k:=0 to DropedItemList.Count-1 do begin
      pd := PTDropItem (DropedItemList[k]);
      if pd <> nil then begin
        if GetTickCount - pd.FlashTime > 5 * 1000 then begin  //�舡
          pd.FlashTime := GetTickCount;
          pd.BoFlash := TRUE;
          pd.FlashStepTime := GetTickCount;
          pd.FlashStep := 0;
        end;
        if pd.BoFlash then begin
          if GetTickCount - pd.FlashStepTime >= 20 then begin
            pd.FlashStepTime := GetTickCount;
            Inc (pd.FlashStep);
          end;
          ix := (pd.x-Map.ClientRect.Left)*UNITX+defx + SOFFX;
          iy := (pd.y-Map.ClientRect.Top-1)*UNITY+defy + SOFFY;
          if (pd.FlashStep >= 0) and (pd.FlashStep < 10) then begin
            DSurface := WProgUse.GetCachedImage (FLASHBASE+pd.FlashStep, ax, ay);
            DrawBlend (ObjSurface, ix+ax, iy+ay, DSurface);
          end else pd.BoFlash := FALSE;
        end;
      end;
    end;
  except
   DebugOutStr ('110');
  end;

  //훙膠价空，鞫刻붚겜뺌충
  try
    if Myself.Death then
    DrawEffect(ObjSurface, ceGrayScale);
    MSurface.Draw (SOFFX, SOFFY, ObjSurface.ClientRect, ObjSurface, FALSE);
  except
    DebugOutStr ('111');
  end;

  //鬼뒈暠학뻣
  if ViewMiniMapStyle > 0 then begin
    if ViewMiniMapStyle = 1 then
      DrawMiniMap (MSurface, TRUE)
    else
      DrawMiniMap (MSurface, FALSE);
  end;
end;

{-------------------------------------------------------}
{ TPlayScene.NewMagic
  功能: 创建新的魔法效果并添加到场景中
  参数:
    aowner - 魔法施放者角色
    magid - 魔法服务器ID（用于防止重复创建）
    magnumb - 魔法编号（用于获取效果图像）
    cx, cy - 起始地图坐标
    tx, ty - 目标地图坐标
    targetcode - 目标角色代码
    mtype - 魔法类型（准备、飞行、爆炸、雷电等）
    Recusion - 是否循环播放
    anitime - 动画时间
    bofly - 返回是否为飞行类魔法
  实现原理:
    1. 检查魔法是否已存在（防止重复）
    2. 将地图坐标转换为屏幕坐标
    3. 根据魔法类型创建相应的效果对象
    4. 设置效果的各种属性（爆炸基础、帧时间、光效等）
    5. 将效果添加到效果列表中 }
procedure TPlayScene.NewMagic (aowner: TActor;
                               magid, magnumb, cx, cy, tx, ty, targetcode: integer;
                               mtype: TMagicType;
                               Recusion: Boolean;
                               anitime: integer;
                               var bofly: Boolean);
var
   i, scx, scy, sctx, scty, effnum: integer;
   meff: TMagicEff;           // 魔法效果对象
   target: TActor;            // 目标角色
   wimg: TGameImages;         // 图像库
begin
   bofly := FALSE;
   // 检查是否已存在相同ID的魔法效果（111号魔法可重复）
   if magid <> 111 then
      for i:=0 to EffectList.Count-1 do
         if TMagicEff(EffectList[i]).ServerMagicId = magid then
            exit;  // 已存在则退出
   ScreenXYfromMCXY (cx, cy, scx, scy);
   ScreenXYfromMCXY (tx, ty, sctx, scty);
   if magnumb > 0 then GetEffectBase (magnumb-1, 0, wimg, effnum)
   else effnum := -magnumb;
   target := FindActor (targetcode);

   meff := nil;
   case mtype of
      mtReady, mtFly, mtFlyAxe:
         begin
            meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
            meff.TargetActor := target;
            bofly := TRUE;
         end;
      mtExplosion:
         case magnumb of
            18: begin //뢰혼격
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.MagExplosionBase := 1570;
               meff.TargetActor := target;
               meff.NextFrameTime := 80;
            end;
            21: begin //폭열파
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.MagExplosionBase := 1660;
               meff.TargetActor := nil; //target;
               meff.NextFrameTime := 80;
               meff.ExplosionFrame := 20;
               meff.Light := 3;
            end;
            26: begin //탐기파연
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.MagExplosionBase := 3990;
               meff.TargetActor := target;
               meff.NextFrameTime := 80;
               meff.ExplosionFrame := 10;
               meff.Light := 2;
            end;
            27: begin //대회복술
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.MagExplosionBase := 1800;
               meff.TargetActor := nil; //target;
               meff.NextFrameTime := 80;
               meff.ExplosionFrame := 10;
               meff.Light := 3;
            end;
            30: begin //사자윤회
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.MagExplosionBase := 3930;
               meff.TargetActor := target;
               meff.NextFrameTime := 80;
               meff.ExplosionFrame := 16;
               meff.Light := 3;
            end;
            31: begin //빙설풍
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.MagExplosionBase := 3850;
               meff.TargetActor := nil; //target;
               meff.NextFrameTime := 80;
               meff.ExplosionFrame := 20;
               meff.Light := 3;
            end;
            else begin  //회복등..
               meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
               meff.TargetActor := target;
               meff.NextFrameTime := 80;
            end;
         end;
      mtFireWind:
         meff := nil;  //효과 없음
      mtFireGun: //화염방사
         meff := TFireGunEffect.Create (930, scx, scy, sctx, scty);
      mtThunder:
         begin
            //meff := TThuderEffect.Create (950, sctx, scty, nil); //target);
            meff := TThuderEffect.Create (10, sctx, scty, nil); //target);
            meff.ExplosionFrame := 6;
            meff.ImgLib := WMagic2;
         end;
      // 2003/03/15 신규무공 추가
      mtFireThunder:
         begin
            meff := TThuderEffect.Create (140, sctx, scty, nil); //target);
            meff.ExplosionFrame := 10;
            meff.ImgLib := WMagic2;
         end;

      mtLightingThunder:
         meff := TLightingThunder.Create (970, scx, scy, sctx, scty, target);
      mtExploBujauk:
         begin
            case magnumb of
               10: begin  //폭살계
                  meff := TExploBujaukEffect.Create (1160, scx, scy, sctx, scty, target);
                  meff.MagExplosionBase := 1360;
               end;
               17: begin  //대은신
                  meff := TExploBujaukEffect.Create (1160, scx, scy, sctx, scty, target);
                  meff.MagExplosionBase := 1540;
               end;
            end;
            bofly := TRUE;
         end;
      // 2003/03/04
      mtGroundEffect:
         begin
            meff := TMagicEff.Create (magid, effnum, scx, scy, sctx, scty, mtype, Recusion, anitime);
            if meff <> nil then begin
               meff.ImgLib := WMon21Img;
               meff.MagExplosionBase := 3580;
               meff.TargetActor := target;
               meff.Light := 3;
               meff.ExplosionFrame := 20;
            end;
//          bofly := TRUE;
         end;
      mtBujaukGroundEffect:
         begin
            meff := TBujaukGroundEffect.Create (1160, magnumb, scx, scy, sctx, scty);
            case magnumb of
               11: meff.ExplosionFrame := 16; //항마진법
               12: meff.ExplosionFrame := 16; //대지원호
            end;
            bofly := TRUE;
         end;
      mtKyulKai:
         begin
            meff := nil; //TKyulKai.Create (1380, scx, scy, sctx, scty);
         end;
   end;
   if meff = nil then exit;

   meff.TargetRx := tx;
   meff.TargetRy := ty;
   if meff.TargetActor <> nil then begin
      meff.TargetRx := TActor(meff.TargetActor).XX;
      meff.TargetRy := TActor(meff.TargetActor).YY;
   end;
   meff.MagOwner := aowner;
   EffectList.Add (meff);
end;


procedure TPlayScene.DelMagic (magid: integer);
var
   i: integer;
begin
   for i:=0 to EffectList.Count-1 do begin
      if TMagicEff(EffectList[i]).ServerMagicId = magid then begin
         TMagicEff(EffectList[i]).Free;
         EffectList.Delete (i);
         break;
      end;
   end;
end;

//cx, cy, tx, ty : 맵의 좌표
function  TPlayScene.NewFlyObject (aowner: TActor; cx, cy, tx, ty, targetcode: integer;  mtype: TMagicType): TMagicEff;
var
   i, scx, scy, sctx, scty: integer;
   meff: TMagicEff;
begin
   ScreenXYfromMCXY (cx, cy, scx, scy);
   ScreenXYfromMCXY (tx, ty, sctx, scty);
   case mtype of
      mtFlyArrow: meff := TFlyingArrow.Create (1, 1, scx, scy, sctx, scty, mtype, TRUE, 0);
      mtFireBall: meff := TFlyingFireBall.Create (1, 1, scx, scy, sctx, scty, mtype, TRUE, 0);
      else meff := TFlyingAxe.Create (1, 1, scx, scy, sctx, scty, mtype, TRUE, 0);
   end;
   meff.TargetRx := tx;
   meff.TargetRy := ty;
   meff.TargetActor := FindActor (targetcode);
   meff.MagOwner := aowner;
   FlyList.Add (meff);
   Result := meff;
end;

//전기쏘는 좀비의 마법처럼 길게 나가는 마법
//effnum: 각 번호마다 Base가 다 다르다.
{function  NewStaticMagic (aowner: TActor; tx, ty, targetcode, effnum: integer);
var
   i, scx, scy, sctx, scty, effbase: integer;
   meff: TMagicEff;
begin
   ScreenXYfromMCXY (cx, cy, scx, scy);
   ScreenXYfromMCXY (tx, ty, sctx, scty);
   case effnum of
      1: effbase := 340;   //좀비의 라이트닝의 시작 위치
      else exit;
   end;

   meff := TLightingEffect.Create (effbase, 1, 1, scx, scy, sctx, scty, mtype, TRUE, 0);
   meff.TargetRx := tx;
   meff.TargetRy := ty;
   meff.TargetActor := FindActor (targetcode);
   meff.MagOwner := aowner;
   FlyList.Add (meff);
   Result := meff;
end;  }

{-------------------------------------------------------}

//맵 좌표계로 셀 중앙의 스크린 좌표를 얻어냄
{procedure TPlayScene.ScreenXYfromMCXY (cx, cy: integer; var sx, sy: integer);
begin
   if Myself = nil then exit;
   sx := -UNITX*2 - Myself.ShiftX + AAX + 14 + (cx - Map.ClientRect.Left) * UNITX + UNITX div 2;
   sy := -UNITY*3 - Myself.ShiftY + (cy - Map.ClientRect.Top) * UNITY + UNITY div 2;
end; }

{ TPlayScene.ScreenXYfromMCXY
  功能: 将地图格子坐标转换为屏幕像素坐标（格子中心点）
  参数:
    cx, cy - 地图格子坐标
    sx, sy - 输出的屏幕像素坐标
  实现原理:
    以玩家位置为参考点，计算目标格子的屏幕位置 }
procedure TPlayScene.ScreenXYfromMCXY (cx, cy: integer; var sx, sy: integer);
begin
   if Myself = nil then exit;
   // 364和192是屏幕中心点偏移
   sx := (cx-Myself.Rx)*UNITX + 364 + UNITX div 2 - Myself.ShiftX;
   sy := (cy-Myself.Ry)*UNITY + 192 + UNITY div 2 - Myself.ShiftY;
end;

{ TPlayScene.CXYfromMouseXY
  功能: 将屏幕鼠标坐标转换为地图格子坐标
  参数:
    mx, my - 屏幕鼠标坐标
    ccx, ccy - 输出的地图格子坐标
  实现原理:
    以玩家位置为参考点，反向计算鼠标指向的地图格子 }
procedure TPlayScene.CXYfromMouseXY (mx, my: integer; var ccx, ccy: integer);
begin
   if Myself = nil then exit;
   ccx := UpInt((mx - 364 + Myself.ShiftX - UNITX) / UNITX) + Myself.Rx;
   ccy := UpInt((my - 192 + Myself.ShiftY - UNITY) / UNITY) + Myself.Ry;
end;

{ TPlayScene.GetCharacter
  功能: 通过屏幕坐标精确选择角色（像素级别检测）
  参数:
    x, y - 屏幕坐标
    wantsel - 想要选择的索引（用于多个角色重叠时切换）
    nowsel - 返回当前选中索引
    liveonly - 是否只选择活着的角色
  返回值: 选中的角色对象，未选中返回nil
  实现原理:
    从Y坐标由远到近遍历，检查鼠标是否点击到角色图像上 }
function  TPlayScene.GetCharacter (x, y, wantsel: integer; var nowsel: integer; liveonly: Boolean): TActor;
var
   k, i, ccx, ccy, dx, dy: integer;
   a: TActor;
begin
   Result := nil;
   nowsel := -1;
   CXYfromMouseXY (x, y, ccx, ccy);
   for k:=ccy+8 downto ccy-1 do begin
      for i:=ActorList.Count-1 downto 0 do
         if TActor(ActorList[i]) <> Myself then begin
            a := TActor(ActorList[i]);
            if (not liveonly or not a.Death) and (a.BoHoldPlace) and (a.Visible) then begin
               if a.YY = k then begin
                  //더 넓은 범위로 선택되게
                  dx := (a.Rx-Map.ClientRect.Left)*UNITX+DefXX + a.px + a.ShiftX;
                  dy := (a.Ry-Map.ClientRect.Top-1)*UNITY+DefYY + a.py + a.ShiftY;
                  if a.CheckSelect (x-dx, y-dy) then begin
                     Result := a;
                     Inc (nowsel);
                     if nowsel >= wantsel then
                        exit;
                  end;
               end;
            end;
         end;
   end;
end;

{ TPlayScene.GetAttackFocusCharacter
  功能: 通过屏幕坐标选择攻击目标角色（范围更大）
  参数:
    x, y - 屏幕坐标
    wantsel - 想要选择的索引
    nowsel - 返回当前选中索引
    liveonly - 是否只选择活着的角色
  返回值: 选中的角色对象
  实现原理:
    先尝试精确选择，若失败则使用更大的检测范围 }
function  TPlayScene.GetAttackFocusCharacter (x, y, wantsel: integer; var nowsel: integer; liveonly: Boolean): TActor;
var
   k, i, ccx, ccy, dx, dy, centx, centy: integer;
   a: TActor;
begin
   Result := GetCharacter (x, y, wantsel, nowsel, liveonly);
   if Result = nil then begin
      nowsel := -1;
      CXYfromMouseXY (x, y, ccx, ccy);
      for k:=ccy+8 downto ccy-1 do begin
         for i:=ActorList.Count-1 downto 0 do
            if TActor(ActorList[i]) <> Myself then begin
               a := TActor(ActorList[i]);
               if (not liveonly or not a.Death) and (a.BoHoldPlace) and (a.Visible) then begin
                  if a.YY = k then begin
                     //
                     dx := (a.Rx-Map.ClientRect.Left)*UNITX+DefXX + a.px + a.ShiftX;
                     dy := (a.Ry-Map.ClientRect.Top-1)*UNITY+DefYY + a.py + a.ShiftY;
                     if a.CharWidth > 40 then centx := (a.CharWidth - 40) div 2
                     else centx := 0;
                     if a.CharHeight > 70 then centy := (a.CharHeight - 70) div 2
                     else centy := 0;
                     if (x-dx >= centx) and (x-dx <= a.CharWidth-centx) and (y-dy >= centy) and (y-dy <= a.CharHeight-centy) then begin
                        Result := a;
                        Inc (nowsel);
                        if nowsel >= wantsel then
                           exit;
                     end;
                  end;
               end;
            end;
      end;
   end;
end;

{ TPlayScene.IsSelectMyself
  功能: 检查屏幕坐标是否选中了自己的角色
  参数:
    x, y - 屏幕坐标
  返回值: 是否选中自己
  实现原理:
    计算自己角色的屏幕位置，检查鼠标是否点击到角色图像上 }
function  TPlayScene.IsSelectMyself (x, y: integer): Boolean;
var
   k, i, ccx, ccy, dx, dy: integer;
begin
   Result := FALSE;
   CXYfromMouseXY (x, y, ccx, ccy);
   for k:=ccy+2 downto ccy-1 do begin
      if Myself.YY = k then begin
         // 计算角色屏幕位置
         dx := (Myself.Rx-Map.ClientRect.Left)*UNITX+DefXX + Myself.px + Myself.ShiftX;
         dy := (Myself.Ry-Map.ClientRect.Top-1)*UNITY+DefYY + Myself.py + Myself.ShiftY;
         // 检查是否点击到角色图像
         if Myself.CheckSelect (x-dx, y-dy) then begin
            Result := TRUE;
            exit;
         end;
      end;
   end;
end;

{ TPlayScene.GetDropItems
  功能: 通过屏幕坐标获取地上的掉落物品
  参数:
    x, y - 屏幕坐标
    inames - 返回物品名称列表（用\分隔）
  返回值: 掉落物品指针，未找到返回nil }
function  TPlayScene.GetDropItems (x, y: integer; var inames: string): PTDropItem;
var
   k, i, ccx, ccy, ssx, ssy, dx, dy: integer;
   d: PTDropItem;
   s: TTexture;
   c: byte;
begin
   Result := nil;
   CXYfromMouseXY (x, y, ccx, ccy);
   ScreenXYfromMCXY (ccx, ccy, ssx, ssy);
   dx := x - ssx;
   dy := y - ssy;
   inames := '';
   for i:=0 to DropedItemList.Count-1 do begin
      d := PTDropItem(DropedItemList[i]);
      if (d.X = ccx) and (d.Y = ccy) then begin
         s := WDnItem.Images[d.Looks];
         if s = nil then continue;
         dx := (x - ssx) + (s.Width div 2) - 3;
         dy := (y - ssy) + (s.Height div 2);
         c := s.Pixels[dx, dy];
         if c <> 0 then begin
            if Result = nil then Result := d;
            inames := inames + d.Name + '\';   
            //break;
         end;
      end;
   end;
end;

{ TPlayScene.CanRun
  功能: 检查从起点到终点是否可以跑步
  参数:
    sx, sy - 起始坐标
    ex, ey - 终点坐标
  返回值: 是否可以跑步
  实现原理: 检查中间点和终点是否都可行走 }
function  TPlayScene.CanRun (sx, sy, ex, ey: integer): Boolean;
var
   ndir, rx, ry: integer;
begin
   ndir := GetNextDirection (sx, sy, ex, ey);  // 获取方向
   rx := sx;
   ry := sy;
   GetNextPosXY (ndir, rx, ry);                 // 获取中间点
   if CanWalk (rx, ry) and CanWalk (ex, ey) then
      Result := TRUE
   else Result := FALSE;
end;

{ TPlayScene.CanWalk
  功能: 检查指定位置是否可以行走
  参数:
    mx, my - 地图坐标
  返回值: 是否可以行走
  实现原理: 检查地图是否可移动且没有角色占位 }
function  TPlayScene.CanWalk (mx, my: integer): Boolean;
begin
   Result := FALSE;
   if Map.CanMove(mx,my) then         // 检查地图是否可移动
      Result := not CrashMan (mx, my); // 检查是否有角色占位
end;

{ TPlayScene.CrashMan
  功能: 检查指定位置是否有其他角色占位
  参数:
    mx, my - 地图坐标
  返回值: 是否有角色占位
  实现原理: 遍历角色列表检查是否有可见的活着角色在该位置 }
function  TPlayScene.CrashMan (mx, my: integer): Boolean;
var
   i: integer;
   a: TActor;
begin
   Result := FALSE;
   for i:=0 to ActorList.Count-1 do begin
      a := TActor(ActorList[i]);
      if (a.Visible) and (a.BoHoldPlace) and (not a.Death) and (a.XX = mx) and (a.YY = my) then begin
         Result := TRUE;
         break;
      end;
   end;
end;

{ TPlayScene.CanFly
  功能: 检查指定位置是否可以飞越
  参数:
    mx, my - 地图坐标
  返回值: 是否可以飞越
  实现原理: 调用地图的飞行检查方法 }
function  TPlayScene.CanFly (mx, my: integer): Boolean;
begin
   Result := Map.CanFly (mx, my);  // 委托给地图对象检查
end;


{------------------------ Actor ------------------------}

{ TPlayScene.FindActor
  功能: 通过角色ID查找角色
  参数:
    id - 角色识别ID
  返回值: 找到的角色对象，未找到返回nil }
function  TPlayScene.FindActor (id: integer): TActor;
var
   i: integer;
begin
   Result := nil;
   for i:=0 to ActorList.Count-1 do begin
      if TActor(ActorList[i]).RecogId = id then begin
         Result := TActor(ActorList[i]);
         break;
      end;
   end;
end;

{ TPlayScene.FindActorXY
  功能: 通过地图坐标查找角色
  参数:
    x, y - 地图坐标
  返回值: 找到的角色对象，优先返回活着的可见角色 }
function  TPlayScene.FindActorXY (x, y: integer): TActor;
var
   i: integer;
begin
   Result := nil;
   for i:=0 to ActorList.Count-1 do begin
      if (TActor(ActorList[i]).XX = x) and (TActor(ActorList[i]).YY = y) then begin
         Result := TActor(ActorList[i]);
         // 优先返回活着的可见角色
         if not Result.Death and Result.Visible and Result.BoHoldPlace then
            break;
      end;
   end;
end;

{ TPlayScene.IsValidActor
  功能: 验证角色是否有效（是否在角色列表中）
  参数:
    actor - 要验证的角色对象
  返回值: 角色是否有效 }
function  TPlayScene.IsValidActor (actor: TActor): Boolean;
var
   i: integer;
begin
   Result := FALSE;
   for i:=0 to ActorList.Count-1 do begin
      if TActor(ActorList[i]) = actor then begin
         Result := TRUE;
         break;
      end;
   end;
end;

{ TPlayScene.NewActor
  功能: 创建新角色并添加到场景中
  参数:
    chrid - 角色ID
    cx, cy - 初始坐标
    cdir - 初始方向
    cfeature - 外观特征（包含种族、发型、服装、武器）
    cstate - 角色状态
  返回值: 创建的角色对象
  实现原理:
    1. 检查角色是否已存在
    2. 根据种族类型创建对应的角色类
    3. 设置角色属性
    4. 添加到角色列表 }
function  TPlayScene.NewActor (chrid:     integer;
                               cx:        word;    // X坐标
                               cy:        word;    // Y坐标
                               cdir:      word;    // 方向
                               cfeature:  integer; // 外观特征（种族、发型、服装、武器）
                               cstate:    integer): TActor;  // 状态
var
   i: integer;
   actor: TActor;
   pm: PTMonsterAction;
begin
   // 检查角色是否已存在
   for i:=0 to ActorList.Count-1 do
      if TActor(ActorList[i]).RecogId = chrid then begin
         Result := TActor(ActorList[i]);
         exit;  // 已存在则直接返回
      end;
   if IsChangingFace (chrid) then exit;  // 正在变身中则跳过

   case RACEfeature (cfeature) of
      0:  actor := THumActor.Create;
      9: actor := TSoccerBall.Create;  //축구공

      13: actor := TKillingHerb.Create;
      14: actor := TSkeletonOma.Create;
      15: actor := TDualAxeOma.Create;

      16: actor := TGasKuDeGi.Create;  //가스쏘는 구데기

      17: actor := TCatMon.Create;   //괭이, 우면귀(우면귀,창든우면귀,철퇴우면귀)
      18: actor := THuSuABi.Create;
      19: actor := TCatMon.Create;   //우면귀(우면귀,창든우면귀,철퇴든우면귀)

      20: actor := TFireCowFaceMon.Create;
      21: actor := TCowFaceKing.Create;
      22: actor := TDualAxeOma.Create;  //침쏘는 다크
      23: actor := TWhiteSkeleton.Create;  //소환백골

      24: actor := TSuperiorGuard.Create;  //멋있는 경비병

      30: actor := TCatMon.Create; //날개짓
      31: actor := TCatMon.Create; //날개짓
      32: actor := TScorpionMon.Create; //공격이 2동작

      33: actor := TCentipedeKingMon.Create;  //지네왕, 촉룡신
      34: actor := TBigHeartMon.Create;  //적월마, 심장
      35: actor := TSpiderHouseMon.Create;  //폭안거미
      36: actor := TExplosionSpider.Create;  //폭주
      37: actor := TFlyingSpider.Create;  //비독거미

      40: actor := TZombiLighting.Create;  //좀비 1 (전기 마법 좀비)
      41: actor := TZombiDigOut.Create;  //땅파고 나오는 좀비
      42: actor := TZombiZilkin.Create;

      43: actor := TBeeQueen.Create;

      45: actor := TArcherMon.Create;
      47: actor := TSculptureMon.Create;  //염소장군, 염소대장
      48: actor := TSculptureMon.Create;  //
      49: actor := TSculptureKingMon.Create;  //주마왕

      50: actor := TNpcActor.Create;

      52, 53: actor := TGasKuDeGi.Create;  //가스쏘는 쐐기나방, 둥
      54: actor := TSmallElfMonster.Create;
      55: actor := TWarriorElfMonster.Create;

      60: actor := TElectronicScolpionMon.Create;   //뢰혈사
      61: actor := TBossPigMon.Create;              //왕돈
      62: actor := TKingOfSculpureKingMon.Create;   //주마본왕(왕중왕)
      // 2003/02/11 신규 몹 추가 .. 해골본왕, 부식귀
      63: actor := TSkeletonKingMon.Create;
      64: actor := TGasKuDeGi.Create;
      65: actor := TSamuraiMon.Create;
      66: actor := TSkeletonSoldierMon.Create;
      67: actor := TSkeletonSoldierMon.Create;
      68: actor := TSkeletonSoldierMon.Create;
      69: actor := TSkeletonArcherMon.Create;
      70: actor := TBanyaGuardMon.Create;           //반야우사
      71: actor := TBanyaGuardMon.Create;           //반야좌사
      72: actor := TBanyaGuardMon.Create;           //사우천왕

      98: actor := TWallStructure.Create;
      99: actor := TCastleDoor.Create;  //성문...


      else actor := TActor.Create;
   end;

   with actor do begin
      RecogId := chrid;
      XX     := cx;
      YY     := cy;
      Rx := XX;
      Ry := YY;
      Dir    := cdir;
      Feature := cfeature;
      Race   := RACEfeature (cfeature);         //changefeature가 있을때만
      hair   := HAIRfeature (cfeature);         //변경된다.
      dress  := DRESSfeature (cfeature);
      weapon := WEAPONfeature (cfeature);
      Appearance := APPRfeature (cfeature);

      pm := RaceByPm (Race, Appearance);
      if pm <> nil then
         WalkFrameDelay := pm.ActWalk.ftime;

      if Race = 0 then begin
         Sex := dress mod 2;   //0:남자 1:여자
      end else
         Sex := 0;
      state  := cstate;
      Saying[0] := '';
   end;
   ActorList.Add (actor);
   Result := actor;
end;

{ TPlayScene.ActorDied
  功能: 处理角色死亡，调整绘制顺序
  参数:
    actor - 死亡的角色对象
  实现原理:
    将死亡角色移动到列表最前面，使其优先绘制（在底层） }
procedure TPlayScene.ActorDied (actor: TObject);
var
   i: integer;
   flag: Boolean;
begin
   // 先从列表中删除
   for i:=0 to ActorList.Count-1 do
      if ActorList[i] = actor then begin
         ActorList.Delete (i);
         break;
      end;
   // 插入到第一个活着的角色之前
   flag := FALSE;
   for i:=0 to ActorList.Count-1 do
      if not TActor(ActorList[i]).Death then begin
         ActorList.Insert (i, actor);
         flag := TRUE;
         break;
      end;
   if not flag then ActorList.Add (actor);
end;

{ TPlayScene.SetActorDrawLevel
  功能: 设置角色绘制层级
  参数:
    actor - 角色对象
    level - 绘制层级（0表示最先绘制）
  实现原理:
    将角色移动到列表指定位置以改变绘制顺序 }
procedure TPlayScene.SetActorDrawLevel (actor: TObject; level: integer);
var
   i: integer;
begin
   if level = 0 then begin  // 移动到列表开头（最先绘制）
      for i:=0 to ActorList.Count-1 do
         if ActorList[i] = actor then begin
            ActorList.Delete (i);
            ActorList.Insert (0, actor);
            break;
         end;
   end;
end;

{ TPlayScene.ClearActors
  功能: 清除所有角色，用于登出时清理
  实现原理:
    1. 释放所有角色对象
    2. 清空角色列表
    3. 重置所有角色引用
    4. 清理魔法效果 }
procedure TPlayScene.ClearActors;
var
   i: integer;
begin
   // 释放所有角色
   for i:=0 to ActorList.Count-1 do
      TActor(ActorList[i]).Free;
   ActorList.Clear;
   // 重置角色引用
   Myself := nil;
   TargetCret := nil;
   FocusCret := nil;
   MagicTarget := nil;

   // 清理魔法效果
   for i:=0 to EffectList.Count-1 do
      TMagicEff (EffectList[i]).Free;
   EffectList.Clear;
end;

{ TPlayScene.DeleteActor
  功能: 通过ID删除角色
  参数:
    id - 角色ID
  返回值: 被删除的角色对象
  实现原理:
    1. 查找指定ID的角色
    2. 清除相关引用
    3. 添加到释放列表延迟释放
    4. 从角色列表删除 }
function  TPlayScene.DeleteActor (id: integer): TActor;
var
   i: integer;
begin
   Result := nil;
   i := 0;
   while TRUE do begin
      if i >= ActorList.Count then break;
      if TActor(ActorList[i]).RecogId = id then begin
         // 清除相关引用
         if TargetCret = TActor(ActorList[i]) then TargetCret := nil;
         if FocusCret = TActor(ActorList[i]) then FocusCret := nil;
         if MagicTarget = TActor(ActorList[i]) then MagicTarget := nil;
         TActor(ActorList[i]).DeleteTime := GetTickCount;
         FreeActorList.Add (ActorList[i]);  // 添加到释放列表
         ActorList.Delete (i);               // 从列表删除
      end else
         Inc (i);
   end;
end;

{ TPlayScene.DelActor
  功能: 直接删除指定角色对象
  参数:
    actor - 要删除的角色对象
  实现原理:
    查找角色并添加到释放列表，从角色列表删除 }
procedure TPlayScene.DelActor (actor: TObject);
var
   i: integer;
begin
   for i:=0 to ActorList.Count-1 do
      if ActorList[i] = actor then begin
         TActor(ActorList[i]).DeleteTime := GetTickCount;  // 记录删除时间
         FreeActorList.Add (ActorList[i]);                  // 添加到释放列表
         ActorList.Delete (i);                              // 从列表删除
         break;
      end;
end;

{ TPlayScene.ButchAnimal
  功能: 屠宰动物，查找指定位置附近的动物尸体
  参数:
    x, y - 地图坐标
  返回值: 找到的动物尸体角色，未找到返回nil
  实现原理:
    遍历角色列表查找死亡的非人类角色（动物） }
function  TPlayScene.ButchAnimal (x, y: integer): TActor;
var
   i: integer;
   a: TActor;
begin
   Result := nil;
   for i:=0 to ActorList.Count-1 do begin
      a := TActor(ActorList[i]);
      // 查找死亡的非人类角色（动物尸体）
      if a.Death and (a.Race <> 0) then begin
         // 检查是否在指定位置附近（1格范围内）
         if (abs(a.XX - x) <= 1) and (abs(a.YY - y) <= 1) then begin
            Result := a;
            break;
         end;
      end;
   end;
end;


{------------------------- Msg -------------------------}

{ TPlayScene.SendMsg
  功能: 向场景发送并处理各类游戏消息
  参数:
    ident - 消息标识（SM_TEST、SM_NEWMAP、SM_LOGON等）
    chrid - 角色ID
    x, y - 坐标
    cdir - 方向
    feature - 外观特征
    state - 状态
    param - 额外参数
    str - 字符串参数（如地图名称、角色名称等）
  实现原理:
    根据消息类型执行不同的处理：
    - SM_TEST: 测试消息，创建测试角色
    - SM_CHANGEMAP/SM_NEWMAP: 切换地图
    - SM_LOGON: 玩家登录
    - SM_HIDE: 隐藏/删除角色
    - 其他: 转发给对应角色处理
  注意事项:
    消息需要缓冲是因为角色可能还有未处理完的消息 }
procedure TPlayScene.SendMsg (ident, chrid, x, y, cdir, feature, state, param: integer; str: string);
var
   actor: TActor;
begin
   case ident of
      // 测试消息：创建测试角色
      SM_TEST:
         begin
            actor := NewActor (111, 254{x}, 214{y}, 0, 0, 0);
            Myself := THumActor (actor);
            Map.LoadMap ('0', Myself.XX, Myself.YY);
         end;
      // 切换地图/新地图消息
      SM_CHANGEMAP,
      SM_NEWMAP:
         begin
            Map.LoadMap (str, x, y);           // 加载新地图
            DarkLevel_fake := cdir;             // 设置黑暗等级
            pDarkLevelCheck^ := cdir;

            // 服务器切换时平滑移动地图
            if (ident = SM_NEWMAP) and (Myself <> nil) then begin
               Myself.XX := x;
               Myself.YY := y;
               Myself.RX := x;
               Myself.RY := y;
               DelActor (Myself);
            end;

            // 处理小地图请求
            if BoWantMiniMap then begin
               if ViewMiniMapStyle > 0 then
                  PrevVMMStyle := ViewMiniMapStyle;
               ViewMiniMapStyle := 0;
               FrmMain.SendWantMiniMap;
            end;

         end;
         
      // 玩家登录消息
      SM_LOGON:
         begin
            actor := FindActor (chrid);
            if actor = nil then begin
               // 创建新角色
               actor := NewActor (chrid, x, y, Lobyte(cdir), feature, state);
               actor.ChrLight := Hibyte(cdir);  // 设置角色光效
               cdir := Lobyte(cdir);
               actor.SendMsg (SM_TURN, x, y, cdir, feature, state, '', 0);
            end;
            if Myself <> nil then begin
               Myself := nil;
            end;
            Myself := THumActor (actor);        // 设置为当前玩家
         end;
         
      // 隐藏/删除角色消息
      SM_HIDE:
         begin
            actor := FindActor (chrid);
            if actor <> nil then begin
               // 如果角色正在执行消失动画，等动画结束自动删除
               if actor.BoDelActionAfterFinished then begin
                  exit;
               end;
               // 如果角色正在变身，等变身结束自动删除
               if actor.WaitForRecogId <> 0 then begin
                  exit;
               end;
            end;
            DeleteActor (chrid);                // 删除角色
         end;
         
      // 其他消息：转发给对应角色处理
      else
         begin
            actor := FindActor (chrid);
            // 这些消息需要先创建角色（如果不存在）
            if (ident=SM_TURN) or (ident=SM_RUN) or (ident=SM_WALK) or
               (ident=SM_BACKSTEP) or
               (ident = SM_DEATH) or (ident = SM_SKELETON) or
               (ident = SM_DIGUP) or (ident = SM_ALIVE) then
            begin
               if actor = nil then
                  actor := NewActor (chrid, x, y, Lobyte(cdir), feature, state);
               if actor <> nil then begin
                  actor.ChrLight := Hibyte(cdir);
                  cdir := Lobyte(cdir);
                  // 骷髅状态处理
                  if ident = SM_SKELETON then begin
                     actor.Death := TRUE;
                     actor.Skeleton := TRUE;
                  end;
               end;
            end;
            if actor = nil then exit;
            
            case ident of
               // 外观改变消息
               SM_FEATURECHANGED:
                  begin
                     actor.Feature := feature;
                     actor.FeatureChanged;
                  end;
               // 角色状态改变消息
               SM_CHARSTATUSCHANGED:
                  begin
                     actor.State := Feature;
                     actor.HitSpeed := state;
                     // 如果是自己，更新行走和攻击速度
                     if actor = Myself then
                        ChangeWalkHitValues (Myself.Abil.Level
                                 , Myself.HitSpeed
                                 , Myself.Abil.Weight + Myself.Abil.MaxWeight
                                 + Myself.Abil.WearWeight + Myself.Abil.MaxWearWeight
                                 + Myself.Abil.HandWeight + Myself.Abil.MaxHandWeight
                                 , RUN_STRUCK_DELAY
                                             );
                  end;
               // 其他消息直接转发给角色
               else begin
                  // 转向消息：设置角色名称
                  if ident = SM_TURN then begin
                     if str <> '' then
                        actor.UserName := str;
                  end;
                  // 行走消息：设置行走帧延迟
                  if ident = SM_WALK then begin
                     if param > 0 then
                        actor.WalkFrameDelay := param;
                  end;
                  actor.SendMsg (ident, x, y, cdir, feature, state, '', 0);
               end;
            end;
         end;
   end;
end;


end.


