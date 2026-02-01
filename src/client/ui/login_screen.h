// =============================================================================
// Legend2 登录界面 (Login Screen)
// 
// 功能说明:
//   - 登录界面UI：用户名和密码输�?
//   - 角色选择界面：显示已有�?色列表供玩�?选择
//   - 角色创建界面：创建新角色（�?�择职业、�?�别、输入名称）
//   - 处理用户输入和界�?��互（鼠标点击、键盘输入等�?
//
// 涓昏缁勪欢:
//   - LoginScreenState: 登录界面状�?�枚举，控制当前显示的界�?
//   - TextInputField: 文本输入框组件，�?��光标、密码遮罩等功能
//   - Button: 按钮组件，支持悬停�?�按下�?��?用状�?
//   - CharacterSlot: 角色槽位组件，用于�?色�?�择界面
//   - LoginScreen: 登录界面主类，�?理所有登录相关的UI和�?�辑
//
// 使用流程:
//   1. 用户在登录界面输入账号密�?
//   2. 登录成功后进入�?色�?�择界面
//   3. 选择已有角色或创建新角色
//   4. 纭鍚庤繘鍏ユ父鎴?
// =============================================================================

#ifndef LEGEND2_LOGIN_SCREEN_H
#define LEGEND2_LOGIN_SCREEN_H

#include "render/renderer.h"
#include "ui/ui_renderer.h"
#include "ui/ui_layout_loader.h"
#include "ui/input_validation.h"
#include "ui/states/login_screen_state.h"
#include "core/event_dispatcher.h"
#include "scene/scene_manager.h"
#include "common/types.h"
#include "common/character_data.h"
#include "client/resource/resource_loader.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mir2::ui::screens {

// 引入公共类型定义
using mir2::common::CharacterClass;
using mir2::common::CharacterData;
using mir2::common::Color;
using mir2::common::Gender;
using mir2::common::Rect;

// 跨模块类型引用
using mir2::render::SDLRenderer;
using mir2::render::Texture;
using mir2::core::IEventListener;
using mir2::ui::UIRenderer;

// 前向声明（跨模块引用）
using mir2::client::ResourceManager;           // 来自 client/resource/resource_loader.h
using mir2::scene::SceneStateMachine;     // 来自 scene/scene_manager.h

class ILoginState;
struct LoginStateContext;

// =============================================================================
// 文本输入框组�?(Text Input Field)
// =============================================================================

/// @brief 文本输入框结构体
/// @details 提供基本的文�?��入功能，�?��光标定位、密码遮罩�?�占位�?等特�?
struct TextInputField {
    std::string text;           ///< 输入的文�?���?
    std::string placeholder;    ///< 占位符文�?��输入框为空时显示�?
    Rect bounds;                ///< 输入框的边界矩形（位�?��大小�?
    bool focused = false;       ///< 鏄惁鑾峰緱鐒︾偣锛堝綋鍓嶆槸鍚﹁閫変腑锛?
    bool password = false;      ///< �?��为密码模式（隐藏字�?显示�?�?
    int max_length = 20;        ///< �?大输入长度限�?
    int cursor_pos = 0;         ///< 光标位置（字符索引）
    
    /// @brief 处理字�?输入
    /// @param c 输入的字�?
    /// @details 在光标位�?��入字符，并将光标后移
    void input_char(char c);

    /// @brief 处理文本输入
    /// @param text 输入的文本（UTF-8）
    /// @details 在光标位置插入UTF-8文本，并保持编码有效
    void input_text(const char* text);
    
    /// @brief 处理�?格键
    /// @details 删除光标前的�?�?���?
    void backspace();
    
    /// @brief 处理删除�?
    /// @details 删除光标后的�?�?���?
    void delete_char();
    
    /// @brief 光标左移
    /// @details 将光标向左移动一�?���?
    void cursor_left();
    
    /// @brief 光标右移
    /// @details 将光标向右移动一�?���?
    void cursor_right();
    
    /// @brief 清空文本
    /// @details 清除�?有输入内容并重置光标位置
    void clear();
};

// =============================================================================
// 输入验证 (Input Validation)
// =============================================================================

// =============================================================================
// 按钮组件 (Button)
// =============================================================================

