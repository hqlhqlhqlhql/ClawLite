// ClawLite — 会话存储实现
// TODO: B 同学实现

#include "memory/session_store.h"
#include <sstream>
#include <algorithm>

namespace clawlite {

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

void SessionStore::clear() {
    m_sessions.clear();
}

SessionKeyInfo SessionStore::parseSessionKey(const std::string& key) {
    // TODO: 实现层级键解析
    // 格式：agent:<agentId>:<channel>:<peerKind>:<peerId>[:thread:<threadId>]
    //
    // 参考：openclaw-main/src/sessions/session-key-utils.ts:parseAgentSessionKey
    // 算法：按 ':' 分割，逐段提取
    //
    // 示例：
    //   "agent:main:cli:user:default"
    //   → { agentId: "main", channel: "cli", peerKind: "user", peerId: "default" }
    //
    //   "agent:main:discord:group:123:thread:456"
    //   → { agentId: "main", channel: "discord", peerKind: "group", peerId: "123", threadId: "456" }

    SessionKeyInfo info;
    info.rawKey = key;

    std::vector<std::string> parts;
    std::istringstream ss(key);
    std::string part;
    while (std::getline(ss, part, ':')) {
        parts.push_back(part);
    }

    if (parts.size() >= 1 && parts[0] == "agent") {
        if (parts.size() >= 2) info.agentId = parts[1];
        if (parts.size() >= 3) info.channel = parts[2];
        if (parts.size() >= 4) info.peerKind = parts[3];
        if (parts.size() >= 5) info.peerId = parts[4];
        for (size_t i = 5; i < parts.size() - 1; i++) {
            if (parts[i] == "thread") {
                info.threadId = parts[i + 1];
                break;
            }
        }
    }

    return info;
}

std::string SessionStore::buildSessionKey(
    const std::string& agentId,
    const std::string& channel,
    const std::string& peerKind,
    const std::string& peerId
) {
    return "agent:" + agentId + ":" + channel + ":" + peerKind + ":" + peerId;
}

std::vector<std::string> SessionStore::listSessionsByAgent(const std::string& agentId) const {
    std::vector<std::string> result;
    std::string prefix = "agent:" + agentId + ":";
    for (const auto& [key, entry] : m_sessions) {
        if (key.substr(0, prefix.size()) == prefix) {
            result.push_back(key);
        }
    }
    return result;
}

} // namespace clawlite
