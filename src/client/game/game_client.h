// =============================================================================
// Legend2 游戏客户端 (Game Client)
//
// 功能说明:
//   - 管理游戏主循环
//   - 处理客户端状态转换：登录、角色选择、游戏中等
//   - 协调各子系统（渲染、网络、输入、音频等）
//
// 主要组件:
//   - GameState: 客户端游戏状态枚举
//   - InputState: 输入状态结构
//   - FrameTimer: 帧计时器
//   - ClientConfig: 客户端配置
//   - GameClient: 游戏客户端主类
// =============================================================================

#ifndef LEGEND2_GAME_CLIENT_H
#define LEGEND2_GAME_CLIENT_H

#include "render/i_renderer.h"
#include "game/map/map_renderer.h"
#include "render/animation.h"
#include "client/render/effect_player.h"
#include "ui/ui_renderer.h"
#include "audio/audio_engine.h"
#include "ui/login_screen.h"
#include "scene/login_scene.h"
#include "core/event_dispatcher.h"
#include "core/timer.h"
#include "scene/scene_manager.h"
#include "render/actor_renderer.h"
#include "common/types.h"
#include "common/character_data.h"
#include "client/network/i_network_manager.h"
#include "client/game/map/map_system.h"
#include "client/game/client_character.h"
#include "client/game/entity_manager.h"
#include "client/game/monster/monster_manager.h"
#include "client/core/position_interpolator.h"
#include "client/game/skill/skill_manager.h"
#include "client/game/skill/skill_executor.h"
#include "client/handlers/skill_handler.h"
#include "client/ui/skill/skill_bar.h"
#include "client/ui/skill/skill_book.h"
#include "client/ui/skill/cast_bar.h"
#include "client/ui/skill/skill_target_indicator.h"

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace mir2::game::handlers {
class CharacterHandler;
class LoginHandler;
class MovementHandler;
class CombatHandler;
class EffectHandler;
class SystemHandler;
class NpcHandler;
class SkillHandler;
} // namespace mir2::game::handlers

namespace mir2::client {
class AsyncLoader;
} // namespace mir2::client

namespace mir2::client::skill {
class SkillManager;
class SkillExecutor;
} // namespace mir2::client::skill

namespace mir2::ui {
class NpcDialogUI;
class UIManager;
} // namespace mir2::ui

namespace mir2::ui::skill {
class SkillBar;
class SkillBook;
class CastBar;
class SkillTargetIndicator;
} // namespace mir2::ui::skill

namespace mir2::game {

// 引入公共类型定义
using namespace mir2::common;
namespace client = mir2::client;
namespace ui = mir2::ui;
namespace monster = mir2::game::monster;

// 跨模块类型引用
using mir2::scene::SceneState;
using mir2::scene::SceneStateMachine;
using mir2::scene::StateConfig;
using mir2::scene::LoginFlowState;
using mir2::render::IRenderer;
using mir2::render::Camera;
using mir2::render::Texture;
using mir2::render::AnimationManager;
using mir2::render::ActorRenderer;
using mir2::render::Actor;
using mir2::render::AnimatedEntity;
using mir2::render::CharacterAnimator;
using mir2::render::EffectPlayer;
using mir2::ui::UIRenderer;
using mir2::ui::UIManager;
using mir2::ui::NpcDialogUI;
using mir2::audio::AudioManager;
using mir2::ui::screens::LoginScreen;
using mir2::ui::screens::LoginScreenState;
using mir2::scene::LoginFlowManager;
using mir2::game::map::MapRenderer;
using mir2::game::map::MapSystem;
using mir2::core::EventDispatcher;
using mir2::core::FrameTimer;
using mir2::client::ResourceManager;
using mir2::client::INetworkManager;

class MovementController;

// =============================================================================
// 游戏状态 (Game State)
// =============================================================================

/// 客户端游戏状态枚举（与 SceneState 相同，保持兼容）
using GameState = SceneState;

// =============================================================================
// 输入状态 (Input State)
// =============================================================================

/// 当前输入状态
/// 跟踪鼠标和键盘的输入状态
struct InputState {
    // 鼠标状态
    int mouse_x = 0;                   // 鼠标X坐标
    int mouse_y = 0;                   // 鼠标Y坐标
    bool mouse_left = false;           // 左键是否按下
    bool mouse_right = false;          // 右键是否按下
    bool mouse_middle = false;         // 中键是否按下
    bool mouse_left_clicked = false;   // 左键本帧刚按下
    bool mouse_right_clicked = false;  // 右键本帧刚按下
    