/// @brief 按钮结构�?
/// @details 鎻愪緵鍩烘湰鐨勬寜閽姛鑳斤紝鏀寔鎮仠銆佹寜涓嬨€佺鐢ㄧ瓑鐘舵€?
struct Button {
    std::string text;       ///< 按钮显示的文�?
    Rect bounds;            ///< 按钮的边界矩�?��位置和大小）
    bool hovered = false;   ///< 榧犳爣鏄惁鎮仠鍦ㄦ寜閽笂
    bool pressed = false;   ///< 按钮�?���?���?
    bool enabled = true;    ///< 按钮�?���?��（�?用时不响应点击）
    
    /// @brief 妫€鏌ョ偣鏄惁鍦ㄦ寜閽唴閮?
    /// @param x 点的X坐标
    /// @param y 点的Y坐标
    /// @return 如果点在按钮边界内返回true，否则返回false
    bool contains_point(int x, int y) const;
};

// =============================================================================
// 角色槽位组件 (Character Slot)
// =============================================================================

/// @brief 角色槽位结构�?
/// @details 用于角色选择界面，显示单�??色的信息和�?�择状�??
struct CharacterSlot {
    CharacterData data;     ///< 角色数据（名称�?�等级�?�职业等�?
    Rect bounds;            ///< 槽位的边界矩�?��位置和大小）
    bool hovered = false;   ///< 榧犳爣鏄惁鎮仠鍦ㄦЫ浣嶄笂
    bool selected = false;  ///< 妲戒綅鏄惁琚€変腑
    bool empty = true;      ///< 妲戒綅鏄惁涓虹┖锛堟病鏈夎鑹诧級
};

// =============================================================================
// 登录界面主类 (Login Screen)
// =============================================================================

/// @brief 登录和�?色�?�择界面�?
/// @details 管理整个登录流程的UI，包�?��
///          - 登录界面（用户名/密码输入�?
///          - 角色选择界面（显示已有�?色）
///          - 角色创建界面（创建新角色�?
///          - 连接�?��错�?提示界面
class LoginScreen : public IEventListener {
public:
    /// 动画帧数�?��包含纹理和偏移量�?
    struct AnimationFrame {
        std::shared_ptr<Texture> texture;
        int offset_x = 0;
        int offset_y = 0;
    };

    /// @brief 鏋勯€犲嚱鏁?
    /// @param renderer SDL渲染器引�?��用于基�?渲染
    /// @param ui_renderer UI渲染器引�?��用于UI元素渲染
    /// @param resource_manager 资源管理器引�?��用于加载角色预�?�?
    LoginScreen(SDLRenderer& renderer, UIRenderer& ui_renderer, ResourceManager& resource_manager);
    
    /// @brief 析构函数（使用默认实现）
    ~LoginScreen() = default;
    
    /// @brief 设置屏幕尺�?
    /// @param width 屏幕宽度（像素）
    /// @param height 屏幕高度（像素）
    /// @details 更新屏幕尺�?后会重新布局�?有UI元素
    void set_dimensions(int width, int height);
    
    /// @brief 获取当前界面状�??
    /// @return 褰撳墠鐨凩oginScreenState鏋氫妇鍊?
    LoginScreenState get_state() const { return state_; }

    /// @brief 设置状�?�机引用用于事件�?活判�?
    /// @param state_machine 场景状�?�机指针
    void set_state_machine(const SceneStateMachine* state_machine);
    
    /// @brief 设置界面状�??
    /// @param state 要切换到的界面状�?
    /// @details 切换状�?�时会触发相应界面的布局更新
    void set_state(LoginScreenState state);

    /// @brief 状态切换接口（状态机管理）
    /// @param new_state 新的状态对象
    void transition_to(std::unique_ptr<ILoginState> new_state);
    
    /// @brief 设置角色列表
    /// @param characters 从服务器获取的�?色数�?���?
    /// @details 用于角色选择界面显示玩�?已有的�?�?
    void set_character_list(const std::vector<CharacterData>& characters);
    
    /// @brief 设置错�?信息
    /// @param error 要显示的错�?信息文本
    /// @details 设置后会�?��切换到错�?��示状�?
    void set_error(const std::string& error);

    /// @brief 设置状�?�文�?
    /// @param status 状�?�文�?��用于连接�?��面显示）
    void set_status(const char* status);
    
