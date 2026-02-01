/**
 * @file game_client.cpp
 * @brief Legend2 游戏客户端核心实现
 *
 * 本文件实现了游戏客户端的核心功能，包括：
 * - 帧计时器（FrameTimer）：控制游戏帧率和计算帧间隔时间
 * - 游戏客户端（GameClient）：管理游戏主循环、状态机、渲染和网络通信
 *
 * 游戏状态流程：
 * LOGIN -> CHARACTER_SELECT -> CHARACTER_CREATE -> LOADING -> PLAYING -> EXITING
 */

#include "game/game_client.h"
#include "client/handlers/login_handler.h"
#include "client/handlers/character_handler.h"
#include "client/handlers/combat_handler.h"
#include "client/handlers/effect_handler.h"
#include "client/handlers/handler_registry.h"
#include "client/handlers/movement_handler.h"
#include "client/handlers/npc_handler.h"
#include "client/handlers/system_handler.h"
#include "client/network/network_manager.h"
#include "client/resource/async_loader.h"
#include "client/game/movement_controller.h"
#include "client/game/monster/monster_data.h"
#include "client/game/monster/monster_template_mapper.h"
#include "render/renderer.h"
#include "ui/login_screen.h"
#include "ui/npc_dialog_ui.h"
#include "ui/ui_manager.h"
#include "common/enums.h"
#include "common/protocol/message_codec.h"
#include "common/protocol/npc_message_codec.h"

#include <flatbuffers/flatbuffers.h>
#include "login_generated.h"
#include "game_generated.h"
#include "combat_generated.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <fstream>
#include <vector>

namespace mir2::game {

using mir2::client::Sprite;

namespace {
uint8_t direction_from_delta(int dx, int dy) {
    int step_x = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    int step_y = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

    if (step_x == 0 && step_y < 0) return static_cast<uint8_t>(Direction::UP);
    if (step_x > 0 && step_y < 0) return static_cast<uint8_t>(Direction::UP_RIGHT);
    if (step_x > 0 && step_y == 0) return static_cast<uint8_t>(Direction::RIGHT);
    if (step_x > 0 && step_y > 0) return static_cast<uint8_t>(Direction::DOWN_RIGHT);
    if (step_x == 0 && step_y > 0) return static_cast<uint8_t>(Direction::DOWN);
    if (step_x < 0 && step_y > 0) return static_cast<uint8_t>(Direction::DOWN_LEFT);
    if (step_x < 0 && step_y == 0) return static_cast<uint8_t>(Direction::LEFT);
    if (step_x < 0 && step_y < 0) return static_cast<uint8_t>(Direction::UP_LEFT);

    return static_cast<uint8_t>(Direction::DOWN);
}

mir2::proto::Profession to_proto_profession(CharacterClass char_class) {
    switch (char_class) {
        case CharacterClass::WARRIOR:
            return mir2::proto::Profession::WARRIOR;
        case CharacterClass::MAGE:
            return mir2::proto::Profession::WIZARD;
        case CharacterClass::TAOIST:
            return mir2::proto::Profession::TAOIST;
        default:
            return mir2::proto::Profession::NONE;
    }
}

mir2::proto::Gender to_proto_gender(Gender gender) {
    return gender == Gender::FEMALE ? mir2::proto::Gender::FEMALE : mir2::proto::Gender::MALE;
}

EntityType to_local_entity_type(mir2::proto::EntityType type) {
    switch (type) {
        case mir2::proto::EntityType::PLAYER:
            return EntityType::Player;
        case mir2::proto::EntityType::MONSTER:
            return EntityType::Monster;
        case mir2::proto::EntityType::NPC:
            return EntityType::NPC;
        case mir2::proto::EntityType::ITEM:
            return EntityType::GroundItem;
        default:
            return EntityType::Player;
    }
}

std::vector<uint8_t> build_payload(flatbuffers::FlatBufferBuilder& builder) {
    const uint8_t* data = builder.GetBufferPointer();
    return std::vector<uint8_t>(data, data + builder.GetSize());
}

int64_t now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               clock::now().time_since_epoch())
        .count();
}

class NpcDialogWidget final : public mir2::ui::UIWidget {
public:
    explicit NpcDialogWidget(mir2::ui::NpcDialogUI* dialog)
        : dialog_(dialog) {}

    void render(mir2::ui::UIRenderer& renderer) override {
        (void)renderer;
        if (dialog_) {
            dialog_->render();
        }
    }

    bool handle_event(const SDL_Event& event) override {
        if (!dialog_) {
            return false;
        }

        switch (event.type) {
            case SDL_MOUSEMOTION:
                return dialog_->on_mouse_move(event.motion.x, event.motion.y);
            case SDL_MOUSEBUTTONDOWN:
                return dialog_->on_mouse_button_down(event.button.button, event.button.x, event.button.y);
            case SDL_MOUSEBUTTONUP:
                return dialog_->on_mouse_button_up(event.button.button, event.button.x, event.button.y);
            case SDL_MOUSEWHEEL:
                return dialog_->on_mouse_wheel(event.wheel.x, event.wheel.y);
            case SDL_KEYDOWN:
                return dialog_->on_key_down(event.key.keysym.scancode,
                                           event.key.keysym.sym,
                                           event.key.repeat != 0);
            case SDL_USEREVENT:
                return dialog_->on_user_event(event.user);
            default:
                return false;
        }
    }

private:
    mir2::ui::NpcDialogUI* dialog_ = nullptr;
};
} // namespace

// FrameTimer 已移至 core/timer.h

// =============================================================================
// GameClient 游戏客户端实现
// 管理游戏的整个生命周期，包括初始化、主循环、状态管理和资源清理
// =============================================================================

/**
 * @brief 构造函数 - 初始化帧计时器
 */
GameClient::GameClient()
    : GameClient(nullptr, nullptr) {
}

GameClient::GameClient(std::unique_ptr<IRenderer> renderer,
                       std::unique_ptr<INetworkManager> network_manager)
    : renderer_(std::move(renderer))
    , network_manager_(std::move(network_manager))
    , frame_timer_(constants::TARGET_FPS)
    , monster_manager_(entity_manager_) {
}

/**
 * @brief 析构函数 - 确保资源正确释放
 */
GameClient::~GameClient() {
    shutdown();
}

/**
 * @brief 初始化游戏客户端
 * @param config 客户端配置参数
 * @return 初始化成功返回true
 * 
 * 初始化顺序：
 * 1. SDL库初始化（视频、音频、计时器）
 * 2. 渲染器初始化
 * 3. 资源管理器创建
 * 4. 渲染子系统创建（地图渲染、动画、UI）
 * 5. 音频管理器初始化
 * 6. 登录界面创建
 * 7. 网络客户端创建
 * 8. 游戏系统创建（地图系统等）
 * 9. 加载资源档案
 * 10. 设置摄像机和帧率
 */
bool GameClient::initialize(const ClientConfig& config) {
    config_ = config;
    
    std::cout << "Legend2 Client v0.1.0" << std::endl;
    std::cout << "Initializing..." << std::endl;
    
    // 初始化SDL库（视频、音频、计时器子系统）
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }
    std::cout << "  SDL initialized" << std::endl;
    
    // 创建并初始化渲染器
    if (!renderer_) {
        renderer_ = std::make_unique<mir2::render::SDLRenderer>();
    }

    auto* sdl_renderer = dynamic_cast<mir2::render::SDLRenderer*>(renderer_.get());
    if (!sdl_renderer) {
        std::cerr << "Renderer does not support SDLRenderer features" << std::endl;
        return false;
    }

    if (!renderer_->initialize(config_.window_width, config_.window_height, config_.window_title)) {
        std::cerr << "Failed to initialize renderer" << std::endl;
        return false;
    }
    std::cout << "  Renderer initialized" << std::endl;
    
    // 创建资源管理器（管理WIL档案、精灵、纹理等）
    resource_manager_ = std::make_unique<ResourceManager>();
    async_loader_ = std::make_unique<mir2::client::AsyncLoader>(*resource_manager_);
    std::cout << "  Resource manager created" << std::endl;

    // 获取纹理缓存接口
    mir2::render::ITextureCache& texture_cache = sdl_renderer->get_texture_cache();

    // 创建渲染子系统
    map_renderer_ = std::make_unique<MapRenderer>(*sdl_renderer, *resource_manager_,
                                                  texture_cache, async_loader_.get());      // 地图渲染
    animation_manager_ = std::make_unique<AnimationManager>(*sdl_renderer, *resource_manager_);  // 动画管理
    effect_player_ = std::make_unique<mir2::render::EffectPlayer>(*sdl_renderer, *resource_manager_);
    ui_renderer_ = std::make_unique<UIRenderer>(*sdl_renderer);                            // UI渲染
    ui_renderer_->set_ui_scale(config_.ui_scale);
    actor_renderer_ = std::make_unique<ActorRenderer>(*sdl_renderer, *resource_manager_, texture_cache);  // 角色渲染
    std::cout << "  Rendering subsystems created" << std::endl;
    
    // 创建并初始化音频管理
    audio_manager_ = std::make_unique<AudioManager>();
    if (audio_manager_->initialize()) {
        audio_manager_->set_music_volume(config_.music_volume);
        audio_manager_->set_sfx_volume(config_.sfx_volume);
        std::cout << "  Audio manager initialized" << std::endl;
    } else {
        std::cout << "  Audio manager failed to initialize (continuing without audio)" << std::endl;
    }
    
    // 创建登录界面并设置回调
    login_screen_ = std::make_unique<LoginScreen>(*sdl_renderer, *ui_renderer_, *resource_manager_);
    login_screen_->set_state_machine(&state_machine_);
    event_dispatcher_.add_listener(login_screen_.get(), 10);
    login_screen_->set_dimensions(config_.window_width, config_.window_height);
    setup_login_screen_callbacks();  // 设置登录、角色选择等回调函数
    load_login_background();         // 加载登录界面背景
    load_character_create_assets();  // 加载角色创建界面资源
    std::cout << "  Login screen created" << std::endl;

    // 创建NPC对话界面并接入事件系统
    npc_dialog_ui_ = std::make_unique<NpcDialogUI>(*sdl_renderer, *ui_renderer_, &event_dispatcher_);
    npc_dialog_ui_->set_dimensions(config_.window_width, config_.window_height);
    npc_dialog_ui_->set_menu_select_callback([this](uint64_t npc_id, uint8_t option_index) {
        send_npc_menu_select(npc_id, option_index);
    });
    event_dispatcher_.add_listener(npc_dialog_ui_.get(), 20);

    // 创建UI管理器并注册游戏内UI层
    ui_manager_ = std::make_unique<UIManager>();
    auto gameplay_screen = std::make_shared<mir2::ui::UIContainer>();
    gameplay_screen->set_id("gameplay");
    gameplay_screen->set_bounds({0, 0, config_.window_width, config_.window_height});
    gameplay_screen->add_child(std::make_shared<NpcDialogWidget>(npc_dialog_ui_.get()));
    ui_manager_->register_screen("gameplay", gameplay_screen);
    ui_manager_->set_active_screen("gameplay");

    initialize_skill_system();
    monster::MonsterTemplateMapper::instance().load_from_file(
        (std::filesystem::path("config") / "tables" / "monsters.yaml").string());
    
    // 创建网络管理器并设置消息处理
    if (!network_manager_) {
        network_manager_ = std::make_unique<mir2::client::NetworkManager>();
    }
    setup_network_handlers();
    std::cout << "  Network manager created" << std::endl;
    
    // 创建游戏系统
    map_system_ = std::make_unique<MapSystem>(*resource_manager_, "Map");
    if (network_manager_) {
        movement_controller_ = std::make_unique<MovementController>(*map_system_, *network_manager_,
                                                                    player_interpolator_);
    }
    std::cout << "  Game systems created" << std::endl;
    
    // 加载地图渲染所需的资源档案
    map_renderer_->load_archives();
    
    // 设置摄像机视口大小
    camera_.viewport_width = config_.window_width;
    camera_.viewport_height = config_.window_height;
    
    // 设置目标帧率
    frame_timer_.set_target_fps(config_.target_fps);
    
    // 如果配置了自动连接，则连接服务器
    if (config_.auto_connect) {
        connect_to_server();
    }
    
    std::cout << "Initialization complete" << std::endl;
    initialize_state_machine();
    set_state(GameState::LOGIN);  // 进入登录状态
    
    return true;
}

