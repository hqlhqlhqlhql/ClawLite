// ClawLite — 会话存储实现
// TODO: B 同学实现

#include "memory/session_store.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace clawlite {

// JSON 转义辅助（本地使用）
static std::string escapeJson(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else result += c;
    }
    return result;
}

static std::string unescapeJson(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == '"') { result += '"'; i++; }
            else if (next == '\\') { result += '\\'; i++; }
            else if (next == 'n') { result += '\n'; i++; }
            else if (next == 'r') { result += '\r'; i++; }
            else if (next == 't') { result += '\t'; i++; }
            else result += s[i];
        } else {
            result += s[i];
        }
    }
    return result;
}

// roleToString 已在 types.h 中定义，无需重复

void SessionStore::createSession(const std::string& sessionKey) {
    if (m_sessions.find(sessionKey) == m_sessions.end()) {
        SessionEntry entry;
        entry.sessionKey = sessionKey;
        entry.createdAt = nowMs();
        entry.updatedAt = nowMs();
        m_sessions[sessionKey] = std::move(entry);
    }
}

bool SessionStore::hasSession(const std::string& sessionKey) const {
    return m_sessions.find(sessionKey) != m_sessions.end();
}

SessionEntry* SessionStore::getSession(const std::string& sessionKey) {
    auto it = m_sessions.find(sessionKey);
    if (it != m_sessions.end()) return &it->second;
    return nullptr;
}

void SessionStore::appendMessage(const std::string& sessionKey, const Message& msg) {
    // 自动创建会话
    if (!hasSession(sessionKey)) {
        createSession(sessionKey);
    }
    m_sessions[sessionKey].messages.push_back(msg);
    m_sessions[sessionKey].updatedAt = nowMs();
}

std::vector<Message> SessionStore::getHistory(const std::string& sessionKey, int maxTurns) const {
    // TODO: 实现 — 返回最近 N 轮对话
    // 每轮 = 1 个 user 消息 + 1 个 assistant 消息
    // 从后往前数 maxTurns 轮
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end()) return {};

    const auto& all = it->second.messages;
    int turnCount = 0;
    int startIdx = 0;  // 默认返回全部

    for (int i = (int)all.size() - 1; i >= 0; i--) {
        if (all[i].role == Role::User) {
            turnCount++;
            if (turnCount >= maxTurns) {
                startIdx = i;
                break;
            }
        }
    }

    return std::vector<Message>(all.begin() + startIdx, all.end());
}

std::vector<Message> SessionStore::getAllMessages(const std::string& sessionKey) const {
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end()) return {};
    return it->second.messages;
}

size_t SessionStore::sessionCount() const {
    return m_sessions.size();
}

std::vector<std::string> SessionStore::sessionKeys() const {
    std::vector<std::string> keys;
    keys.reserve(m_sessions.size());
    for (const auto& [key, entry] : m_sessions) {
        keys.push_back(key);
    }
    return keys;
}

void SessionStore::clear() {
    m_sessions.clear();
}

// ── JSONL 持久化 ──────────────────────────────────────────

void SessionStore::save(const std::string& path) {
    // JSONL 格式：每行一个 JSON 对象
    // {"session_key":"...", "role":"...", "content":"...", "timestamp":...}
    // 每次覆盖写全量（避免 append-only 导致重复行）
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;

    for (const auto& [key, entry] : m_sessions) {
        for (const auto& msg : entry.messages) {
            out << "{\"session_key\":\"" << key
                << "\",\"role\":\"" << roleToString(msg.role)
                << "\",\"content\":\"" << escapeJson(msg.content)
                << "\",\"timestamp\":" << msg.timestamp
                << "}\n";
        }
    }
    out.close();
}

void SessionStore::load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        // 简单的 JSON 解析（手动解析，避免依赖）
        // 格式：{"session_key":"...", "role":"...", "content":"...", "timestamp":...}
        auto extractValue = [&line](const std::string& key) -> std::string {
            size_t pos = line.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = line.find(":", pos + key.size() + 2);
            if (pos == std::string::npos) return "";
            pos++;  // skip ':'

            while (pos < line.size() && line[pos] == ' ') pos++;
            if (pos >= line.size()) return "";

            if (line[pos] == '"') {
                // 字符串值：跳过转义引号 \"，找到真正的结束引号
                size_t start = pos + 1;
                size_t end = start;
                while (end < line.size()) {
                    if (line[end] == '\\' && end + 1 < line.size()) {
                        end += 2;  // 跳过转义字符
                    } else if (line[end] == '"') {
                        break;
                    } else {
                        end++;
                    }
                }
                return line.substr(start, end - start);
            }

            // 数值
            size_t end = line.find_first_of(",}", pos);
            if (end == std::string::npos) end = line.size();
            return line.substr(pos, end - pos);
        };

        std::string sessionKey = extractValue("session_key");
        std::string roleStr = extractValue("role");
        std::string content = extractValue("content");
        std::string timestampStr = extractValue("timestamp");

        if (sessionKey.empty() || roleStr.empty()) continue;

        // 解析 role
        Role role = Role::User;
        if (roleStr == "assistant") role = Role::Assistant;
        else if (roleStr == "system") role = Role::System;

        // 解析 timestamp
        int64_t timestamp = 0;
        try {
            timestamp = std::stoll(timestampStr);
        } catch (...) {
            timestamp = nowMs();
        }

        // 创建消息
        Message msg;
        msg.role = role;
        msg.content = unescapeJson(content);
        msg.timestamp = timestamp;

        // 追加到对应会话
        if (!hasSession(sessionKey)) {
            createSession(sessionKey);
        }
        m_sessions[sessionKey].messages.push_back(msg);
    }

    in.close();
}

} // namespace clawlite