    /// @brief 娓呴櫎閿欒淇℃伅
    /// @details 清除当前显示的错�?���?
    void clear_error();
    
    /// @brief 当前�?��接受事件
    bool is_active() const override;

    /// @brief 处理鼠标移动事件
    /// @param x 鼠标X坐标
    /// @param y 鼠标Y坐标
    /// @details 更新按钮和槽位的�?��状�??
    /// @return true 表示事件已消�?
    bool on_mouse_move(int x, int y) override;

    /// @brief 处理鼠标按下事件
    /// @param button 鼠标按键
    /// @param x 点击位置X坐标
    /// @param y 点击位置Y坐标
    /// @return 如果点击�??理返回true，否则返回false
    bool on_mouse_button_down(int button, int x, int y) override;

    /// @brief 处理鼠标释放事件
    bool on_mouse_button_up(int button, int x, int y) override;

    /// @brief 处理鼠标滚轮事件
    bool on_mouse_wheel(int x, int y) override;
    
    /// @brief 处理鼠标点击事件
    /// @param x 点击位置X坐标
    /// @param y 点击位置Y坐标
    /// @param button 榧犳爣鎸夐敭锛?=宸﹂敭锛?=涓敭锛?=鍙抽敭锛?
    /// @return 如果点击�??理返回true，否则返回false
    bool on_mouse_click(int x, int y, int button);
    
    /// @brief 处理�?��按键事件
    /// @param key SDL閿爜
    /// @return 如果按键�??理返回true，否则返回false
    /// @details 处理Tab切换焦点、Enter�??、方向键�?
    bool on_key_press(SDL_Keycode key);

    /// @brief 处理�?��按下事件
    bool on_key_down(SDL_Scancode key, SDL_Keycode keycode, bool repeat) override;

    /// @brief 处理�?��释放事件
    bool on_key_up(SDL_Scancode key, SDL_Keycode keycode) override;
    
    /// @brief 处理文本输入事件
    /// @param text 输入的文�?��UTF-8编码�?
    /// @details 将输入的文本添加到当前获得焦点的输入�?
    /// @return true 表示事件已消�?
    bool on_text_input(const char* text) override;
    
    /// @brief 更新界面
    /// @param delta_time 距上�?帧的时间间隔（�?�?
    /// @details 更新动画效果（�?光标�?��）和计时�?
    void update(float delta_time);
    
    /// @brief 渲染界面
    /// @details 根据当前状�?�渲染相应的界面内�?
    void render();

    /// @brief 设置登录界面背景纹理
    /// @param texture 背景纹理的共�?���?
    void set_background_texture(std::shared_ptr<Texture> texture);
    
    /// @brief 设置登录动画�?
    /// @param frames 动画帧纹理列�?
    void set_login_animation_frames(std::vector<AnimationFrame> frames);
    
    /// @brief �?查登录动画是否�?在播�?
    /// @return 正在�?��返回true
    bool is_login_animation_playing() const { return login_animation_playing_; }
    
    // =========================================================================
    // 角色创建界面资源设置 (Character Create Screen Resources)
    // =========================================================================
    
    /// @brief 设置角色创建界面背景纹理 (prguse.wil �?5�?
    void set_create_background_texture(std::shared_ptr<Texture> texture);
    
    /// @brief 设置职业选择面板纹理 (prguse.wil �?3�?
    void set_class_panel_texture(std::shared_ptr<Texture> texture);
    
    /// @brief 设置职业选择按钮纹理 (prguse.wil �?4-78�?
    void set_class_select_textures(std::vector<std::shared_ptr<Texture>> textures);
    
    /// @brief 设置职业选择高亮纹理 (prguse.wil �?5-59�?
    void set_class_highlight_textures(std::vector<std::shared_ptr<Texture>> textures);
    
    /// @brief 设置角色预�?�?(按职业和性别分类)
    /// @param char_class 角色职业
    /// @param gender 角色性别
    /// @param frames 动画帧列�?
    void set_character_preview_frames(CharacterClass char_class, Gender gender, std::vector<AnimationFrame> frames);

    // =========================================================================
    // 回调函数类型定义 (Callback Type Definitions)
    // =========================================================================
    
    /// @brief 登录回调函数类型
    /// @param username 鐢ㄦ埛鍚?
    /// @param password 密码
    using LoginCallback = std::function<void(const std::string& username, const std::string& password)>;
    
