#pragma once

#include <SDL.h>
#include <algorithm>
#include <functional>
#include <vector>
#include "common/types.h"

namespace mir2::core {

// 事件监听器接口
class IEventListener {
public:
    virtual ~IEventListener() = default;

    // 是否当前接受事件（由监听器自己决定，可基于状态判断）
    virtual bool is_active() const { return true; }

    // 事件处理方法（返回 true 表示事件已消费，不再传递）
    virtual bool on_mouse_move(int, int) { return false; }
    virtual bool on_mouse_button_down(int, int, int) { return false; }
    virtual bool on_mouse_button_up(int, int, int) { return false; }
    virtual bool on_mouse_wheel(int, int) { return false; }
    virtual bool on_key_down(SDL_Scancode, SDL_Keycode, bool) { return false; }
    virtual bool on_key_up(SDL_Scancode, SDL_Keycode) { return false; }
    virtual bool on_text_input(const char*) { return false; }
    virtual bool on_user_event(const SDL_UserEvent&) { return false; }
};

// 事件分发器
class EventDispatcher {
public:
    // 添加监听器（优先级越高越先处理，默认 0）
    void add_listener(IEventListener* listener, int priority = 0);

    // 移除监听器
    void remove_listener(IEventListener* listener);

    // 分发 SDL 事件
    void dispatch(const SDL_Event& event);

    // 清空所有监听器
    void clear();

private:
    struct ListenerEntry {
        IEventListener* listener;
        int priority;

        bool operator<(const ListenerEntry& other) const {
            return priority > other.priority;  // 高优先级在前
        }
    };

    std::vector<ListenerEntry> listeners_;
    bool needs_sort_ = false;

    void ensure_sorted();
};

} // namespace mir2::core
