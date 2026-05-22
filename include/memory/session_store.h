#pragma once
// ClawLite — 会话存储
// 参考：openclaw-main/src/sessions/session-key-utils.ts — 层级会话键解析
//       openclaw-main/src/sessions/ — 会话生命周期管理
// 数据结构：
//   - unordered_map<string, SessionEntry> 按 sessionKey 索引
//   - 层级键解析：agent:id:channel:peer → 提取各维度信息

#include "core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

namespace clawlite {

// 会话键解析结果
// 参考：openclaw-main/src/sessions/session-key-utils.ts:parseAgentSessionKey
struct SessionKeyInfo {
    std::string agentId;
    std::string channel;
    std::string peerKind;    // "user", "group", "channel"
    std::string peerId;
    std::string threadId;    // 可选
    std::string rawKey;
};

class SessionStore {
public:
    SessionStore() = default;

    // ── 会话管理 ──────────────────────────────────────────

    // 创建会话
    void createSession(const std::string& sessionKey);

    // 检查会话是否存在
    bool hasSession(const std::string& sessionKey) const;

    // 获取会话
    SessionEntry* getSession(const std::string& sessionKey);

    // ── 消息管理 ──────────────────────────────────────────

    // 向会话追加消息
    void appendMessage(const std::string& sessionKey, const Message& msg);

    // 获取会话历史（最近 N 轮）
    // 每轮 = 1 个 user 消息 + 1 个 assistant 消息
    std::vector<Message> getHistory(const std::string& sessionKey, int maxTurns = 20) const;

    // 获取所有消息
    std::vector<Message> getAllMessages(const std::string& sessionKey) const;

    // 获取会话数量
    size_t sessionCount() const;

    // 清空所有会话
    void clear();

    // ── 会话键解析 ────────────────────────────────────────

    // 解析层级会话键
    // 格式：agent:<agentId>:<channel>:<peerKind>:<peerId>[:thread:<threadId>]
    // 参考：openclaw-main/src/sessions/session-key-utils.ts:parseAgentSessionKey
    static SessionKeyInfo parseSessionKey(const std::string& key);

    // 构建会话键
    static std::string buildSessionKey(
        const std::string& agentId,
        const std::string& channel,
        const std::string& peerKind,
        const std::string& peerId
    );

    // 按 agent 查询会话列表
    std::vector<std::string> listSessionsByAgent(const std::string& agentId) const;

private:
    // 数据结构：HashMap 按 sessionKey 索引
    std::unordered_map<std::string, SessionEntry> m_sessions;
};

} // namespace clawlite
