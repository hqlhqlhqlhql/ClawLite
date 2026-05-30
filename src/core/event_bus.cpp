// ClawLite — 事件总线实现

#include "core/event_bus.h"

namespace clawlite {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

int EventBus::subscribe(EventType type, EventListener listener) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextId++;
    m_listeners.insert({id, type, std::move(listener)});
    return id;
}

void EventBus::unsubscribe(int listenerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        if (it->id == listenerId) {
            m_listeners.erase(it);  
            return;
        }
    }
}

void EventBus::emit(const Event& event) {
    std::vector<EventListener> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : m_listeners) {
            if (entry.type == event.type) {
                callbacks.push_back(entry.callback);
            }
        }
    }

    for (const auto& callback : callbacks) {
        if (callback) {
            callback(event);
        }
    }
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_listeners.clear();
    m_nextId = 0;
}

} // namespace clawlite