/**
 * @brief 运行游戏主循环
 * @return 退出码（0 表示正常退出）
 * 
 * 主循环流程（每帧执行）：
 * 1. begin_frame() - 标记帧开始
 * 2. process_events() - 处理SDL事件（键盘、鼠标、窗口等）
 * 3. process_input() - 处理游戏输入逻辑
 * 4. network_manager_->update() - 处理网络消息
 * 5. update() - 更新游戏逻辑
 * 6. render() - 渲染画面
 * 7. end_frame() - 帧率限制和FPS统计
 * 8. reset_frame_state() - 重置单帧输入状态
 */
int GameClient::run() {
    if (!renderer_ || !renderer_->is_initialized()) {
        std::cerr << "Client not initialized" << std::endl;
        return 1;
    }
    
    running_ = true;
    std::cout << "Entering main loop..." << std::endl;
    
    // 游戏主循环
    while (running_ && state_ != GameState::EXITING) {
        frame_timer_.begin_frame();
        
        // 处理SDL事件（窗口、输入等）
        process_events();
        
        // 处理游戏输入
        process_input();
        
        // 更新网络（接收和处理服务器消息）
        if (network_manager_) {
            network_manager_->update();
        }
        
        // 更新游戏逻辑
        float delta_time = frame_timer_.get_delta_time();
        update(delta_time);
        
        // 渲染画面
        render();
        
        // 结束帧（执行帧率限制）
        frame_timer_.end_frame();
        
        // 重置单帧输入状态（如按键按下事件）
        input_.reset_frame_state();
    }
    
    std::cout << "Exiting main loop" << std::endl;
    return 0;
}

/**
 * @brief 关闭游戏客户端，释放所有资源
 * 
 * 资源释放顺序（与创建顺序相反）：
 * 1. 断开服务器连接
 * 2. 释放玩家相关对象
 * 3. 释放游戏系统
 * 4. 释放音频、UI、渲染子系统
 * 5. 释放网络客户端和资源管理器
 * 6. 关闭渲染器
 * 7. 退出SDL
 */
void GameClient::shutdown() {
    // 防止重复关闭
    if (!running_ && state_ == GameState::EXITING) {
        return;
    }
    
    std::cout << "Shutting down..." << std::endl;
    running_ = false;
    set_state(GameState::EXITING);
    
    // 断开服务器连接
    disconnect_from_server();
    
    // 按创建的逆序释放资源
    player_entity_.reset();
    player_.reset();
    character_animator_.reset();

    movement_controller_.reset();
    async_loader_.reset();
    map_system_.reset();

    skill_target_indicator_.reset();
    cast_bar_.reset();
    skill_book_.reset();
    skill_bar_.reset();
    skill_executor_.reset();
    skill_manager_.reset();
    skill_handler_.reset();
    
    effect_player_.reset();
    audio_manager_.reset();
    event_dispatcher_.clear();
    npc_dialog_ui_.reset();
    ui_manager_.reset();
    login_screen_.reset();
    ui_renderer_.reset();
    animation_manager_.reset();
    map_renderer_.reset();
    
    network_manager_.reset();
    resource_manager_.reset();
    
    // 关闭渲染
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
    
    // 退出SDL
    SDL_Quit();
    std::cout << "Shutdown complete" << std::endl;
}

/**
 * @brief 处理SDL事件
 * 
 * 处理的事件类型：
 * - SDL_QUIT: 窗口关闭请求
 * - SDL_KEYDOWN/SDL_KEYUP: 键盘按下/释放
 * - SDL_TEXTINPUT: 文本输入（用于输入框）
 * - SDL_MOUSEMOTION: 鼠标移动
 * - SDL_MOUSEBUTTONDOWN/UP: 鼠标按键
 * - SDL_WINDOWEVENT: 窗口事件（如调整大小）
 */
void GameClient::process_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            // 窗口关闭请求
            running_ = false;
            continue;
        }

        if (event.type == SDL_WINDOWEVENT) {
            // 窗口事件
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                // 窗口大小改变，更新摄像机视口
                camera_.viewport_width = event.window.data1;
                camera_.viewport_height = event.window.data2;
                config_.window_width = event.window.data1;
                config_.window_height = event.window.data2;
                // 更新登录界面尺寸
                if (login_screen_) {
                    login_screen_->set_dimensions(event.window.data1, event.window.data2);
                }
                if (npc_dialog_ui_) {
                    npc_dialog_ui_->set_dimensions(event.window.data1, event.window.data2);
                }
                if (ui_manager_) {
                    if (auto* screen = ui_manager_->get_screen("gameplay")) {
                        screen->set_bounds({0, 0, event.window.data1, event.window.data2});
                    }
                }
            }
            continue;
        }

        event_dispatcher_.dispatch(event);

        // 全局快捷键处理
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                if (state_ == GameState::PLAYING) {
                    // TODO: 显示游戏菜单
                } else if (state_ == GameState::LOGIN) {
                    running_ = false;  // 在登录界面按ESC退出游戏
                }
            }
        }

        update_input_state(event);
    }
}

void GameClient::update_input_state(const SDL_Event& event) {
    switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                // 记录按键按下状态（仅首次按下时触发 pressed）
                if (!input_.keys[event.key.keysym.scancode]) {
                    input_.keys_pressed[event.key.keysym.scancode] = true;
                }
                input_.keys[event.key.keysym.scancode] = true;
            }
            break;
        case SDL_KEYUP:
            if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                input_.keys[event.key.keysym.scancode] = false;
            }
            break;
        case SDL_MOUSEMOTION:
            input_.mouse_x = event.motion.x;
            input_.mouse_y = event.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                input_.mouse_left = true;
                input_.mouse_left_clicked = true;  // 单帧点击标记
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                input_.mouse_right = true;
                input_.mouse_right_clicked = true;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                input_.mouse_middle = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                input_.mouse_left = false;
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                input_.mouse_right = false;
            } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                input_.mouse_middle = false;
            }
            break;
        default:
            break;
    }
}

/**
 * @brief 处理游戏输入
 * 根据当前游戏状态分发到对应的输入处理函数
 */
void GameClient::process_input() {
    state_machine_.process_input();
}

/**
 * @brief 更新游戏逻辑
 * @param delta_time 帧间隔时间（秒）
 * 根据当前游戏状态分发到对应的更新函数
 */
void GameClient::update(float delta_time) {
    state_machine_.update(delta_time);

    update_transition(delta_time);
}

/**
 * @brief 渲染游戏画面
 * 根据当前游戏状态渲染对应的界面
 */
void GameClient::render() {
    renderer_->begin_frame();
    renderer_->clear({30, 30, 50, 255});  // 深蓝灰色背景
    
    // 根据状态渲染对应界面
    state_machine_.render();

    render_transition();
    
    // 如果启用了FPS显示，在左上角绘制FPS
    if (config_.show_fps && ui_renderer_) {
        ui_renderer_->draw_text("FPS: " + std::to_string(static_cast<int>(frame_timer_.get_fps())),
                               10, 10, Color::white());
    }
    
    renderer_->end_frame();
}

/**
 * @brief 初始化场景状态机
 */
