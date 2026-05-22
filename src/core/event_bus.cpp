// ClawLite — 事件总线实现

#include "core/event_bus.h"

namespace clawlite {

EventBus& EventBus::instance() {
    static EventBus bus;
    return bus;
}

int EventBus::subscribe(EventType type, EventListener listener) {
    int id = m_nextId++;
    m_listeners.insert({id, type, std::move(listener)});
    return id;
}

void EventBus::unsubscribe(int listenerId) {
    for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
        if (it->id == listenerId) {
            m_listeners.erase(it);
            return;
        }
    }
}

void EventBus::emit(const Event& event) {
    for (const auto& entry : m_listeners) {
        if (entry.type == event.type) {
            entry.callback(event);
        }
    }
}

void EventBus::clear() {
    m_listeners.clear();
    m_nextId = 0;
}

} // namespace clawlite
