// ClawLite — 上下文压缩实现
// 分层裁剪策略：
//   1. ensureToolPairing()              — 防孤儿
//   2. 截断超长 tool_result             — 每个 > 500 token 的截到 500
//   3. 重新计 token，仍超 → 丢最老轮    — recent-first
//   4. 仍超 → 走 LLMSummary 策略        — 调摘要

#include "memory/compaction.h"
#include "core/types.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace clawlite {

void Compactor::ensureToolPairing(std::vector<Message>& messages) {
    // 第一遍：找所有 tool_use（assistant 消息中的 toolCalls）
    std::unordered_set<std::string> useIds;
    for (size_t i = 0; i < messages.size(); i++) {
        if (messages[i].role == Role::Assistant) {
            for (const auto& tc : messages[i].toolCalls) {
                if (!tc.id.empty()) {
                    useIds.insert(tc.id);
                }
            }
        }
    }

    // 第二遍：找孤儿 tool_result（Role::Tool 但没有对应 tool_use）
    std::unordered_set<std::string> orphanIds;
    for (size_t i = 0; i < messages.size(); i++) {
        if (messages[i].role == Role::Tool && !messages[i].toolCallId.empty()) {
            if (useIds.find(messages[i].toolCallId) == useIds.end()) {
                orphanIds.insert(messages[i].toolCallId);
            }
        }
    }

    // 第三遍：移除孤儿 tool_result
    if (!orphanIds.empty()) {
        std::vector<Message> cleaned;
        cleaned.reserve(messages.size());
        for (auto& msg : messages) {
            if (msg.role == Role::Tool && orphanIds.count(msg.toolCallId)) {
                continue;  // 跳过孤儿
            }
            cleaned.push_back(std::move(msg));
        }
        messages = std::move(cleaned);
    }
}

CompactResult Compactor::compact(
    std::vector<Message>& messages,
    const CompactionConfig& config
) {
    // Step 1: 配对保护
    ensureToolPairing(messages);

    // Step 2: 截断超长 tool_result（> 500 token 的截到 500）
    const int TOOL_RESULT_TOKEN_LIMIT = 500;
    for (auto& msg : messages) {
        if (msg.role == Role::Tool && !msg.toolCallId.empty()) {
            int estimatedTokens = estimateTokens(msg.content);
            if (estimatedTokens > TOOL_RESULT_TOKEN_LIMIT) {
                // 截断：保留首尾，中间用省略号
                int headChars = TOOL_RESULT_TOKEN_LIMIT * 3 / 4;  // ~375 token
                int tailChars = TOOL_RESULT_TOKEN_LIMIT * 1 / 4;  // ~125 token
                std::string head = msg.content.substr(0, headChars);
                std::string tail = msg.content.substr(msg.content.size() - tailChars);
                msg.content = head + "\n...[truncated]...\n" + tail;
            }
        }
    }

    // Step 3: 重新计 token
    int totalTokens = countTotalTokens(messages);
    if (totalTokens <= config.targetTokens) {
        return {true, false, "no compaction needed", "", totalTokens, totalTokens};
    }

    // Step 4: 按策略压缩
    if (config.strategy == CompactionStrategy::TruncateOnly) {
        // baseline：直接丢前 N 轮
        int keepFrom = (int)messages.size();
        int turnCount = 0;
        for (int i = (int)messages.size() - 1; i >= 0; i--) {
            if (messages[i].role == Role::User) {
                turnCount++;
                if (turnCount >= config.keepTurns) {
                    keepFrom = i;
                    break;
                }
            }
        }

        if (keepFrom <= 0) {
            return {true, false, "nothing to compact", "", totalTokens, totalTokens};
        }

        std::vector<Message> compacted(messages.begin() + keepFrom, messages.end());
        int afterTokens = countTotalTokens(compacted);
        messages = std::move(compacted);
        return {true, true, "truncated", "", totalTokens, afterTokens};
    }

    if (config.strategy == CompactionStrategy::GreedySummary) {
        // 截断 + 字符串拼接摘要
        int keepFrom = (int)messages.size();
        int turnCount = 0;
        for (int i = (int)messages.size() - 1; i >= 0; i--) {
            if (messages[i].role == Role::User) {
                turnCount++;
                if (turnCount >= config.keepTurns) {
                    keepFrom = i;
                    break;
                }
            }
        }

        if (keepFrom <= 0) {
            return {true, false, "nothing to compact", "", totalTokens, totalTokens};
        }

        std::vector<Message> removed(messages.begin(), messages.begin() + keepFrom);
        std::string summary = generateSummary(removed, config.summaryMaxLen);

        std::vector<Message> compacted;
        compacted.push_back(Message::system("[Summary of earlier conversation]: " + summary));
        compacted.insert(compacted.end(), messages.begin() + keepFrom, messages.end());

        int afterTokens = countTotalTokens(compacted);
        messages = std::move(compacted);
        return {true, true, "compacted", summary, totalTokens, afterTokens};
    }

    if (config.strategy == CompactionStrategy::LLMSummary) {
        // 调 LLM 生成结构化摘要
        if (!config.summarizer) {
            // 没有摘要器，降级为 GreedySummary
            CompactionConfig fallback = config;
            fallback.strategy = CompactionStrategy::GreedySummary;
            return compact(messages, fallback);
        }

        int keepFrom = (int)messages.size();
        int turnCount = 0;
        for (int i = (int)messages.size() - 1; i >= 0; i--) {
            if (messages[i].role == Role::User) {
                turnCount++;
                if (turnCount >= config.keepTurns) {
                    keepFrom = i;
                    break;
                }
            }
        }

        if (keepFrom <= 0) {
            return {true, false, "nothing to compact", "", totalTokens, totalTokens};
        }

        std::vector<Message> removed(messages.begin(), messages.begin() + keepFrom);
        std::string summary = config.summarizer(removed);

        std::vector<Message> compacted;
        compacted.push_back(Message::system("[Summary of earlier conversation]: " + summary));
        compacted.insert(compacted.end(), messages.begin() + keepFrom, messages.end());

        int afterTokens = countTotalTokens(compacted);
        messages = std::move(compacted);
        return {true, true, "compacted_llm", summary, totalTokens, afterTokens};
    }

    return {true, false, "unknown strategy", "", totalTokens, totalTokens};
}

CompactResult Compactor::truncateBaseline(
    std::vector<Message>& messages,
    int targetTokens
) {
    int before = countTotalTokens(messages);
    if (before <= targetTokens) {
        return {true, false, "baseline no truncation needed", "", before, before};
    }

    while (!messages.empty() && countTotalTokens(messages) > targetTokens) {
        messages.erase(messages.begin());
    }

    int after = countTotalTokens(messages);
    return {true, true, "baseline truncated oldest messages", "", before, after};
}

int Compactor::countTotalTokens(const std::vector<Message>& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        total += estimateTokens(msg.content);
    }
    return total;
}

std::string Compactor::generateSummary(const std::vector<Message>& removed, int maxLen) {
    // 简单摘要：拼接被移除消息的前 maxLen 个字符
    std::string summary;
    for (const auto& msg : removed) {
        if (msg.role == Role::User || msg.role == Role::Assistant) {
            if (!summary.empty()) summary += " | ";
            summary += msg.content;
        }
    }
    if ((int)summary.size() > maxLen) {
        summary = summary.substr(0, maxLen) + "...";
    }
    return summary;
}

} // namespace clawlite