    // 键盘状态
    bool keys[SDL_NUM_SCANCODES] = {false};         // 按键当前状态
    bool keys_pressed[SDL_NUM_SCANCODES] = {false}; // 按键本帧刚按下
    
    // 移动目标
    Position target_position = {-1, -1};  // 移动目标位置
    bool has_move_target = false;         // 是否有移动目标
    
    /// 重置每帧状态
    void reset_frame_state() {
        mouse_left_clicked = false;
        mouse_right_clicked = false;
        std::fill(std::begin(keys_pressed), std::end(keys_pressed), false);
    }
    
    /// 检查按键是否本帧刚按下
    bool key_pressed(SDL_Scancode key) const {
        return keys_pressed[key];
    }
    
    /// 检查按键是否当前按下
    bool key_held(SDL_Scancode key) const {
        return keys[key];
    }
};

// FrameTimer 已移至 core/timer.h

// =============================================================================
// 客户端配置 (Game Client Configuration)
// =============================================================================

/// 客户端配置结构
struct ClientConfig {
    // 窗口设置
    int window_width = 800;                     // 窗口宽度
    int window_height = 600;                    // 窗口高度
    std::string window_title = "Legend2 Client"; // 窗口标题
    bool fullscreen = false;                    // 是否全屏
    bool vsync = true;                          // 是否启用垂直同步
    
    // 网络设置
    std::string server_host = "127.0.0.1";      // 服务器地址
    uint16_t server_port = constants::DEFAULT_SERVER_PORT;  // 服务器端口
    bool auto_connect = false;                  // 是否自动连接
    
    // 图形设置
    int target_fps = constants::TARGET_FPS;     // 目标FPS
    float ui_scale = 1.0f;                      // UI缩放比例
    
    // 音频设置
    float music_volume = 0.7f;                  // 音乐音量
    float sfx_volume = 1.0f;                    // 音效音量
    
    // 调试设置
    bool show_fps = true;                       // 是否显示FPS
    bool show_debug_info = false;               // 是否显示调试信息
};

// =============================================================================
// 游戏客户端 (Game Client)
// =============================================================================

/// 游戏客户端主类
/// 管理游戏主循环、状态转换和各子系统的协作
class GameClient {
public:
    /// 构造函数
    GameClient();

    /// 构造函数（支持依赖注入）
    GameClient(std::unique_ptr<IRenderer> renderer,
               std::unique_ptr<INetworkManager> network_manager);
    
    /// 析构函数
    ~GameClient();
    
    // 禁止拷贝
    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;
    
    /// 初始化客户端
    /// @param config 客户端配置
    /// @return 初始化成功返回true
    bool initialize(const ClientConfig& config = ClientConfig{});
    
    /// 运行游戏主循环
    /// @return 退出码
    int run();
    
    /// 请求关闭
    void shutdown();
    
    /// 检查客户端是否正在运行
    bool is_running() const { return running_; }
    
    /// 获取当前游戏状态
    GameState get_state() const { return state_; }
    
    /// 获取配置
    const ClientConfig& get_config() const { return config_; }
    
    // --- 网络 (Network) ---
    
    /// 连接到服务器
    bool connect_to_server();
    
    /// 断开服务器连接
    void disconnect_from_server();
    
    /// 检查是否已连接到服务器
    bool is_connected() const;
    
    // --- 角色 (Character) ---
    
    /// 获取当前玩家角色(不在游戏中时返回nullptr)
    ClientCharacter* get_player() { return player_.get(); }
    const ClientCharacter* get_player() const { return player_.get(); }
    
