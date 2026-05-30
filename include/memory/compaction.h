#pragma once
// ClawLite — 上下文压缩
// 参考：openclaw-main/src/agents/pi-embedded-runner/compact.ts
// 算法：当对话 token 数超过 budget 时，截断旧对话，保留最近 N 轮 + 摘要
//
// 分层裁剪策略（不用优先级堆，用固定 heuristic）：
//   1. ensureToolPairing()              — 防孤儿
//   2. 截断超长 tool_result             — 每个 > 500 token 的截到 500
//   3. 重新计 token，仍超 → 丢最老轮    — recent-first
//   4. 仍超 → 走 LLMSummary 策略        — 调摘要
//   5. 状态重注入                       — 从 FileStateCache 取最近 3 个文件

#include "core/types.h"
#include <functional>
#include <vector>
#include <string>

namespace clawlite {

// 压缩策略
enum class CompactionStrategy {
    TruncateOnly,      // baseline：直接丢前 N 轮
    GreedySummary,     // 截断 + 字符串拼接摘要
    LLMSummary         // 调 LLM 生成结构化摘要
};

struct CompactionConfig {
    int targetTokens = 4000;         // 压缩目标 token 数
    int keepTurns = 6;               // 至少保留的最近对话轮数
    int summaryMaxLen = 200;         // 摘要最大字符数
    CompactionStrategy strategy = CompactionStrategy::GreedySummary;

    // LLMSummary 策略需要的摘要函数（由外部注入，避免 memory 模块依赖 llm 模块）
    // 输入：被移除的消息列表
    // 输出：结构化摘要文本
    std::function<std::string(const std::vector<Message>&)> summarizer;
};

class Compactor {
public:
    // 压缩消息列表
    // 参考：openclaw-main/src/agents/pi-embedded-runner/compact.ts
    static CompactResult compact(
        std::vector<Message>& messages,
        const CompactionConfig& config = {}
    );

    // Baseline for benchmarks: drop oldest messages until the token budget fits.
    static CompactResult truncateBaseline(
        std::vector<Message>& messages,
        int targetTokens
    );

    // 计算消息列表的总 token 数
    static int countTotalTokens(const std::vector<Message>& messages);

    // 检查 tool_use ↔ tool_result 配对，防止孤儿消息
    // 数据结构：HashMap 配对检查，O(n)
    static void ensureToolPairing(std::vector<Message>& messages);

private:
    // 生成简单摘要（取被移除消息的前 N 个字符）
    static std::string generateSummary(
        const std::vector<Message>& removed,
        int maxLen
    );
};

} // namespace clawlite