void GameClient::initialize_state_machine() {
    StateConfig initializing_config;
    initializing_config.name = "INITIALIZING";
    state_machine_.register_state(SceneState::INITIALIZING, initializing_config);

    StateConfig login_config;
    login_config.on_enter = [this]() {
        if (login_screen_) {
            login_screen_->set_state(LoginScreenState::LOGIN);
        }
    };
    login_config.update = [this](float dt) { update_login(dt); };
    login_config.render = [this]() { render_login(); };
    login_config.process_input = [this]() { process_input_login(); };
    login_config.name = "LOGIN";
    state_machine_.register_state(SceneState::LOGIN, login_config);

    StateConfig character_select_config;
    character_select_config.on_enter = [this]() {
        if (login_screen_) {
            login_screen_->set_state(LoginScreenState::CHARACTER_SELECT);
            login_screen_->set_character_list(character_list_);
        }
    };
    character_select_config.update = [this](float dt) { update_character_select(dt); };
    character_select_config.render = [this]() { render_character_select(); };
    character_select_config.process_input = [this]() { process_input_character_select(); };
    character_select_config.name = "CHARACTER_SELECT";
    state_machine_.register_state(SceneState::CHARACTER_SELECT, character_select_config);

    StateConfig character_create_config;
    character_create_config.on_enter = [this]() {
        if (login_screen_) {
            login_screen_->set_state(LoginScreenState::CHARACTER_CREATE);
        }
    };
    character_create_config.update = [this](float dt) { update_character_create(dt); };
    character_create_config.render = [this]() { render_character_create(); };
    character_create_config.process_input = [this]() { process_input_character_create(); };
    character_create_config.name = "CHARACTER_CREATE";
    state_machine_.register_state(SceneState::CHARACTER_CREATE, character_create_config);

    StateConfig loading_config;
    loading_config.on_enter = [this]() {
        if (login_screen_) {
            login_screen_->set_state(LoginScreenState::CONNECTING);
        }
    };
    loading_config.name = "LOADING";
    state_machine_.register_state(SceneState::LOADING, loading_config);

    StateConfig playing_config;
    playing_config.update = [this](float dt) { update_playing(dt); };
    playing_config.render = [this]() { render_playing(); };
    playing_config.process_input = [this]() { process_input_playing(); };
    playing_config.name = "PLAYING";
    state_machine_.register_state(SceneState::PLAYING, playing_config);

    StateConfig disconnected_config;
    disconnected_config.name = "DISCONNECTED";
    state_machine_.register_state(SceneState::DISCONNECTED, disconnected_config);

    StateConfig exiting_config;
    exiting_config.name = "EXITING";
    state_machine_.register_state(SceneState::EXITING, exiting_config);
}

/**
 * @brief 设置游戏状态
 * @param new_state 新的游戏状态
 * 
 * 状态切换时会：
 * 1. 触发状态机 on_exit/on_enter
 * 2. 触发状态变化回调
 */
void GameClient::set_state(GameState new_state) {
    if (state_machine_.get_state() == new_state) return;
    
    GameState old_state = state_machine_.get_state();
    state_machine_.set_state(new_state);
    state_ = state_machine_.get_state();
    
    std::cout << "State change: " << static_cast<int>(old_state) 
              << " -> " << static_cast<int>(state_) << std::endl;

    start_transition();
    
    // 触发状态变化回调
    if (on_state_change_) {
        on_state_change_(old_state, state_);
    }
}

// =============================================================================
// 网络相关方法
// =============================================================================

/**
 * @brief 连接到游戏服务器
 * @return 连接成功返回true
 */
bool GameClient::connect_to_server() {
    if (!network_manager_) return false;
    
    std::cout << "Connecting to " << config_.server_host << ":" << config_.server_port << std::endl;
    return network_manager_->connect(config_.server_host, config_.server_port);
}

/**
 * @brief 断开与服务器的连接
 */
void GameClient::disconnect_from_server() {
    if (network_manager_) {
        network_manager_->disconnect();
    }
}

/**
 * @brief 检查是否已连接到服务器
 * @return 已连接返回true
 */
bool GameClient::is_connected() const {
    return network_manager_ && network_manager_->is_connected();
}

void GameClient::initialize_skill_system() {
    skill_manager_ = std::make_unique<client::skill::SkillManager>();
    skill_executor_ = std::make_unique<client::skill::SkillExecutor>(*skill_manager_);
    skill_executor_->set_send_callback([this](uint32_t skill_id, uint64_t target_id) {
        send_skill_request(skill_id, target_id);
    });

    skill_bar_ = std::make_unique<ui::skill::SkillBar>(*skill_manager_);
    skill_book_ = std::make_unique<ui::skill::SkillBook>(*skill_manager_);
    cast_bar_ = std::make_unique<ui::skill::CastBar>();
    skill_target_indicator_ = std::make_unique<ui::skill::SkillTargetIndicator>();

    const std::filesystem::path skills_path = std::filesystem::path("config") / "skills.yaml";
    if (std::filesystem::exists(skills_path)) {
        if (!skill_manager_->load_templates_from_yaml(skills_path.string())) {
            std::cerr << "Failed to load skill templates from " << skills_path.string() << std::endl;
        }
    }
}

/**
 * @brief 设置网络消息处理
 * 
 * 注册以下回调
 * - handlers: 收到服务器消息时调用
 * - on_connect: 连接成功时调用
 * - on_disconnect: 断开连接时调用
 */