    /// @brief 角色选择回调函数类型
    /// @param character_id 选中角色的ID
    using CharacterSelectCallback = std::function<void(uint32_t character_id)>;
    
    /// @brief 角色创建回调函数类型
    /// @param name 角色名称
    /// @param char_class 角色职业
    /// @param gender 角色性别
    using CharacterCreateCallback = std::function<void(const std::string& name, CharacterClass char_class, Gender gender)>;

    /// @brief �?始游戏回调函数类�?
    using StartGameCallback = std::function<void()>;

    
    /// @brief 离线游戏回调函数类型
    using OfflinePlayCallback = std::function<void()>;
    
    /// @brief 设置登录回调
    /// @param callback 登录按钮点击时调用的回调函数
    void set_on_login(LoginCallback callback) { on_login_ = callback; }
    
    /// @brief 设置角色选择回调
    /// @param callback 选择角色�??时调用的回调函数
    void set_on_character_select(CharacterSelectCallback callback) { on_character_select_ = callback; }
    
    /// @brief 设置角色创建回调
    /// @param callback 创建角色�??时调用的回调函数
    void set_on_character_create(CharacterCreateCallback callback) { on_character_create_ = callback; }

    /// @brief 设置�?始游戏回�?
    /// @param callback 点击�?始游戏时调用的回调函�?
    void set_on_start_game(StartGameCallback callback) { on_start_game_ = callback; }

    /// @brief 设置离线游戏回调
    /// @param callback 离线游戏按钮点击时调用的回调函数
    void set_on_offline_play(OfflinePlayCallback callback) { on_offline_play_ = callback; }

    /// @brief 设置已创建�?色信�?��用于显示姓名/等级/职业�?
    /// @param name 角色名称
    /// @param char_class 角色职业
    /// @param gender 角色性别
    /// @param level 角色等级
    void set_created_character_info(const std::string& name, CharacterClass char_class, Gender gender, int level);
    
    // =========================================================================
    // 状�?�获取方�?(Getters)
    // =========================================================================
    
    /// @brief 获取输入的用户名
    /// @return 鐢ㄦ埛鍚嶅瓧绗︿覆鐨勫父閲忓紩鐢?
    const std::string& get_username() const { return username_field_.text; }
    
    /// @brief 获取选中的�?色索�?
    /// @return 选中角色的索引，-1表示�??�中
    int get_selected_character_index() const { return selected_character_index_; }
    
    /// @brief 获取创建角色时�?�择的职�?
    /// @return 选择的�?色职业枚举�??
    CharacterClass get_create_class() const { return create_class_; }
    
    /// @brief 获取创建角色时�?�择的�?�别
    /// @return 选择的�?色�?�别枚举�?
    Gender get_create_gender() const { return create_gender_; }
    
    /// @brief 获取创建角色时输入的名称
    /// @return 角色名称字�?串的常量引用
    const std::string& get_create_name() const { return create_name_field_.text; }
    
private:
    // =========================================================================
    // 渲染器引�?(Renderer References)
    // =========================================================================
    SDLRenderer& renderer_;         ///< SDL渲染器引�?
    UIRenderer& ui_renderer_;       ///< UI渲染器引�?
    ResourceManager& resource_manager_; ///< 资源管理器引�?
    UILayoutLoader layout_loader_;  ///< JSON驱动的UI布局加载�?
    
    // =========================================================================
    // 灞忓箷灏哄 (Screen Dimensions)
    // =========================================================================
    int width_ = 800;               ///< 屏幕宽度（默�?00像素�?
    int height_ = 600;              ///< 屏幕高度（默�?00像素�?
    
    // =========================================================================
    // 界面状�??(Screen State)
    // =========================================================================
    LoginScreenState state_ = LoginScreenState::LOGIN;  ///< 当前界面状�??
    const SceneStateMachine* state_machine_ = nullptr;  ///< 状�?�机指针（用于事件激活判�?��
    std::unique_ptr<LoginStateContext> state_context_;  ///< 状态共享上下文
    std::unique_ptr<ILoginState> current_state_;        ///< 当前状态对象
    std::string error_message_;     ///< 错�?信息文本
    float error_timer_ = 0.0f;      ///< 错�?显示计时�?��用于�?��隐藏�?
    std::string status_text_;       ///< 状�?�文�?��连接�?��面显示）
    