    /// 向服务器请求角色列表
    void request_character_list();
    
    /// 请求创建角色
    void request_create_character(const std::string& name, CharacterClass char_class, Gender gender);
    
    /// 选择角色进入游戏
    void select_character(uint64_t character_id);

    // --- NPC交互 (NPC) ---

    /// 发送NPC交互请求
    void InteractWithNpc(uint64_t npc_id);

    /// 发送NPC菜单选择请求（使用当前NPC）
    void SelectNpcMenuOption(uint8_t option_index);

    /// 关闭NPC对话框
    void CloseNpcDialog();

    // --- 场景角色管理 (Scene Actors) ---

    /// 获取玩家角色渲染数据
    Actor& get_player_actor() { return player_actor_; }
    const Actor& get_player_actor() const { return player_actor_; }

    /// 添加场景角色(其他玩家/怪物/NPC)
    Actor* add_scene_actor(int32_t actor_id);

    /// 移除场景角色
    void remove_scene_actor(int32_t actor_id);

    /// 根据ID查找场景角色
    Actor* find_actor(int32_t actor_id);

    /// 设置选中的角色
    void set_focused_actor(Actor* actor) { focused_actor_ = actor; }
    Actor* get_focused_actor() { return focused_actor_; }
    
    // --- 状态转换 (State transitions) ---
    
    /// 转到登录界面
    void go_to_login();
    
    /// 转到角色选择界面
    void go_to_character_select();
    
    /// 转到角色创建界面
    void go_to_character_create();
    
    /// 进入游戏
    void enter_game();

    /// 启动离线游戏模式（创建测试角色并进入游戏）
    void start_offline_play();

    // --- 回调 (Callbacks) ---
    
    using StateChangeCallback = std::function<void(GameState old_state, GameState new_state)>;
    void set_on_state_change(StateChangeCallback callback) { on_state_change_ = callback; }
    
private:
    // Configuration
    ClientConfig config_;

    // State
    bool running_ = false;
    GameState state_ = GameState::INITIALIZING;
    SceneStateMachine state_machine_;
    
    // Core systems
    std::unique_ptr<IRenderer> renderer_;
    std::unique_ptr<ResourceManager> resource_manager_;
    std::unique_ptr<INetworkManager> network_manager_;
    std::unique_ptr<handlers::LoginHandler> login_handler_;
    std::unique_ptr<handlers::CharacterHandler> character_handler_;
    std::unique_ptr<handlers::MovementHandler> movement_handler_;
    std::shared_ptr<handlers::CombatHandler> combat_handler_;
    std::shared_ptr<handlers::EffectHandler> effect_handler_;
    std::unique_ptr<handlers::SystemHandler> system_handler_;
    std::unique_ptr<handlers::NpcHandler> npc_handler_;
    std::shared_ptr<handlers::SkillHandler> skill_handler_;
    
    // Rendering subsystems
    std::unique_ptr<MapRenderer> map_renderer_;
    std::unique_ptr<AnimationManager> animation_manager_;
    std::unique_ptr<EffectPlayer> effect_player_;
    std::unique_ptr<UIRenderer> ui_renderer_;
    std::unique_ptr<AudioManager> audio_manager_;
    std::unique_ptr<LoginScreen> login_screen_;
    std::unique_ptr<ActorRenderer> actor_renderer_;  // 角色渲染
    LoginFlowManager login_flow_;
    std::unique_ptr<UIManager> ui_manager_;
    std::unique_ptr<NpcDialogUI> npc_dialog_ui_;

    // Skill system
    std::unique_ptr<client::skill::SkillManager> skill_manager_;
    std::unique_ptr<client::skill::SkillExecutor> skill_executor_;

    // Skill UI
    std::unique_ptr<ui::skill::SkillBar> skill_bar_;
    std::unique_ptr<ui::skill::SkillBook> skill_book_;
    std::unique_ptr<ui::skill::CastBar> cast_bar_;
    std::unique_ptr<ui::skill::SkillTargetIndicator> skill_target_indicator_;