void GameClient::setup_network_handlers() {
    if (!network_manager_) {
        return;
    }

    handlers::LoginHandler::Callbacks login_callbacks;
    login_callbacks.on_login_success = [this](uint64_t account_id, const std::string& token) {
        account_id_ = account_id;
        session_token_ = token;
        login_flow_.on_auth_success();
    };
    login_callbacks.on_login_failure = [this](const std::string& error) {
        std::cerr << "Login failed: " << error << std::endl;
        login_flow_.on_auth_failed(error);
    };
    login_callbacks.on_login_timeout = []() {};
    login_callbacks.request_character_list = [this]() { request_character_list(); };

    login_handler_ = std::make_unique<handlers::LoginHandler>(std::move(login_callbacks));

    handlers::CharacterHandler::Callbacks character_callbacks;
    character_callbacks.on_character_list = [this](std::vector<CharacterData> characters) {
        character_list_ = std::move(characters);
        login_flow_.on_characters_loaded();
        set_state(GameState::CHARACTER_SELECT);
    };
    character_callbacks.on_character_list_failed = [](const std::string& error) {
        std::cerr << "Role list failed: " << error << std::endl;
    };
    character_callbacks.on_character_created = [this](uint64_t player_id) {
        last_created_player_id_ = player_id;
    };
    character_callbacks.on_character_create_failed = [](const std::string& error) {
        std::cerr << "Character creation failed: " << error << std::endl;
    };
    character_callbacks.on_select_role_success = [this]() {
        awaiting_enter_game_ = true;
    };
    character_callbacks.on_select_role_failed = [](const std::string& error) {
        std::cerr << "Select character failed: " << error << std::endl;
    };
    character_callbacks.on_enter_game_success = [this](const CharacterData& data) {
        player_ = std::make_unique<ClientCharacter>(data);
        camera_.center_on(player_->get_position());

        player_entity_ = std::make_unique<AnimatedEntity>();
        character_animator_ = std::make_unique<CharacterAnimator>(*animation_manager_);
        character_animator_->setup_character(*player_entity_, player_->get_class(), player_->get_gender());
        player_entity_->set_position(player_->get_position());

        awaiting_enter_game_ = false;
        enter_game();
    };
    character_callbacks.on_enter_game_failed = [this](const std::string& error) {
        std::cerr << "Enter game failed: " << error << std::endl;
        awaiting_enter_game_ = false;
    };
    character_callbacks.request_character_list = [this]() { request_character_list(); };
    character_callbacks.get_account_id = [this]() { return account_id_; };
    character_callbacks.get_character_list = [this]() -> const std::vector<CharacterData>& {
        return character_list_;
    };

    character_handler_ = std::make_unique<handlers::CharacterHandler>(std::move(character_callbacks));

    handlers::MovementHandler::Callbacks movement_callbacks;
    movement_callbacks.on_move_response = [this](int x, int y) {
        if (!player_) {
            return;
        }
        const Position pos{x, y};
        player_->set_position(x, y);
        entity_manager_.update_entity_position(player_->get_id(), pos, player_actor_.data.direction);
        if (movement_controller_) {
            movement_controller_->on_move_response(pos);
        }
        if (player_entity_) {
            player_entity_->set_position(pos);
        }
    };
    movement_callbacks.on_move_failed = [this](const std::string& error) {
        std::cerr << "Move failed: " << error << std::endl;
        if (movement_controller_) {
            movement_controller_->on_move_failed();
        }
    };
    movement_callbacks.on_parse_error = [](const std::string& error) {
        std::cerr << "Movement handler parse error: " << error << std::endl;
    };
    movement_callbacks.on_entity_move = [this](const events::EntityMovedEvent& event) {
        entity_manager_.update_entity_position(event.entity_id, {event.x, event.y}, event.direction);
        if (player_ && player_->get_id() == static_cast<uint32_t>(event.entity_id)) {
            player_->set_position(event.x, event.y);
            player_actor_.data.direction = event.direction;
            if (movement_controller_) {
                movement_controller_->on_move_response({event.x, event.y});
            }
            return;
        }

        Actor* actor = find_actor(static_cast<int32_t>(event.entity_id));
        if (!actor) {
            return;
        }

        if (event.entity_type == mir2::proto::EntityType::NPC) {
            actor->data.race = ActorRace::NPC;
        }
        actor->data.map_x = static_cast<uint16_t>(event.x);
        actor->data.map_y = static_cast<uint16_t>(event.y);
        actor->data.direction = event.direction;
    };
    movement_callbacks.on_entity_enter = [this](const events::EntityEnteredEvent& event) {
        Entity entity;
        entity.id = event.entity_id;
        entity.type = to_local_entity_type(event.entity_type);
        entity.position = {event.x, event.y};
        entity.direction = event.direction;
        if (!entity_manager_.add_entity(entity)) {
            entity_manager_.update_entity(entity);
        }

        if (event.entity_type == mir2::proto::EntityType::ITEM) {
            return;
        }

        Actor* actor = add_scene_actor(static_cast<int32_t>(event.entity_id));
        if (!actor) {
            return;
        }

        actor->data.map_x = static_cast<uint16_t>(event.x);
        actor->data.map_y = static_cast<uint16_t>(event.y);
        actor->data.direction = event.direction;
        actor->data.race = (event.entity_type == mir2::proto::EntityType::NPC) ? ActorRace::NPC : ActorRace::HUMAN;
        actor->data.job = 0;

        if (actor_renderer_) {
            actor_renderer_->load_actor_surfaces(*actor);
            actor_renderer_->set_action(*actor, ActorAction::STAND);
        }
    };
    movement_callbacks.on_entity_leave = [this](const events::EntityLeftEvent& event) {
        entity_manager_.remove_entity(event.entity_id);
        remove_scene_actor(static_cast<int32_t>(event.entity_id));
    };
    movement_callbacks.on_entity_update = [this](const events::EntityStatsUpdatedEvent& event) {
        if (player_ && player_->get_id() == static_cast<uint32_t>(event.entity_id)) {
            auto& stats = player_->get_data_mut().stats;
            stats.hp = event.hp;
            stats.max_hp = event.max_hp;
            stats.mp = event.mp;
            stats.max_mp = event.max_mp;
            stats.level = event.level;
        }
        entity_manager_.update_entity_stats(event.entity_id,
                                            event.hp,
                                            event.max_hp,
                                            event.mp,
                                            event.max_mp,
                                            event.level);
    };
    movement_callbacks.on_monster_enter = [this](uint64_t id, uint32_t template_id,
                                                 int x, int y, uint8_t dir,
                                                 int hp, int max_hp) {
        monster::ClientMonster monster;
        monster.id = id;
        monster.template_id = template_id;
        monster.position = {x, y};
        monster.direction = dir;
        monster.hp = hp;
        monster.max_hp = max_hp;

        const auto render_info = monster::MonsterTemplateMapper::instance().get_render_info(template_id);
        monster.render_config.race = render_info.race;
        monster.render_config.appearance = render_info.appearance;

        monster_manager_.add_monster(monster);
    };
    movement_callbacks.on_monster_leave = [this](uint64_t id) {
        monster_manager_.remove_monster(id);
    };
    movement_callbacks.on_monster_move = [this](uint64_t id, int x, int y, uint8_t dir) {
        monster_manager_.update_position(id, {x, y}, dir);
    };
    movement_callbacks.on_monster_stats = [this](uint64_t id, int hp, int max_hp) {
        monster_manager_.update_stats(id, hp, max_hp, 0);
    };
    movement_callbacks.on_monster_death = [this](uint64_t id, uint64_t killer_id) {
        monster_manager_.handle_death(id, killer_id);
    };
    movement_handler_ = std::make_unique<handlers::MovementHandler>(std::move(movement_callbacks));
    movement_handler_->BindHandlers(*network_manager_);

    handlers::CombatHandler::Callbacks combat_callbacks;
    combat_callbacks.owner = std::weak_ptr<void>(handler_callbacks_owner_);
    combat_callbacks.on_attack_response = [](mir2::proto::ErrorCode, uint64_t, uint64_t, int, int, bool) {};
    combat_callbacks.on_pickup_item_response = [](mir2::proto::ErrorCode, uint32_t) {};
    combat_callbacks.on_parse_error = [](const std::string& error) {
        std::cerr << "Combat handler parse error: " << error << std::endl;
    };

    combat_handler_ = std::make_shared<handlers::CombatHandler>(std::move(combat_callbacks));

    handlers::EffectHandler::Callbacks effect_callbacks;
    effect_callbacks.owner = std::weak_ptr<void>(handler_callbacks_owner_);
    effect_callbacks.on_skill_effect = [this](const handlers::SkillEffectParams& params) {
        if (effect_player_ && !params.effect_id.empty()) {
            effect_player_->play_skill_effect(
                params.caster_id, params.target_id, params.skill_id,
                params.effect_type,
                params.effect_id, params.x, params.y, params.duration_ms);
        }
        if (audio_manager_ && !params.sound_id.empty()) {
            audio_manager_->play_sound(params.sound_id);
        }
    };
    effect_callbacks.on_play_effect = [this](const std::string& effect_id,
                                             int x, int y, uint8_t direction,
                                             uint32_t duration_ms) {
        if (effect_player_ && !effect_id.empty()) {
            effect_player_->play_effect(effect_id, x, y, direction, duration_ms);
        }
    };
    effect_callbacks.on_play_sound = [this](const std::string& sound_id, int, int) {
        if (audio_manager_ && !sound_id.empty()) {
            audio_manager_->play_sound(sound_id);
        }
    };
    effect_callbacks.on_parse_error = [](const std::string& error) {
        std::cerr << "Effect handler parse error: " << error << std::endl;
    };

    effect_handler_ = std::make_shared<handlers::EffectHandler>(std::move(effect_callbacks));
    combat_handler_->BindHandlers(*network_manager_);
    effect_handler_->BindHandlers(*network_manager_);
    setup_skill_handlers();

    handlers::SystemHandler::Callbacks system_callbacks;
    system_callbacks.on_heartbeat_response = [](uint32_t, uint32_t) {};
    system_callbacks.on_server_notice = [this](uint16_t, const std::string& message, uint32_t) {
        std::cout << "Server notice: " << message << std::endl;
        if (login_screen_) {
            login_screen_->set_status(message.c_str());
        }
    };
    system_callbacks.on_kick = [this](mir2::proto::ErrorCode, const std::string& reason_text,
                                     const std::string& message) {
        std::cerr << "Kicked: " << reason_text << " - " << message << std::endl;
        if (login_screen_) {
            login_screen_->set_error(message);
        }
        disconnect_from_server();
        if (state_ == GameState::PLAYING) {
            set_state(GameState::DISCONNECTED);
        }
    };

    system_handler_ = std::make_unique<handlers::SystemHandler>(std::move(system_callbacks));

    handlers::NpcHandler::Callbacks npc_callbacks;
    npc_callbacks.event_dispatcher = &event_dispatcher_;
    npc_handler_ = std::make_unique<handlers::NpcHandler>(std::move(npc_callbacks));

    handlers::HandlerRegistry::RegisterHandlers(*network_manager_);

    network_manager_->set_default_handler([this](const NetworkPacket& packet) {
        std::cerr << "Unhandled message id: " << packet.msg_id << std::endl;
    });
    
    // 连接成功回调
    network_manager_->set_on_connect([this]() {
        std::cout << "Connected to server" << std::endl;
        login_flow_.on_connect_success();
    });
    
    // 断开连接回调
    network_manager_->set_on_disconnect([this]() {
        std::cout << "Disconnected from server" << std::endl;
        // 如果在游戏中断开，切换到断开状态
        if (state_ == GameState::PLAYING) {
            set_state(GameState::DISCONNECTED);
        }
    });
}

void GameClient::setup_skill_handlers() {
    if (!network_manager_ || !skill_manager_) {
        return;
    }

    handlers::SkillHandler::Callbacks skill_callbacks;
    skill_callbacks.owner = std::weak_ptr<void>(handler_callbacks_owner_);
    skill_callbacks.on_skill_result = [this](mir2::proto::ErrorCode code,
                                             uint32_t skill_id,
                                             uint64_t /*caster_id*/,
                                             uint64_t target_id,
                                             mir2::common::SkillResult result,
                                             int damage,
                                             int healing,
                                             bool /*target_dead*/) {
        if (code != mir2::proto::ErrorCode::ERR_OK) {
            std::cerr << "Skill failed: " << static_cast<uint16_t>(code) << std::endl;
            return;
        }

        const Entity* target = entity_manager_.get_entity(target_id);
        if (!target) {
            return;
        }

        if (result == mir2::common::SkillResult::HIT) {
            if (ui_renderer_) {
                if (damage != 0) {
                    ui_renderer_->add_damage_number(damage, target->position, false);
                }
                if (healing != 0) {
                    ui_renderer_->add_damage_number(-healing, target->position, false);
                }
            }

            if (skill_manager_ && effect_player_) {
                const auto* tmpl = skill_manager_->get_template(skill_id);
                if (tmpl && tmpl->effect_id != 0) {
                    effect_player_->play_effect(std::to_string(tmpl->effect_id),
                                                target->position.x,
                                                target->position.y);
                }
            }
            return;
        }

        if (ui_renderer_) {
            ui_renderer_->add_miss_indicator(target->position);
        }
        std::cout << "Skill " << skill_id << " result: " << static_cast<int>(result) << std::endl;
    };
    skill_callbacks.on_skill_cooldown = [this](uint32_t skill_id, uint32_t cooldown_ms) {
        if (!skill_manager_ || skill_id == 0) {
            return;
        }
        skill_manager_->start_cooldown(skill_id, static_cast<int64_t>(cooldown_ms), now_ms());
    };
    skill_callbacks.on_cast_start = [this](uint64_t caster_id,
                                           uint32_t skill_id,
                                           uint64_t target_id,
                                           uint32_t cast_time_ms) {
        if (!skill_manager_ || !cast_bar_) {
            return;
        }
        if (player_ && caster_id != player_->get_id()) {
            return;
        }

        const int64_t start_ms = now_ms();
        const int64_t end_ms = start_ms + static_cast<int64_t>(cast_time_ms);
        const auto* tmpl = skill_manager_->get_template(skill_id);
        const std::string skill_name = tmpl ? tmpl->name : std::string();
        cast_bar_->start_cast(skill_name, start_ms, end_ms);
        skill_manager_->start_casting(skill_id, target_id,
                                      static_cast<int64_t>(cast_time_ms), start_ms);
    };
    skill_callbacks.on_cast_interrupt = [this](uint64_t caster_id, uint32_t /*skill_id*/) {
        if (player_ && caster_id != player_->get_id()) {
            return;
        }
        if (cast_bar_) {
            cast_bar_->cancel();
        }
        if (skill_manager_) {
            skill_manager_->cancel_casting();
        }
    };
    skill_callbacks.on_skill_list =
        [this](const std::vector<client::skill::ClientLearnedSkill>& skills) {
            if (!skill_manager_) {
                return;
            }

            std::vector<uint32_t> existing_ids;
            existing_ids.reserve(skill_manager_->get_learned_skills().size());
            for (const auto& slot : skill_manager_->get_learned_skills()) {
                if (slot.has_value()) {
                    existing_ids.push_back(slot->skill_id);
                }
            }
            for (uint32_t id : existing_ids) {
                skill_manager_->remove_learned_skill(id);
            }
            for (const auto& skill : skills) {
                skill_manager_->add_learned_skill(skill);
            }
        };
    skill_callbacks.on_parse_error = [](const std::string& error) {
        std::cerr << "Skill handler parse error: " << error << std::endl;
    };

    skill_handler_ = std::make_shared<handlers::SkillHandler>(std::move(skill_callbacks));
    skill_handler_->BindHandlers(*network_manager_);
}

