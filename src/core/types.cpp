// ClawLite — 公共类型实现

#include "core/types.h"
#include <chrono>

namespace clawlite {

Message Message::system(const std::string& content) {
    Message m;
    m.role = Role::System;
    m.content = content;
    m.timestamp = nowMs();
    return m;
}

Message Message::user(const std::string& content) {
    Message m;
    m.role = Role::User;
    m.content = content;
    m.timestamp = nowMs();
    return m;
}

Message Message::assistant(const std::string& content) {
    Message m;
    m.role = Role::Assistant;
    m.content = content;
    m.timestamp = nowMs();
    return m;
}

Message Message::toolResult(const std::string& toolCallId,
                            const std::string& toolName,
                            const std::string& content) {
    Message m;
    m.role = Role::Tool;
    m.content = content;
    m.toolCallId = toolCallId;
    m.name = toolName;
    m.timestamp = nowMs();
    return m;
}

int estimateTokens(const std::string& text) {
    // 简单估算：ASCII 字符 /4，CJK 字符 ≈ 1 token/字
    int tokens = 0;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            // ASCII
            ++tokens;
            ++i;
        } else if (c < 0xE0) {
            // 2-byte UTF-8 (CJK 等)
            tokens += 1;
            i += 2;
        } else if (c < 0xF0) {
            // 3-byte UTF-8 (CJK)
            tokens += 1;
            i += 3;
        } else {
            // 4-byte UTF-8
            tokens += 1;
            i += 4;
        }
    }
    return tokens / 4 + 1;  // 粗略估算，保证不为 0
}

std::string roleToString(Role role) {
    switch (role) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "unknown";
}

Role stringToRole(const std::string& s) {
    if (s == "system")    return Role::System;
    if (s == "user")      return Role::User;
    if (s == "assistant") return Role::Assistant;
    if (s == "tool")      return Role::Tool;
    return Role::User;
}

int64_t nowMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
}

} // namespace clawlite