    // =========================================================================
    // 登录界面组件 (Login Screen Components)
    // =========================================================================
    TextInputField username_field_; ///< 用户名输入�?
    TextInputField password_field_; ///< 密码输入�?
    Rect login_confirm_bounds_;     ///< 背景“确定�?�按�?��点击区域（按背景坐标缩放�?
    Button offline_button_;         ///< 离线游戏按钮
    
    // =========================================================================
    // 角色选择界面组件 (Character Select Screen Components)
    // =========================================================================
    std::vector<CharacterSlot> character_slots_;    ///< 角色槽位列表
    int selected_character_index_ = -1;             ///< 当前选中的�?色索引（-1表示�??�中�?
    
    // =========================================================================
    // 角色创建界面组件 (Character Create Screen Components)
    // =========================================================================
    TextInputField create_name_field_;              ///< 角色名称输入�?
    CharacterClass create_class_ = CharacterClass::WARRIOR; ///< 选择的职业（默�?战士�?
    Gender create_gender_ = Gender::MALE;           ///< 选择的�?�别（默认男性）
    Button confirm_create_button_;  ///< �??创建按钮
    Button cancel_create_button_;   ///< 取消创建按钮
    
    // =========================================================================
    // 动画相关 (Animation)
    // =========================================================================
    float cursor_blink_timer_ = 0.0f;   ///< 光标�?��计时�?
    bool cursor_visible_ = true;        ///< 光标�?���??
    
    // 登录动画
    bool login_animation_playing_ = false;      ///< 登录动画�?��正在�?��
    int login_animation_frame_ = 0;             ///< 当前动画帧索�?
    float login_animation_timer_ = 0.0f;        ///< 动画帧�?时器
    float login_animation_frame_time_ = 0.1f;   ///< 每帧持续时间（�?�?
    std::string pending_username_;              ///< 待发送的用户名（动画完成后使�?��
    std::string pending_password_;              ///< 待发送的密码（动画完成后使用�?

    // =========================================================================
    // 纹理资源 (Texture Resources)
    // =========================================================================
    std::shared_ptr<Texture> background_texture_;       ///< 背景纹理
    
    std::vector<AnimationFrame> login_animation_frames_;  ///< 登录动画�?
    
    // 角色创建界面资源
    std::shared_ptr<Texture> create_background_texture_;  ///< 角色创建背景 (prguse.wil 65)
    std::shared_ptr<Texture> class_panel_texture_;        ///< 职业选择面板 (prguse.wil 73)
    std::vector<std::shared_ptr<Texture>> class_select_textures_;    ///< 职业选择按钮 (prguse.wil 74-78)
    std::vector<std::shared_ptr<Texture>> class_highlight_textures_; ///< 职业高亮 (prguse.wil 55-59)
    
    /// @brief 角色预�?动画帧（按职业和性别索引�?
    /// 索引: [职业 * 2 + 性别] = frames
    /// 职业: 0=战士, 1=法师, 2=道士
    /// 性别: 0=�? 1=�?
    std::vector<AnimationFrame> character_preview_frames_[6];
    
    // 角色预�?动画状�??
    int preview_animation_frame_ = 0;          ///< 当前预�?动画�?
    float preview_animation_timer_ = 0.0f;     ///< 预�?动画计时�?
    float preview_animation_frame_time_ = 0.15f; ///< 每帧持续时间
    
    // 角色创建界面图标选择状�??(74-78�?�?��标：3职业 + 2性别)
    int selected_create_class_index_ = -1;      ///< 选中的职业图标索�?(0=战士 1=法师 2=道士)
    int selected_create_gender_index_ = -1;     ///< 选中的�?�别图标索引 (0=�?1=�?
    bool class_panel_visible_ = false;         ///< 创建面板�?���??
    float class_panel_visibility_ = 0.0f;      ///< 创建面板显示进度(0=隐藏,1=显示)
    