/**
 * @brief 设置登录界面回调函数
 * 
 * 注册以下回调
 * - on_login: 用户点击登录按钮
 * - on_character_select: 用户选择角色
 * - on_character_create: 用户创建角色
 * - on_offline_play: 用户选择离线游戏
 */
void GameClient::setup_login_screen_callbacks() {
    if (!login_screen_) return;

    login_flow_.set_on_state_change([this](LoginFlowState /*old_state*/, LoginFlowState new_state) {
        if (login_screen_) {
            login_screen_->set_status(login_flow_.get_status_text());
        }

        if (new_state == LoginFlowState::TIMEOUT || new_state == LoginFlowState::FAILED) {
            if (login_screen_) {
                login_screen_->set_error(login_flow_.get_error().value_or("Unknown error"));
            }
            disconnect_from_server();
            start_offline_play();
        }
    });

    login_flow_.set_on_ready_to_send_login([this](const std::string& username, const std::string& password) {
        send_login_request(username, password);
    });
    
    // 登录回调 - 连接服务器并发送登录请求
    login_screen_->set_on_login([this](const std::string& username, const std::string& password) {
        if (!login_flow_.is_idle()) {
            return;
        }

        login_flow_.start_login(username, password);

        if (login_screen_) {
            login_screen_->set_state(LoginScreenState::CONNECTING);
            login_screen_->set_status(login_flow_.get_status_text());
        }

        if (is_connected()) {
            login_flow_.on_connect_success();
            return;
        }

        if (!connect_to_server()) {
            login_flow_.on_connect_failed("Network client not available");
        }
    });
    
    // 角色选择回调
    login_screen_->set_on_character_select([this](uint32_t character_id) {
        std::cout << "Character selected: " << character_id << std::endl;
        select_character(static_cast<uint64_t>(character_id));
    });
    
    // 角色创建回调
    login_screen_->set_on_character_create([this](const std::string& name, CharacterClass char_class, Gender gender) {
        std::cout << "Creating character: " << name << std::endl;
        create_char_name_ = name;
        create_char_class_ = char_class;
        create_char_gender_ = gender;
        create_char_level_ = 0;
        has_created_character_ = true;
        if (login_screen_) {
            login_screen_->set_created_character_info(name, char_class, gender, create_char_level_);
        }
        request_create_character(name, char_class, gender);
    });

    // 开始游戏回调
    login_screen_->set_on_start_game([this]() {
        if (!has_created_character_ || last_created_player_id_ == 0) {
            return;
        }
        select_character(last_created_player_id_);
    });

    // 离线游戏回调 - 复用 start_offline_play() 方法
    login_screen_->set_on_offline_play([this]() {
        start_offline_play();
    });
}

/**
 * @brief 加载登录界面背景资源
 * 
 * 从ChrSel.wil档案加载登录背景图片
 * 并尝试从多个档案加载用户密码输入框纹理
 */
void GameClient::load_login_background() {
    if (!resource_manager_ || !renderer_ || !login_screen_) {
        return;
    }
    
    const std::string archive_name = "ChrSel";
    const int background_index = 22;  // 背景图片在档案中的索引
    
    // 尝试多个可能的Data目录路径
    std::vector<std::string> data_paths = {
        "Data/",
        "../Data/",
        "../../Data/",
        "./Data/"
    };
    
    // 查找Data目录
    std::string data_path;
    for (const auto& path : data_paths) {
        std::ifstream test(path + archive_name + ".wil");
        if (test.good()) {
            data_path = path;
            break;
        }
    }
    
    if (data_path.empty()) {
        std::cerr << "Could not find Data directory for login assets" << std::endl;
        return;
    }
    
    // 加载ChrSel档案
    if (!resource_manager_->is_archive_loaded(archive_name)) {
        if (!resource_manager_->load_wil_archive(data_path + archive_name + ".wil")) {
            std::cerr << "Failed to load " << archive_name << " archive" << std::endl;
            return;
        }
    }
    
    // 获取背景精灵
    auto sprite_opt = resource_manager_->get_sprite(archive_name, background_index);
    if (!sprite_opt) {
        std::cerr << "Failed to load " << archive_name
                  << " background sprite index " << background_index << std::endl;
        return;
    }
    
    Sprite sprite = *sprite_opt;
    
    // 创建背景纹理
    auto texture = renderer_->create_texture_from_sprite(sprite, true);
    if (!texture) {
        std::cerr << "Failed to create login background texture" << std::endl;
        return;
    }
    
    login_screen_->set_background_texture(texture);
    
    // 加载登录动画帧（ChrSel 23帧到32帧，跳过22帧因为它与背景相同）
    std::vector<LoginScreen::AnimationFrame> animation_frames;
    const int anim_start = 23;
    const int anim_end = 32;
    
    for (int i = anim_start; i <= anim_end; ++i) {
        auto anim_sprite_opt = resource_manager_->get_sprite(archive_name, i);
        if (!anim_sprite_opt) {
            std::cerr << "Failed to get sprite for animation frame " << i << std::endl;
            continue;
        }
        
        // 保存偏移量信息（用于正确定位动画帧）
        Sprite anim_sprite = *anim_sprite_opt;
        if (!anim_sprite.is_valid()) {
            std::cerr << "Animation frame " << i << " sprite is invalid" << std::endl;
            continue;
        }
        
        int sprite_offset_x = anim_sprite.offset_x;
        int sprite_offset_y = anim_sprite.offset_y;
        int w = anim_sprite.width;
        int h = anim_sprite.height;
        
        auto anim_texture = renderer_->create_texture_from_sprite(anim_sprite, true);
        if (anim_texture) {
            LoginScreen::AnimationFrame frame;
            frame.texture = anim_texture;
            // The login animation frames are center crops of the background; positioning is handled in LoginScreen.
            frame.offset_x = 0;
            frame.offset_y = 0;
            animation_frames.push_back(frame);
            std::cout << "Loaded animation frame " << i << " (" << w << "x" << h 
                      << ") sprite offset: (" << sprite_offset_x << ", " << sprite_offset_y << ")" << std::endl;
        } else {
            std::cerr << "Failed to create texture for animation frame " << i << std::endl;
        }
    }
    
    if (!animation_frames.empty()) {
        size_t frame_count = animation_frames.size();
        login_screen_->set_login_animation_frames(std::move(animation_frames));
        std::cout << "Loaded " << frame_count << " login animation frames" << std::endl;
    } else {
        std::cout << "No login animation frames loaded" << std::endl;
    }
}

