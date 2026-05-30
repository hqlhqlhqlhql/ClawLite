#pragma once
// ClawLite — 事件总线（观察者模式）
// 参考：openclaw-main/src/sessions/session-lifecycle-events.ts
// 数据结构：std::set 存储监听器，支持订阅/取消订阅/广播

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace clawlite {

// ── 事件类型 ──────────────────────────────────────────────

enum class EventType {
    SkillChanged,       // 技能列表变更
    SessionCreated,     // 新会话创建
    SessionUpdated,     // 会话内容更新
    MemoryIndexed,      // 记忆文件索引完成
    CompactionDone,     // 上下文压缩完成
    Custom              // 自定义事件
};

struct Event {
    EventType type;
    std::string key;       // 事件关联的键（如 session key、skill name）
    std::string data;      // 附加数据（JSON 字符串）
};

// ── 事件总线 ──────────────────────────────────────────────

using EventListener = std::function<void(const Event&)>;

class EventBus {
public:
    // 获取全局单例
    static EventBus& instance();

    // 订阅事件，返回 listener ID（用于取消订阅）
    int subscribe(EventType type, EventListener listener);

    // 取消订阅
    void unsubscribe(int listenerId);

    // 广播事件
    void emit(const Event& event);

    // 清除所有监听器
    void clear();

private:
    EventBus() = default;

    struct ListenerEntry {
        int id;
        EventType type;
        EventListener callback;
        // 用于 set 排序
        bool operator<(const ListenerEntry& other) const { return id < other.id; }
    };

    std::set<ListenerEntry> m_listeners;  // 数据结构：有序集合存储监听器
    std::mutex m_mutex;
    int m_nextId = 0;
};

} // namespace clawlite