    // 角色创建界面布局边界
    Rect create_button_bounds_;                ///< 创建新人物按�?���?(prguse.wil 69)
    Rect class_panel_bounds_;                  ///< 职业面板边界
    Rect class_select_bounds_[5];              ///< 职业选择按钮边界 (74-78�?
    Rect preview_area_bounds_;                 ///< 角色预�?区域边界
    Rect start_game_bounds_;                   ///< �?始游戏按�?���?(prguse.wil 65)
    Rect created_name_bounds_;                 ///< 创建后�?名显示区�?
    Rect created_level_bounds_;                ///< 创建后等级显示区�?
    Rect created_class_bounds_;                ///< 创建后职业显示区�?
    
    // =========================================================================
    // 回调函数 (Callbacks)
    // =========================================================================
    LoginCallback on_login_;                    ///< 登录回调
    CharacterSelectCallback on_character_select_;   ///< 角色选择回调
    CharacterCreateCallback on_character_create_;   ///< 角色创建回调
    StartGameCallback on_start_game_;               ///< �?始游戏回�?
    OfflinePlayCallback on_offline_play_;       ///< 离线游戏回调

    // 已创建�?色信�?��用于创建界面左下角信�?���?
    std::string created_character_name_;
    CharacterClass created_character_class_ = CharacterClass::WARRIOR;
    Gender created_character_gender_ = Gender::MALE;
    int created_character_level_ = 0;
    bool has_created_character_ = false;
    
    std::unique_ptr<ILoginState> build_state(LoginScreenState state);
    void refresh_layout();
};

/// @brief Enter reason for state initialization.
enum class LoginStateEnterReason {
    Transition,
    LayoutRefresh
};

/// @brief Shared context passed to login UI states.
struct LoginStateContext {
    SDLRenderer& renderer;
    UIRenderer& ui_renderer;
    ResourceManager& resource_manager;
    UILayoutLoader& layout_loader;
    int& screen_width;
    int& screen_height;
    LoginStateEnterReason enter_reason = LoginStateEnterReason::Transition;

    // Login input data
    TextInputField& username_field;
    TextInputField& password_field;
    Rect& login_confirm_bounds;
    Button& offline_button;

    // Character select data
    std::vector<CharacterSlot>& character_slots;
    int& selected_character_index;

    // Character create data
    TextInputField& create_name_field;
    CharacterClass& create_class;
    Gender& create_gender;
    Button& confirm_create_button;
    Button& cancel_create_button;
    int& selected_create_class_index;
    int& selected_create_gender_index;
    bool& class_panel_visible;
    float& class_panel_visibility;

    Rect& create_button_bounds;
    Rect& class_panel_bounds;
    Rect (&class_select_bounds)[5];
    Rect& preview_area_bounds;
    Rect& start_game_bounds;
    Rect& created_name_bounds;
    Rect& created_level_bounds;
    Rect& created_class_bounds;

    // Animation state
    float& cursor_blink_timer;
    bool& cursor_visible;
    bool& login_animation_playing;
    int& login_animation_frame;
    float& login_animation_timer;
    float& login_animation_frame_time;
    std::string& pending_username;
    std::string& pending_password;

    int& preview_animation_frame;
    float& preview_animation_timer;
    float& preview_animation_frame_time;

    // Textures/resources
    std::shared_ptr<Texture>& background_texture;
    std::shared_ptr<Texture>& create_background_texture;
    std::shared_ptr<Texture>& class_panel_texture;
    std::vector<std::shared_ptr<Texture>>& class_select_textures;
    std::vector<std::shared_ptr<Texture>>& class_highlight_textures;
    std::vector<LoginScreen::AnimationFrame>& login_animation_frames;
    std::vector<LoginScreen::AnimationFrame> (&character_preview_frames)[6];

    // Callbacks
    LoginScreen::LoginCallback& on_login;
    LoginScreen::CharacterSelectCallback& on_character_select;
    LoginScreen::CharacterCreateCallback& on_character_create;
    LoginScreen::StartGameCallback& on_start_game;
    LoginScreen::OfflinePlayCallback& on_offline_play;

    // Created character info
    std::string& created_character_name;
    CharacterClass& created_character_class;
    Gender& created_character_gender;
    int& created_character_level;
    bool& has_created_character;

    // Status/error
    std::string& error_message;
    float& error_timer;
    std::string& status_text;

    std::function<void(LoginScreenState)> transition_to;
    std::function<void(const std::string&)> set_error;
};

} // namespace mir2::ui::screens

#endif // LEGEND2_LOGIN_SCREEN_H