void GameClient::load_character_create_assets() {
    if (!resource_manager_ || !renderer_ || !login_screen_) {
        return;
    }

    // 尝试多个可能的Data目录路径
    std::vector<std::string> data_paths = {
        "Data/",
        "../Data/",
        "../../Data/",
        "./Data/"
    };

    std::string data_path;
    std::string wil_filename;
    std::string archive_name;
    for (const auto& path : data_paths) {
        std::ifstream test1(path + "Prguse.wil");
        if (test1.good()) {
            data_path = path;
            wil_filename = "Prguse.wil";
            archive_name = "Prguse";
            break;
        }
        std::ifstream test2(path + "prguse.wil");
        if (test2.good()) {
            data_path = path;
            wil_filename = "prguse.wil";
            archive_name = "prguse";
            break;
        }
    }

    if (data_path.empty()) {
        std::cerr << "Could not find Data directory for character create assets" << std::endl;
        return;
    }

    if (archive_name.empty()) {
        std::cerr << "Could not determine Prguse archive name" << std::endl;
        return;
    }


    if (!resource_manager_->is_archive_loaded(archive_name)) {
        if (!resource_manager_->load_wil_archive(data_path + wil_filename)) {
            std::cerr << "Failed to load " << archive_name << " archive" << std::endl;
            return;
        }
    }

    auto load_texture = [&](int index) -> std::shared_ptr<Texture> {
        auto sprite_opt = resource_manager_->get_sprite(archive_name, index);
        if (!sprite_opt || !sprite_opt->is_valid()) {
            return nullptr;
        }

        Sprite sprite = *sprite_opt;
        return renderer_->create_texture_from_sprite(sprite, true);
    };

    // 背景 (Prguse 65)
    if (auto tex = load_texture(65)) {
        login_screen_->set_create_background_texture(tex);
    } else {
        std::cerr << "Failed to load Prguse create background (65)" << std::endl;
    }

    // "创建新人"按钮已集成在背景 (Prguse 69)

    // 创建面板 (Prguse 73)
    if (auto tex = load_texture(73)) {
        login_screen_->set_class_panel_texture(tex);
    }

    // 选择图标 (Prguse 74-78)
    {
        std::vector<std::shared_ptr<Texture>> textures;
        textures.reserve(5);
        for (int idx = 74; idx <= 78; ++idx) {
            textures.push_back(load_texture(idx));
        }
        login_screen_->set_class_select_textures(std::move(textures));
    }

    // 高亮图标 (Prguse 55-59)
    {
        std::vector<std::shared_ptr<Texture>> textures;
        textures.reserve(5);
        for (int idx = 55; idx <= 59; ++idx) {
            textures.push_back(load_texture(idx));
        }
        login_screen_->set_class_highlight_textures(std::move(textures));
    }

    // 角色预览资源档案：加载 ChrSel.wil（预览帧按需加载）
    std::string chrsel_archive;
    if (resource_manager_->is_archive_loaded("ChrSel")) {
        chrsel_archive = "ChrSel";
    } else if (resource_manager_->is_archive_loaded("chrsel")) {
        chrsel_archive = "chrsel";
    } else {
        std::string chrsel_path;
        std::ifstream test_upper(data_path + "ChrSel.wil");
        if (test_upper.good()) {
            chrsel_path = data_path + "ChrSel.wil";
            chrsel_archive = "ChrSel";
        } else {
            std::ifstream test_lower(data_path + "chrsel.wil");
            if (test_lower.good()) {
                chrsel_path = data_path + "chrsel.wil";
                chrsel_archive = "chrsel";
            }
        }

        if (chrsel_archive.empty()) {
            std::cerr << "Failed to find ChrSel.wil archive for character preview" << std::endl;
            return;
        }

        if (!resource_manager_->load_wil_archive(chrsel_path)) {
            std::cerr << "Failed to load " << chrsel_archive << " archive for character preview" << std::endl;
            return;
        }
    }
    // 角色预览帧改为按需加载
}

void GameClient::send_login_request(const std::string& username, const std::string& password) {
    if (!is_connected() || !network_manager_) {
        return;
    }

    mir2::common::LoginRequest request;
    request.username = username;
    request.password = password;
    request.version = "0.1.0";
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeLoginRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode login request" << std::endl;
        login_flow_.on_auth_failed("Invalid login request");
        return;
    }

    network_manager_->send_message(mir2::common::MsgId::kLoginReq, payload);
}

// =============================================================================
// 界面切换过渡 (Transition)
// =============================================================================

void GameClient::start_transition() {
    if (transition_duration_ <= 0.0f) {
        transition_active_ = false;
        transition_timer_ = 0.0f;
        return;
    }

    transition_active_ = true;
    transition_timer_ = 0.0f;
}

void GameClient::update_transition(float delta_time) {
    if (!transition_active_) {
        return;
    }

    transition_timer_ += delta_time;
    if (transition_timer_ >= transition_duration_) {
        transition_timer_ = transition_duration_;
        transition_active_ = false;
    }
}

void GameClient::render_transition() {
    if (!transition_active_ || transition_duration_ <= 0.0f || !renderer_) {
        return;
    }

    float t = transition_timer_ / transition_duration_;
    t = std::clamp(t, 0.0f, 1.0f);
    const uint8_t alpha = static_cast<uint8_t>(255.0f * (1.0f - t));

    renderer_->set_blend_mode(SDL_BLENDMODE_BLEND);
    renderer_->draw_rect({0, 0, config_.window_width, config_.window_height}, {0, 0, 0, alpha});
}

// =============================================================================
// 角色管理方法
// =============================================================================

/**
 * @brief 请求角色列表
 * 向服务器发送角色列表请求
 */
void GameClient::request_character_list() {
    if (!is_connected() || !network_manager_) {
        return;
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto token_offset = builder.CreateString(session_token_);
    const auto req = mir2::proto::CreateRoleListReq(builder, account_id_, token_offset);
    builder.Finish(req);
    const auto payload = build_payload(builder);
    network_manager_->send_message(mir2::common::MsgId::kRoleListReq, payload);
}

/**
 * @brief 请求创建角色
 * @param name 角色名称
 * @param char_class 角色职业
 * @param gender 角色性别
 */
void GameClient::request_create_character(const std::string& name, CharacterClass char_class, Gender gender) {
    if (!is_connected() || !network_manager_) {
        return;
    }

    mir2::common::CreateCharacterRequest request;
    request.name = name;
    request.profession = to_proto_profession(char_class);
    request.gender = to_proto_gender(gender);
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeCreateCharacterRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode create character request" << std::endl;
        return;
    }
    network_manager_->send_message(mir2::common::MsgId::kCreateRoleReq, payload);
}

/**
 * @brief 选择角色进入游戏
 * @param character_id 角色ID
 */
