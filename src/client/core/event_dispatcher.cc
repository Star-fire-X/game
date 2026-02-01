#include "core/event_dispatcher.h"

namespace mir2::core {

void EventDispatcher::add_listener(IEventListener* listener, int priority) {
    if (!listener) {
        return;
    }
    listeners_.push_back({listener, priority});
    needs_sort_ = true;
}

void EventDispatcher::remove_listener(IEventListener* listener) {
    if (!listener) {
        return;
    }
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
                       [listener](const ListenerEntry& entry) {
                           return entry.listener == listener;
                       }),
        listeners_.end());
}

void EventDispatcher::clear() {
    listeners_.clear();
    needs_sort_ = false;
}

void EventDispatcher::ensure_sorted() {
    if (!needs_sort_) {
        return;
    }
    std::sort(listeners_.begin(), listeners_.end());
    needs_sort_ = false;
}

void EventDispatcher::dispatch(const SDL_Event& event) {
    ensure_sorted();

    for (const auto& entry : listeners_) {
        if (!entry.listener || !entry.listener->is_active()) {
            continue;
        }

        bool consumed = false;
        switch (event.type) {
            case SDL_MOUSEMOTION:
                consumed = entry.listener->on_mouse_move(event.motion.x, event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                consumed = entry.listener->on_mouse_button_down(event.button.button,
                                                                event.button.x,
                                                                event.button.y);
                break;
            case SDL_MOUSEBUTTONUP:
                consumed = entry.listener->on_mouse_button_up(event.button.button,
                                                              event.button.x,
                                                              event.button.y);
                break;
            case SDL_MOUSEWHEEL:
                consumed = entry.listener->on_mouse_wheel(event.wheel.x, event.wheel.y);
                break;
            case SDL_KEYDOWN:
                consumed = entry.listener->on_key_down(event.key.keysym.scancode,
                                                       event.key.keysym.sym,
                                                       event.key.repeat != 0);
                break;
            case SDL_KEYUP:
                consumed = entry.listener->on_key_up(event.key.keysym.scancode,
                                                     event.key.keysym.sym);
                break;
            case SDL_TEXTINPUT:
                consumed = entry.listener->on_text_input(event.text.text);
                break;
            case SDL_USEREVENT:
                consumed = entry.listener->on_user_event(event.user);
                break;
            default:
                break;
        }

        if (consumed) {
            break;
        }
    }
}

} // namespace mir2::core