    // Game systems
    std::unique_ptr<MapSystem> map_system_;
    std::unique_ptr<mir2::client::AsyncLoader> async_loader_;
    std::unique_ptr<MovementController> movement_controller_;
    EntityManager entity_manager_;
    monster::MonsterManager monster_manager_;

    // Player
    std::unique_ptr<ClientCharacter> player_;
    std::unique_ptr<AnimatedEntity> player_entity_;
    std::unique_ptr<CharacterAnimator> character_animator_;
    Actor player_actor_;  // 玩家角色渲染数据
    Position last_player_pos_ = {-1, -1};
    legend2::PositionInterpolator player_interpolator_;

    // Scene actors (其他玩家、怪物、NPC)
    std::vector<Actor> scene_actors_;
    Actor* focused_actor_ = nullptr;  // 当前选中的角色
    
    // Camera
    Camera camera_;
    
    // Input
    InputState input_;

    // Event dispatcher
    EventDispatcher event_dispatcher_;
    
    // Timing
    FrameTimer frame_timer_;
    
    // Account/session
    uint64_t account_id_ = 0;
    std::string session_token_;
    uint64_t last_created_player_id_ = 0;
    bool awaiting_enter_game_ = false;

    // Character selection
    std::vector<CharacterData> character_list_;
    int selected_character_index_ = -1;
    
    // Character creation
    std::string create_char_name_;
    CharacterClass create_char_class_ = CharacterClass::WARRIOR;
    Gender create_char_gender_ = Gender::MALE;
    int create_char_level_ = 0;
    bool has_created_character_ = false;

    // State transition overlay
    bool transition_active_ = false;
    float transition_timer_ = 0.0f;
    float transition_duration_ = 0.25f;
    
    // Callbacks
    StateChangeCallback on_state_change_;

    // Lifetime guard for handler callbacks (expires first during teardown).
    std::shared_ptr<void> handler_callbacks_owner_ = std::make_shared<int>(0);
    
    // --- Internal methods ---
    
    /// Process SDL events
    void process_events();

    /// Update input state from SDL event
    void update_input_state(const SDL_Event& event);
    
    /// Process input for current state
    void process_input();
    
    /// Update game logic
    void update(float delta_time);
    
    /// Render current frame
    void render();
    
    /// Change game state
    void set_state(GameState new_state);

    /// Initialize scene state machine
    void initialize_state_machine();
    
    // --- State-specific methods ---
    
    void update_login(float delta_time);
    void update_character_select(float delta_time);
    void update_character_create(float delta_time);
    void update_playing(float delta_time);
    
    void render_login();
    void render_character_select();
    void render_character_create();
    void render_playing();
    
    void process_input_login();
    void process_input_character_select();
    void process_input_character_create();
    void process_input_playing();
    
    // --- Network handlers ---
    
    void setup_network_handlers();
    void initialize_skill_system();
    void setup_skill_handlers();
    void setup_login_screen_callbacks();
    void load_login_background();
    void load_character_create_assets();

    // --- Transitions ---

    void start_transition();
    void update_transition(float delta_time);
    void render_transition();
    
    // --- Utility ---
    
    /// Convert screen position to world tile position
    Position screen_to_world(int screen_x, int screen_y) const;
    
    /// Send move request to server
    void send_move_request(const Position& target);
    
    /// Send attack request to server
    void send_attack_request(uint32_t target_id);

    /// Send skill request to server
    void send_skill_request(uint32_t skill_id, uint64_t target_id);

    /// Validate skill target existence and range.
    bool validate_skill_target(uint64_t target_id) const;

    void process_skill_hotkeys();
    void update_skill_system(float delta_time);
    void render_skill_ui();

    /// Send NPC menu selection to server
    void send_npc_menu_select(uint64_t npc_id, uint8_t option_index);

    // --- Login flow helpers ---

    /// Send login request after connection is established
    void send_login_request(const std::string& username, const std::string& password);
};

} // namespace mir2::game

#endif // LEGEND2_GAME_CLIENT_H