void GameClient::select_character(uint64_t character_id) {
    if (!is_connected() || !network_manager_) {
        return;
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto req = mir2::proto::CreateSelectRoleReq(builder, character_id);
    builder.Finish(req);
    const auto payload = build_payload(builder);
    network_manager_->send_message(mir2::common::MsgId::kSelectRoleReq, payload);
}

// =============================================================================
// NPC交互方法
// =============================================================================

void GameClient::InteractWithNpc(uint64_t npc_id) {
    if (!is_connected() || !network_manager_) {
        return;
    }
    if (npc_id == 0) {
        return;
    }

    mir2::common::NpcInteractReq request;
    request.npc_id = npc_id;
    request.player_id = player_ ? static_cast<uint64_t>(player_->get_id()) : 0;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeNpcInteractReq(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode NPC interact request" << std::endl;
        return;
    }

    network_manager_->send_message(mir2::common::MsgId::kNpcInteractReq, payload);
}

void GameClient::SelectNpcMenuOption(uint8_t option_index) {
    uint64_t npc_id = 0;
    if (npc_handler_) {
        npc_id = npc_handler_->state().npc_id;
    }
    if (npc_id == 0) {
        return;
    }
    send_npc_menu_select(npc_id, option_index);
}

void GameClient::CloseNpcDialog() {
    if (npc_dialog_ui_) {
        npc_dialog_ui_->hide_dialog();
    }
}

// =============================================================================
// 状态切换方法
// =============================================================================

/**
 * @brief 切换到登录界面
 */
void GameClient::go_to_login() {
    set_state(GameState::LOGIN);
}

/**
 * @brief 切换到角色选择界面
 */
void GameClient::go_to_character_select() {
    set_state(GameState::CHARACTER_SELECT);
}

/**
 * @brief 切换到角色创建界面
 */
void GameClient::go_to_character_create() {
    create_char_name_.clear();
    create_char_class_ = CharacterClass::WARRIOR;
    create_char_gender_ = Gender::MALE;
    create_char_level_ = 0;
    has_created_character_ = false;
    if (login_screen_) {
        login_screen_->set_created_character_info("", create_char_class_, create_char_gender_, create_char_level_);
    }
    set_state(GameState::CHARACTER_CREATE);
}

/**
 * @brief 进入游戏
 * 加载默认地图并切换到游戏状态
 */
void GameClient::enter_game() {
    entity_manager_.clear();

    // 设置 ActorRenderer 数据路径并加载档案
    if (actor_renderer_) {
        // 查找 Data 目录
        std::vector<std::string> data_paths = {"Data/", "../Data/", "../../Data/", "./Data/"};
        for (const auto& path : data_paths) {
            std::ifstream test(path + "Hum.wil");
            if (test.good()) {
                actor_renderer_->set_data_path(path);
                break;
            }
            // 尝试小写
            std::ifstream test2(path + "hum.wil");
            if (test2.good()) {
                actor_renderer_->set_data_path(path);
                break;
            }
        }
        // 加载角色相关档案
        actor_renderer_->load_archives();

        // 初始化玩家角色渲染数据
        if (player_) {
            player_actor_.data.id = 1;  // 玩家ID
            player_actor_.data.map_x = static_cast<uint16_t>(player_->get_position().x);
            player_actor_.data.map_y = static_cast<uint16_t>(player_->get_position().y);
            player_actor_.data.race = ActorRace::HUMAN;
            player_actor_.data.sex = (player_->get_gender() == Gender::MALE) ? 0 : 1;
            player_actor_.data.dress = static_cast<uint8_t>(player_actor_.data.sex);
            player_actor_.data.hair = 0;
            player_actor_.data.weapon = 0;
            player_actor_.data.direction = 4;  // 默认面向
            actor_renderer_->load_actor_surfaces(player_actor_);
            actor_renderer_->set_action(player_actor_, ActorAction::STAND);  // 初始化动画帧

            last_player_pos_ = {static_cast<int>(player_actor_.data.map_x),
                                static_cast<int>(player_actor_.data.map_y)};
            player_interpolator_.set_immediate(player_->get_position());

            // 初始化摄像机到玩家位置
            camera_.center_on({player_actor_.data.map_x, player_actor_.data.map_y});

            Entity entity;
            entity.id = player_->get_id();
            entity.type = EntityType::Player;
            entity.position = player_->get_position();
            entity.direction = player_actor_.data.direction;
            entity.stats.hp = player_->get_data().stats.hp;
            entity.stats.max_hp = player_->get_data().stats.max_hp;
            entity.stats.mp = player_->get_data().stats.mp;
            entity.stats.max_mp = player_->get_data().stats.max_mp;
            entity.stats.level = static_cast<uint16_t>(player_->get_data().stats.level);
            entity_manager_.add_entity(entity);
        }
    }

    // 加载默认地图（地图ID 0 = "0.map"）
    if (map_system_) {
        if (map_system_->load_map(0)) {
            std::cout << "Loaded map 0" << std::endl;

            // 将地图数据设置给渲染器
            if (map_renderer_) {
                map_renderer_->set_map(map_system_->get_map_data());
            }
        } else {
            std::cerr << "Failed to load map 0" << std::endl;
        }
    }

    set_state(GameState::PLAYING);
}

/**
 * @brief 启动离线游戏模式
 *
 * 当服务器不可用时，创建测试角色并直接进入游戏
 * 复用 on_offline_play_ 回调中定义的离线游戏逻辑
 */
void GameClient::start_offline_play() {
    std::cout << "Starting offline play (server unavailable)" << std::endl;

    // 创建测试角色
    player_ = std::make_unique<ClientCharacter>(1, "TestPlayer", CharacterClass::WARRIOR, Gender::MALE);
    player_->set_position({100, 100});
    camera_.center_on(player_->get_position());

    // 设置玩家动画
    player_entity_ = std::make_unique<AnimatedEntity>();
    character_animator_ = std::make_unique<CharacterAnimator>(*animation_manager_);
    character_animator_->setup_character(*player_entity_, player_->get_class(), player_->get_gender());
    player_entity_->set_position(player_->get_position());

    enter_game();
}

// =============================================================================
// 状态相关的输入处理方法
// =============================================================================

/**
 * @brief 登录状态的输入处理
 * 输入由LoginScreen通过事件转发处理
 */
void GameClient::process_input_login() {
    // Input is handled by LoginScreen via event forwarding
}

/**
 * @brief 角色选择状态的输入处理
 * 输入由LoginScreen通过事件转发处理
 */
void GameClient::process_input_character_select() {
    // Input is handled by LoginScreen via event forwarding
}

/**
 * @brief 角色创建状态的输入处理
 * 输入由LoginScreen通过事件转发处理
 */
void GameClient::process_input_character_create() {
    // Input is handled by LoginScreen via event forwarding
}

/**
 * @brief 游戏状态的输入处理
 * 
 * 处理内容
 * - 调试快捷键（G=网格, W=可行走区域）
 * - 鼠标点击移动
 */
void GameClient::process_input_playing() {
    if (!player_) return;
    
    // 调试快捷键
    if (input_.key_pressed(SDL_SCANCODE_G) && map_renderer_) {
        // G键切换网格显示
        map_renderer_->set_debug_grid(!map_renderer_->is_debug_grid_enabled());
    }
    if (input_.key_pressed(SDL_SCANCODE_W) && map_renderer_) {
        // W键切换可行走区域显示
        map_renderer_->set_debug_walkability(!map_renderer_->is_debug_walkability_enabled());
    }

    process_skill_hotkeys();

    // NPC对话打开时屏蔽场景点击
    if (npc_dialog_ui_ && npc_dialog_ui_->is_visible()) {
        return;
    }
    
    // 鼠标左键点击移动
    if (input_.mouse_left_clicked) {
        // 将屏幕坐标转换为世界坐标
        Position target = screen_to_world(input_.mouse_x, input_.mouse_y);
        if (target.x >= 0 && target.y >= 0) {
            const Entity* target_entity = nullptr;
            const auto entities_at_target = entity_manager_.query_at(target);
            for (const auto* entity : entities_at_target) {
                if (!entity) {
                    continue;
                }
                if (player_ && entity->id == player_->get_id()) {
                    continue;
                }
                if (entity->type == EntityType::NPC) {
                    target_entity = entity;
                    break;
                }
                if (!target_entity) {
                    target_entity = entity;
                }
            }

            if (target_entity) {
                Actor* actor = find_actor(static_cast<int32_t>(target_entity->id));
                if (actor) {
                    set_focused_actor(actor);
                } else {
                    set_focused_actor(nullptr);
                }

                if (target_entity->type == EntityType::NPC) {
                    InteractWithNpc(target_entity->id);
                    return;
                }
            } else {
                set_focused_actor(nullptr);
            }

            input_.target_position = target;
            input_.has_move_target = true;
            
            // 离线模式直接移动，在线模式发送移动请求
            if (!is_connected()) {
                player_->set_position(target);
                player_entity_->set_position(target);
                entity_manager_.update_entity_position(player_->get_id(), target, player_actor_.data.direction);
                player_interpolator_.set_immediate(target);
                camera_.center_on(target);
            } else {
                bool accepted = false;
                if (movement_controller_) {
                    accepted = movement_controller_->request_move(player_->get_position(), target);
                } else {
                    send_move_request(target);
                    accepted = true;
                }
                if (!accepted) {
                    input_.has_move_target = false;
                }
            }
        }
    }
    
}

void GameClient::process_skill_hotkeys() {
    if (!player_ || !skill_executor_) {
        return;
    }

    static constexpr SDL_Scancode kHotkeys[] = {
        SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
        SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8
    };

    const int current_mp = player_->get_data().stats.mp;
    uint64_t target_id = 0;
    if (focused_actor_) {
        target_id = static_cast<uint64_t>(focused_actor_->data.id);
    } else {
        target_id = static_cast<uint64_t>(player_->get_id());
    }

    const size_t hotkey_count = sizeof(kHotkeys) / sizeof(kHotkeys[0]);
    for (size_t i = 0; i < hotkey_count; ++i) {
        if (!input_.key_pressed(kHotkeys[i])) {
            continue;
        }
        const uint8_t slot = static_cast<uint8_t>(i + 1);
        skill_executor_->try_use_skill_by_hotkey(slot, current_mp, target_id);
    }

    if (input_.key_pressed(SDL_SCANCODE_K) && skill_book_) {
        skill_book_->toggle();
    }
}

// =============================================================================
// 状态相关的更新方法
// =============================================================================

/**
 * @brief 登录状态的逻辑更新
 * @param delta_time 帧间隔时间
 */
void GameClient::update_login(float delta_time) {
    login_flow_.update(delta_time);

    if (login_flow_.get_state() == LoginFlowState::CONNECTING) {
        if (!network_manager_) {
            login_flow_.on_connect_failed("Network manager not available");
        } else if (network_manager_->get_state() == mir2::client::ConnectionState::DISCONNECTED) {
            login_flow_.on_connect_failed(error_code_to_string(network_manager_->get_last_error()));
        }
    }

    if (login_screen_) {
        login_screen_->update(delta_time);
    }
}

/**
 * @brief 角色选择状态的逻辑更新
 * @param delta_time 帧间隔时间
 */
void GameClient::update_character_select(float delta_time) {
    if (login_screen_) {
        login_screen_->update(delta_time);
    }
}

/**
 * @brief 角色创建状态的逻辑更新
 * @param delta_time 帧间隔时间
 */
void GameClient::update_character_create(float delta_time) {
    if (login_screen_) {
        login_screen_->update(delta_time);
    }
}

/**
 * @brief 游戏状态的逻辑更新
 * @param delta_time 帧间隔时间
 * 
 * 更新内容
 * - 地图动画
 * - 玩家动画
 * - 伤害数字动画
 * - 摄像机平滑跟随
 */
void GameClient::update_playing(float delta_time) {
    if (async_loader_) {
        async_loader_->poll();
    }

    const uint32_t delta_ms = static_cast<uint32_t>(delta_time * 1000.0f);

    // 更新地图动画（如水面、火焰等）
    if (map_renderer_) {
        map_renderer_->update(delta_time);
    }

    if (movement_controller_) {
        movement_controller_->update(delta_ms);
    }

    monster_manager_.update(static_cast<float>(delta_ms));

    // 更新玩家角色动画
    if (actor_renderer_) {
        if (player_) {
            if (last_player_pos_.x < 0 || last_player_pos_.y < 0) {
                last_player_pos_ = player_->get_position();
                player_interpolator_.set_immediate(last_player_pos_);
            }

            Position current_pos = player_interpolator_.get_tile_position();

            int dx = current_pos.x - last_player_pos_.x;
            int dy = current_pos.y - last_player_pos_.y;
            bool moved = (dx != 0 || dy != 0);

            if (moved) {
                player_actor_.data.direction = direction_from_delta(dx, dy);
                if (player_actor_.anim.current_action != ActorAction::WALK) {
                    actor_renderer_->set_action(player_actor_, ActorAction::WALK);
                }
            } else if (player_actor_.anim.current_action != ActorAction::STAND) {
                actor_renderer_->set_action(player_actor_, ActorAction::STAND);
            }

            player_actor_.data.map_x = static_cast<uint16_t>(current_pos.x);
            player_actor_.data.map_y = static_cast<uint16_t>(current_pos.y);
            if (player_entity_) {
                player_entity_->set_position(current_pos);
            }
            last_player_pos_ = current_pos;

            actor_renderer_->update(player_actor_, delta_ms);
        }

        // 更新场景中其他角色
        for (auto& actor : scene_actors_) {
            Entity* entity = entity_manager_.get_entity(static_cast<uint64_t>(actor.data.id));
            if (entity) {
                entity->interpolator.update(delta_ms);
                const Position interp_pos = entity->interpolator.get_tile_position();
                actor.data.map_x = static_cast<uint16_t>(interp_pos.x);
                actor.data.map_y = static_cast<uint16_t>(interp_pos.y);
                actor.data.direction = entity->direction;
            }
            actor_renderer_->update(actor, delta_ms);
        }
    }

    // 更新旧的玩家实体动画（兼容）
    if (player_entity_) {
        player_entity_->update(delta_time);
    }

    // 更新伤害数字动画
    if (ui_renderer_) {
        ui_renderer_->update_damage_numbers(delta_time);
    }

    if (effect_player_) {
        effect_player_->update(delta_time);
    }

    // 摄像机平滑跟随玩家
    if (player_) {
        const legend2::PositionF interp_pos = player_interpolator_.get_position();
        float target_x = interp_pos.x * constants::TILE_WIDTH + constants::TILE_WIDTH / 2;
        float target_y = interp_pos.y * constants::TILE_HEIGHT + constants::TILE_HEIGHT / 2;
        camera_.move_towards(target_x, target_y, 5.0f * delta_time);
    }

    update_skill_system(delta_time);

    if (ui_manager_) {
        ui_manager_->update(delta_time);
    }
}

void GameClient::update_skill_system(float delta_time) {
    (void)delta_time;

    const int64_t current_ms = now_ms();
    if (skill_manager_) {
        skill_manager_->update(current_ms);
    }
    if (skill_bar_) {
        skill_bar_->update(current_ms);
    }
    if (cast_bar_) {
        cast_bar_->update(current_ms);
    }
    if (skill_book_) {
        skill_book_->update(current_ms);
    }
}

// =============================================================================
// 状态相关的渲染方法
// =============================================================================

/**
 * @brief 渲染登录界面
 */
void GameClient::render_login() {
    if (login_screen_) {
        login_screen_->render();
    }
}

/**
 * @brief 渲染角色选择界面
 */
void GameClient::render_character_select() {
    if (login_screen_) {
        login_screen_->render();
    }
}

/**
 * @brief 渲染角色创建界面
 */
void GameClient::render_character_create() {
    if (login_screen_) {
        login_screen_->render();
    }
}

/**
 * @brief 渲染游戏画面
 * 
 * 渲染顺序（从底层到顶层）
 * 1. 地图（地面、物件）
 * 2. 玩家角色
 * 3. 伤害数字
 */
void GameClient::render_playing() {
    // 渲染地图
    if (map_renderer_) {
        map_renderer_->render(camera_);
    }

    // 使用 ActorRenderer 渲染所有角色（按Z-order排序）
    if (actor_renderer_) {
        // 构建待渲染角色列表
        std::vector<Actor*> actors_to_render;
        if (player_) {
            actors_to_render.push_back(&player_actor_);
        }
        for (auto& actor : scene_actors_) {
            actors_to_render.push_back(&actor);
        }

        constexpr int kMonsterViewPadding = 2;
        std::vector<Actor> monster_actors;
        const auto visible_monsters = monster_manager_.get_visible_monsters(camera_, kMonsterViewPadding);
        monster_actors.reserve(visible_monsters.size());
        for (const monster::ClientMonster* monster : visible_monsters) {
            if (!monster) {
                continue;
            }
            monster_actors.push_back(monster_manager_.convert_to_actor(*monster));
        }
        for (auto& actor : monster_actors) {
            actors_to_render.push_back(&actor);
        }

        // 批量绘制角色（内部会进行Z-order排序）
        if (!actors_to_render.empty()) {
            actor_renderer_->draw_actors(actors_to_render, camera_, focused_actor_);
        }
    }

    if (effect_player_) {
        effect_player_->render(camera_);
    }

    // 渲染伤害数字
    if (ui_renderer_) {
        ui_renderer_->render_damage_numbers(camera_);
    }

    render_skill_ui();

    // 渲染UI层（NPC对话等）
    if (ui_manager_ && ui_renderer_) {
        ui_manager_->render(*ui_renderer_);
    }

}

void GameClient::render_skill_ui() {
    if (!renderer_) {
        return;
    }

    if (skill_bar_) {
        constexpr int kSlotSize = 40;
        constexpr int kSlotPadding = 2;
        const int bar_width = ui::skill::SkillBar::SLOT_COUNT * kSlotSize +
                              (ui::skill::SkillBar::SLOT_COUNT - 1) * kSlotPadding;
        int x = (config_.window_width - bar_width) / 2;
        int y = config_.window_height - kSlotSize - 20;
        if (x < 0) {
            x = 0;
        }
        if (y < 0) {
            y = 0;
        }
        skill_bar_->render(*renderer_, x, y);
    }

    if (skill_book_ && skill_book_->is_open()) {
        skill_book_->render(*renderer_);
    }

    if (cast_bar_ && cast_bar_->is_active() && player_) {
        const Position screen_pos = camera_.world_to_screen(player_->get_position());
        const int cast_y = screen_pos.y - 50;
        cast_bar_->render(*renderer_, screen_pos.x, cast_y);
    }

    if (skill_target_indicator_ && skill_target_indicator_->is_visible()) {
        skill_target_indicator_->render(*renderer_, camera_);
    }
}

// =============================================================================
// 工具方法
// =============================================================================

/**
 * @brief 将屏幕坐标转换为世界坐标
 * @param screen_x 屏幕X坐标
 * @param screen_y 屏幕Y坐标
 * @return 对应的世界坐标（地图格子坐标）
 */
Position GameClient::screen_to_world(int screen_x, int screen_y) const {
    return camera_.screen_to_world({screen_x, screen_y});
}

/**
 * @brief 发送移动请求到服务器
 * @param target 目标位置
 */
void GameClient::send_move_request(const Position& target) {
    if (!is_connected() || !network_manager_) {
        return;
    }

    mir2::common::MoveRequest request;
    request.target_x = target.x;
    request.target_y = target.y;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeMoveRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode move request" << std::endl;
        return;
    }
    network_manager_->send_message(mir2::common::MsgId::kMoveReq, payload);
}

