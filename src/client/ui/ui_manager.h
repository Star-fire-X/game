// =============================================================================
// Legend2 UI管理器 (UI Manager)
//
// 功能说明:
//   - UI组件的集中管理
//   - 组件的注册、查找和生命周期管理
//   - UI层级和焦点管理
//   - 输入事件分发
// =============================================================================

#ifndef LEGEND2_UI_MANAGER_H
#define LEGEND2_UI_MANAGER_H

#include <SDL.h>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include "common/types.h"

namespace mir2::ui {

// 引入公共类型定义
using namespace mir2::common;

// 前向声明
class UIRenderer;

/// UI组件基类
/// 所有UI元素的基类，定义基本接口
class UIWidget {
public:
    virtual ~UIWidget() = default;

    /// 获取组件ID
    const std::string& get_id() const { return id_; }

    /// 设置组件ID
    void set_id(const std::string& id) { id_ = id; }

    /// 获取组件位置和大小
    const Rect& get_bounds() const { return bounds_; }
    void set_bounds(const Rect& bounds) { bounds_ = bounds; }

    /// 设置位置
    void set_position(int x, int y) { bounds_.x = x; bounds_.y = y; }

    /// 设置大小
    void set_size(int w, int h) { bounds_.width = w; bounds_.height = h; }

    /// 可见性
    bool is_visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }

    /// 启用状态
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }

    /// 焦点状态
    bool has_focus() const { return focused_; }
    void set_focus(bool focused) { focused_ = focused; }

    /// 检查点是否在组件内
    bool contains_point(int x, int y) const {
        return x >= bounds_.x && x < bounds_.x + bounds_.width &&
               y >= bounds_.y && y < bounds_.y + bounds_.height;
    }

    /// 更新组件
    virtual void update(float delta_time) { (void)delta_time; }

    /// 渲染组件
    virtual void render(UIRenderer& renderer) = 0;

    /// 处理事件
    /// @return true 如果事件被处理
    virtual bool handle_event(const SDL_Event& event) { (void)event; return false; }

protected:
    std::string id_;
    Rect bounds_ = {0, 0, 0, 0};
    bool visible_ = true;
    bool enabled_ = true;
    bool focused_ = false;
};

/// UI容器
/// 可以包含子组件的组件
class UIContainer : public UIWidget {
public:
    /// 添加子组件
    void add_child(std::shared_ptr<UIWidget> widget) {
        children_.push_back(widget);
    }

    /// 移除子组件
    void remove_child(const std::string& id) {
        children_.erase(
            std::remove_if(children_.begin(), children_.end(),
                [&id](const auto& w) { return w->get_id() == id; }),
            children_.end());
    }

    /// 查找子组件
    UIWidget* find_child(const std::string& id) {
        for (auto& child : children_) {
            if (child->get_id() == id) return child.get();
        }
        return nullptr;
    }

    /// 获取所有子组件
    const std::vector<std::shared_ptr<UIWidget>>& get_children() const {
        return children_;
    }

    void update(float delta_time) override {
        for (auto& child : children_) {
            if (child->is_visible() && child->is_enabled()) {
                child->update(delta_time);
            }
        }
    }

    void render(UIRenderer& renderer) override {
        for (auto& child : children_) {
            if (child->is_visible()) {
                child->render(renderer);
            }
        }
    }

    bool handle_event(const SDL_Event& event) override {
        // 逆序处理事件（顶层优先）
        for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
            if ((*it)->is_visible() && (*it)->is_enabled()) {
                if ((*it)->handle_event(event)) {
                    return true;
                }
            }
        }
        return false;
    }

protected:
    std::vector<std::shared_ptr<UIWidget>> children_;
};

/// UI管理器
/// 管理整个UI系统
class UIManager {
public:
    UIManager() = default;
    ~UIManager() = default;

    // 禁止拷贝
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    /// 注册屏幕/面板
    void register_screen(const std::string& name, std::shared_ptr<UIContainer> screen) {
        screens_[name] = screen;
    }

    /// 获取屏幕
    UIContainer* get_screen(const std::string& name) {
        auto it = screens_.find(name);
        return it != screens_.end() ? it->second.get() : nullptr;
    }

    /// 设置活动屏幕
    void set_active_screen(const std::string& name) {
        active_screen_name_ = name;
    }

    /// 获取活动屏幕
    UIContainer* get_active_screen() {
        return get_screen(active_screen_name_);
    }

    /// 更新UI
    void update(float delta_time) {
        if (auto* screen = get_active_screen()) {
            screen->update(delta_time);
        }
    }

    /// 渲染UI
    void render(UIRenderer& renderer) {
        if (auto* screen = get_active_screen()) {
            screen->render(renderer);
        }
    }

    /// 处理事件
    bool handle_event(const SDL_Event& event) {
        if (auto* screen = get_active_screen()) {
            return screen->handle_event(event);
        }
        return false;
    }

    /// 设置焦点组件
    void set_focus(UIWidget* widget) {
        if (focused_widget_ == widget) return;
        if (focused_widget_) {
            focused_widget_->set_focus(false);
        }
        focused_widget_ = widget;
        if (focused_widget_) {
            focused_widget_->set_focus(true);
        }
    }

    /// 获取焦点组件
    UIWidget* get_focused_widget() { return focused_widget_; }

private:
    std::unordered_map<std::string, std::shared_ptr<UIContainer>> screens_;
    std::string active_screen_name_;
    UIWidget* focused_widget_ = nullptr;
};

} // namespace mir2::ui

#endif // LEGEND2_UI_MANAGER_H
