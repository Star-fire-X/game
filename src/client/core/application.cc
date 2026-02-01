// =============================================================================
// Legend2 应用程序框架实现 (Application Framework Implementation)
// =============================================================================

#include "core/application.h"
#include <iostream>

namespace mir2::core {

Application::Application()
    : frame_timer_(constants::TARGET_FPS)
{
}

Application::~Application() {
    shutdown();
}

bool Application::initialize(const AppConfig& config) {
    if (initialized_) {
        std::cerr << "Application already initialized" << std::endl;
        return false;
    }

    config_ = config;

    std::cout << "Initializing application..." << std::endl;

    // 初始化SDL
    if (!init_sdl()) {
        return false;
    }

    // 创建窗口
    if (!create_window()) {
        return false;
    }

    // 设置帧率
    frame_timer_.set_target_fps(config_.target_fps);

    // 调用子类初始化
    if (!on_init()) {
        std::cerr << "Application on_init() failed" << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "Application initialized successfully" << std::endl;
    return true;
}

bool Application::init_sdl() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }
    std::cout << "  SDL initialized" << std::endl;
    return true;
}

bool Application::create_window() {
    Uint32 window_flags = SDL_WINDOW_SHOWN;
    if (config_.fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    window_.reset(SDL_CreateWindow(
        config_.window_title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config_.window_width,
        config_.window_height,
        window_flags
    ));

    if (!window_) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    std::cout << "  Window created (" << config_.window_width << "x"
              << config_.window_height << ")" << std::endl;
    return true;
}

int Application::run() {
    if (!initialized_) {
        std::cerr << "Application not initialized" << std::endl;
        return 1;
    }

    running_ = true;
    std::cout << "Entering main loop..." << std::endl;

    while (running_) {
        frame_timer_.begin_frame();
        on_frame_begin();

        // 处理SDL事件
        process_events();

        if (!running_) break;

        // 更新逻辑
        float delta_time = frame_timer_.get_delta_time();
        on_update(delta_time);

        // 渲染画面
        on_render();

        on_frame_end();
        frame_timer_.end_frame();
    }

    std::cout << "Exiting main loop" << std::endl;
    return 0;
}

void Application::process_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (!on_event(event)) {
            running_ = false;
            return;
        }
    }
}

bool Application::on_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT:
            return false;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                return false;
            }
            break;

        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                return false;
            }
            break;
    }
    return true;
}

void Application::quit() {
    running_ = false;
}

void Application::shutdown() {
    if (!initialized_) {
        return;
    }

    std::cout << "Shutting down application..." << std::endl;
    running_ = false;

    // 调用子类清理
    on_cleanup();

    // 销毁窗口
    window_.reset();

    // 退出SDL
    SDL_Quit();

    initialized_ = false;
    std::cout << "Application shutdown complete" << std::endl;
}

} // namespace mir2::core