/**
 * @brief 发送攻击请求到服务器
 * @param target_id 攻击目标的实体ID
 */
void GameClient::send_attack_request(uint32_t target_id) {
    if (!is_connected() || !network_manager_) {
        return;
    }

    mir2::common::AttackRequest request;
    request.target_id = static_cast<uint64_t>(target_id);
    request.target_type = mir2::proto::EntityType::PLAYER;
    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeAttackRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode attack request" << std::endl;
        return;
    }
    network_manager_->send_message(mir2::common::MsgId::kAttackReq, payload);
}

bool GameClient::validate_skill_target(uint64_t target_id) const {
    if (!player_) {
        return false;
    }
    if (target_id == 0) {
        std::cerr << "Skill target is missing" << std::endl;
        return false;
    }

    const Entity* target = entity_manager_.get_entity(target_id);
    if (!target) {
        std::cerr << "Skill target not found: " << target_id << std::endl;
        return false;
    }

    constexpr int kMaxSkillRange = 20;
    const int distance_sq = player_->get_position().distance_squared(target->position);
    if (distance_sq > kMaxSkillRange * kMaxSkillRange) {
        std::cerr << "Skill target out of range: " << target_id << std::endl;
        return false;
    }

    return true;
}

void GameClient::send_skill_request(uint32_t skill_id, uint64_t target_id) {
    if (!is_connected() || !network_manager_) {
        return;
    }
    if (!validate_skill_target(target_id)) {
        return;
    }

    mir2::common::SkillRequest request;
    request.skill_id = skill_id;
    request.target_id = target_id;
    request.target_type = mir2::proto::EntityType::PLAYER;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeSkillRequest(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode skill request" << std::endl;
        return;
    }
    network_manager_->send_message(mir2::common::MsgId::kSkillReq, payload);
}

void GameClient::send_npc_menu_select(uint64_t npc_id, uint8_t option_index) {
    if (!is_connected() || !network_manager_) {
        return;
    }

    mir2::common::NpcMenuSelectReq request;
    request.npc_id = npc_id;
    request.option_index = option_index;

    mir2::common::MessageCodecStatus status = mir2::common::MessageCodecStatus::kOk;
    const auto payload = mir2::common::EncodeNpcMenuSelect(request, &status);
    if (status != mir2::common::MessageCodecStatus::kOk || payload.empty()) {
        std::cerr << "Failed to encode NPC menu select request" << std::endl;
        return;
    }

    network_manager_->send_message(mir2::common::MsgId::kNpcMenuSelect, payload);
}

// =============================================================================
// 场景角色管理 (Scene Actor Management)
// =============================================================================

/**
 * @brief 添加场景角色（其他玩家/怪物/NPC）
 * @param actor_id 角色唯一ID
 * @return 新添加的角色指针
 */
Actor* GameClient::add_scene_actor(int32_t actor_id) {
    // 检查是否已存在
    for (auto& actor : scene_actors_) {
        if (actor.data.id == actor_id) {
            return &actor;
        }
    }

    // 添加新角色
    scene_actors_.emplace_back();
    Actor& new_actor = scene_actors_.back();
    new_actor.data.id = actor_id;
    new_actor.anim.current_action = ActorAction::STAND;

    return &new_actor;
}

/**
 * @brief 移除场景角色
 * @param actor_id 角色唯一ID
 */
void GameClient::remove_scene_actor(int32_t actor_id) {
    // 如果是当前选中的角色，清除选中状态
    if (focused_actor_ && focused_actor_->data.id == actor_id) {
        focused_actor_ = nullptr;
    }

    // 从列表中移除
    scene_actors_.erase(
        std::remove_if(scene_actors_.begin(), scene_actors_.end(),
                       [actor_id](const Actor& a) { return a.data.id == actor_id; }),
        scene_actors_.end()
    );
}

/**
 * @brief 根据ID查找场景角色
 * @param actor_id 角色唯一ID
 * @return 角色指针，未找到返回nullptr
 */
Actor* GameClient::find_actor(int32_t actor_id) {
    // 检查是否是玩家角色
    if (player_actor_.data.id == actor_id) {
        return &player_actor_;
    }

    // 在场景角色中查找
    for (auto& actor : scene_actors_) {
        if (actor.data.id == actor_id) {
            return &actor;
        }
    }

    return nullptr;
}

} // namespace mir2::game
