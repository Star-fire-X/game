// =============================================================================
// Legend2 应用程序框架 (Application Framework)
//
// 功能说明:
//   - SDL生命周期管理
//   - 窗口创建和管理
//   - 主循环框架
//   - 基础事件处理
//
// 设计原则:
//   - 与游戏逻辑解耦
//   - 提供可复用的应用程序基础设施
// =============================================================================

#ifndef LEGEND2_CORE_APPLICATION_H
#define LEGEND2_CORE_APPLICATION_H

#include <SDL.h>
#include <string>
#include <functional>
#include "core/sdl_resource.h"
#include "core/timer.h"
#include "common/types.h"

namespace mir2::core {

// 引入公共类型定义
using namespace mir2::common;

/// 应用程序配置
struct AppConfig {
    int window_width = 800;                      // 窗口宽度
    int window_height = 600;                     // 窗口高度
    std::string window_title = "Legend2";        // 窗口标题
    bool fullscreen = false;                     // 是否全屏
    bool vsync = true;                           // 是否启用垂直同步
    int target_fps = constants::TARGET_FPS;      // 目标FPS
};

/// 应用程序接口
/// 定义应用程序的生命周期方法
class IApplication {
public:
    virtual ~IApplication() = default;

    /// 初始化应用程序
    /// @return 初始化成功返回true
    virtual bool on_init() = 0;

    /// 处理SDL事件
    /// @param event SDL事件
    /// @return 返回false表示退出应用
    virtual bool on_event(const SDL_Event& event) = 0;

    /// 更新逻辑
    /// @param delta_time 帧间隔时间(秒)
    virtual void on_update(float delta_time) = 0;

    /// 渲染画面
    virtual void on_render() = 0;

    /// 清理资源
    virtual void on_cleanup() = 0;
};

/// 应用程序基类
/// 管理SDL生命周期和主循环
class Application {
public:
    Application();
    virtual ~Application();

    // 禁止拷贝
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /// 初始化应用程序
    /// @param config 应用程序配置
    /// @return 初始化成功返回true
    bool initialize(const AppConfig& config);

    /// 运行主循环
    /// @return 退出码
    int run();

    /// 请求退出
    void quit();

    /// 检查是否正在运行
    bool is_running() const { return running_; }

    /// 获取配置
    const AppConfig& get_config() const { return config_; }

    /// 获取帧计时器
    FrameTimer& get_frame_timer() { return frame_timer_; }
    const FrameTimer& get_frame_timer() const { return frame_timer_; }

    /// 获取SDL窗口
    SDL_Window* get_window() const { return window_.get(); }

    /// 获取窗口尺寸
    int get_window_width() const { return config_.window_width; }
    int get_window_height() const { return config_.window_height; }

protected:
    /// 子类实现：初始化回调
    virtual bool on_init() { return true; }

    /// 子类实现：事件处理回调
    /// @return 返回false表示退出应用
    virtual bool on_event(const SDL_Event& event);

    /// 子类实现：更新逻辑回调
    virtual void on_update(float delta_time) { (void)delta_time; }

    /// 子类实现：渲染回调
    virtual void on_render() {}

    /// 子类实现：清理回调
    virtual void on_cleanup() {}

    /// 子类实现：每帧开始回调
    virtual void on_frame_begin() {}

    /// 子类实现：每帧结束回调
    virtual void on_frame_end() {}

private:
    /// 初始化SDL
    bool init_sdl();

    /// 创建窗口
    bool create_window();

    /// 处理所有待处理的SDL事件
    void process_events();

    /// 关闭应用程序
    void shutdown();

private:
    AppConfig config_;
    FrameTimer frame_timer_;
    SDLWindowPtr window_;
    bool running_ = false;
    bool initialized_ = false;
};

} // namespace mir2::core

#endif // LEGEND2_CORE_APPLICATION_H
