// ClawLite — Compaction 测试
// 测试三策略的压缩效果、ensureToolPairing() 防孤儿、状态重注入

#include "memory/compaction.h"
#include "memory/file_state_cache.h"
#include "test_helpers.h"
#include <iostream>
#include <vector>
#include <string>

using namespace clawlite;

// 辅助函数：创建测试消息
static Message makeUserMsg(const std::string& content) {
    Message msg;
    msg.role = Role::User;
    msg.content = content;
    msg.timestamp = 1000;
    return msg;
}

static Message makeAssistantMsg(const std::string& content) {
    Message msg;
    msg.role = Role::Assistant;
    msg.content = content;
    msg.timestamp = 2000;
    return msg;
}

static Message makeToolMsg(const std::string& content, const std::string& toolCallId) {
    Message msg;
    msg.role = Role::Tool;
    msg.content = content;
    msg.toolCallId = toolCallId;
    msg.timestamp = 3000;
    return msg;
}

static Message makeAssistantToolCallMsg(const std::string& toolCallId, const std::string& toolName) {
    Message msg;
    msg.role = Role::Assistant;
    msg.content = "Calling tool " + toolName;
    msg.timestamp = 2000;
    // 模拟 toolCalls
    ToolCall tc;
    tc.id = toolCallId;
    tc.name = toolName;
    tc.arguments = "{}";
    msg.toolCalls.push_back(tc);
    return msg;
}

// 测试 ensureToolPairing() 防孤儿
void testEnsureToolPairing() {
    std::cout << "  Testing ensureToolPairing()...\n";

    std::vector<Message> messages;

    // 正常配对：tool_use + tool_result
    messages.push_back(makeUserMsg("Hello"));
    messages.push_back(makeAssistantToolCallMsg("call_1", "read_file"));
    messages.push_back(makeToolMsg("file content", "call_1"));
    messages.push_back(makeUserMsg("Thanks"));

    // 孤儿 tool_result（没有对应的 tool_use）
    messages.push_back(makeToolMsg("orphan result", "call_2"));

    // 另一个正常配对
    messages.push_back(makeAssistantToolCallMsg("call_3", "write_file"));
    messages.push_back(makeToolMsg("written", "call_3"));

    size_t originalSize = messages.size();
    Compactor::ensureToolPairing(messages);

    // 应该移除 1 个孤儿
    TEST_ASSERT(messages.size() == originalSize - 1);

    // 验证剩余的消息中没有 call_2 的 tool_result
    bool hasOrphan = false;
    for (const auto& msg : messages) {
        if (msg.role == Role::Tool && msg.toolCallId == "call_2") {
            hasOrphan = true;
            break;
        }
    }
    TEST_ASSERT(!hasOrphan);

    TEST_PASS("testEnsureToolPairing");
}

// 测试 TruncateOnly 策略
void testTruncateOnly() {
    std::cout << "  Testing TruncateOnly strategy...\n";

    std::vector<Message> messages;
    // 生成足够长的消息让 token 数超过 target
    for (int i = 0; i < 20; i++) {
        std::string userContent = "User message " + std::to_string(i) + ": " + std::string(200, 'u');
        std::string assistantContent = "Assistant response " + std::to_string(i) + ": " + std::string(200, 'a');
        messages.push_back(makeUserMsg(userContent));
        messages.push_back(makeAssistantMsg(assistantContent));
    }

    // 计算总 token
    int totalTokens = 0;
    for (const auto& msg : messages) {
        totalTokens += estimateTokens(msg.content);
    }

    CompactionConfig config;
    config.targetTokens = totalTokens / 2;  // 设置为目标是当前的一半
    config.keepTurns = 5;
    config.strategy = CompactionStrategy::TruncateOnly;

    auto result = Compactor::compact(messages, config);

    TEST_ASSERT(result.compacted);

    // 应该只保留最近 5 轮
    int userCount = 0;
    for (const auto& msg : messages) {
        if (msg.role == Role::User) userCount++;
    }
    TEST_ASSERT(userCount == 5);

    TEST_PASS("testTruncateOnly");
}

// 测试 GreedySummary 策略
void testGreedySummary() {
    std::cout << "  Testing GreedySummary strategy...\n";

    std::vector<Message> messages;
    // 生成足够长的消息让 token 数超过 target
    for (int i = 0; i < 20; i++) {
        std::string userContent = "User message " + std::to_string(i) + " with some content: " + std::string(200, 'x');
        std::string assistantContent = "Assistant response " + std::to_string(i) + " with detailed explanation: " + std::string(200, 'y');
        messages.push_back(makeUserMsg(userContent));
        messages.push_back(makeAssistantMsg(assistantContent));
    }

    // 计算总 token
    int totalTokens = 0;
    for (const auto& msg : messages) {
        totalTokens += estimateTokens(msg.content);
    }

    CompactionConfig config;
    config.targetTokens = totalTokens / 2;  // 设置为目标是当前的一半
    config.keepTurns = 5;
    config.summaryMaxLen = 100;
    config.strategy = CompactionStrategy::GreedySummary;

    auto result = Compactor::compact(messages, config);

    TEST_ASSERT(result.compacted);

    // 应该有系统消息的摘要
    bool hasSummary = false;
    for (const auto& msg : messages) {
        if (msg.role == Role::System && msg.content.find("[Summary") != std::string::npos) {
            hasSummary = true;
            break;
        }
    }
    TEST_ASSERT(hasSummary);

    TEST_PASS("testGreedySummary");
}

// 测试 LLMSummary 策略（带注入的摘要器）
void testLLMSummary() {
    std::cout << "  Testing LLMSummary strategy...\n";

    std::vector<Message> messages;
    // 生成足够长的消息让 token 数超过 target
    for (int i = 0; i < 20; i++) {
        std::string userContent = "User message " + std::to_string(i) + ": " + std::string(200, 'u');
        std::string assistantContent = "Assistant response " + std::to_string(i) + ": " + std::string(200, 'a');
        messages.push_back(makeUserMsg(userContent));
        messages.push_back(makeAssistantMsg(assistantContent));
    }

    // 计算总 token
    int totalTokens = 0;
    for (const auto& msg : messages) {
        totalTokens += estimateTokens(msg.content);
    }

    CompactionConfig config;
    config.targetTokens = totalTokens / 2;  // 设置为目标是当前的一半
    config.keepTurns = 5;
    config.strategy = CompactionStrategy::LLMSummary;

    // 注入模拟的 LLM 摘要器
    config.summarizer = [](const std::vector<Message>& msgs) -> std::string {
        return "LLM generated summary of " + std::to_string(msgs.size()) + " messages";
    };

    auto result = Compactor::compact(messages, config);

    TEST_ASSERT(result.compacted);

    // 验证摘要内容
    bool hasLLMSummary = false;
    for (const auto& msg : messages) {
        if (msg.role == Role::System && msg.content.find("LLM generated summary") != std::string::npos) {
            hasLLMSummary = true;
            break;
        }
    }
    TEST_ASSERT(hasLLMSummary);

    TEST_PASS("testLLMSummary");
}

// 测试 LLMSummary 策略（无摘要器时降级为 GreedySummary）
void testLLMSummaryFallback() {
    std::cout << "  Testing LLMSummary fallback to GreedySummary...\n";

    std::vector<Message> messages;
    // 生成足够长的消息让 token 数超过 target
    for (int i = 0; i < 20; i++) {
        std::string userContent = "User message " + std::to_string(i) + ": " + std::string(200, 'u');
        std::string assistantContent = "Assistant response " + std::to_string(i) + ": " + std::string(200, 'a');
        messages.push_back(makeUserMsg(userContent));
        messages.push_back(makeAssistantMsg(assistantContent));
    }

    // 计算总 token
    int totalTokens = 0;
    for (const auto& msg : messages) {
        totalTokens += estimateTokens(msg.content);
    }

    CompactionConfig config;
    config.targetTokens = totalTokens / 2;  // 设置为目标是当前的一半
    config.keepTurns = 5;
    config.strategy = CompactionStrategy::LLMSummary;
    // 不设置 summarizer，应该降级为 GreedySummary

    auto result = Compactor::compact(messages, config);

    TEST_ASSERT(result.compacted);

    // 验证有摘要（来自 GreedySummary 的字符串拼接）
    bool hasSummary = false;
    for (const auto& msg : messages) {
        if (msg.role == Role::System && msg.content.find("[Summary") != std::string::npos) {
            hasSummary = true;
            break;
        }
    }
    TEST_ASSERT(hasSummary);

    TEST_PASS("testLLMSummaryFallback");
}

// 测试截断超长 tool_result
void testTruncateToolResult() {
    std::cout << "  Testing tool_result truncation...\n";

    std::vector<Message> messages;
    messages.push_back(makeUserMsg("Hello"));

    // 创建一个超长的 tool_result（超过 500 token）
    std::string longContent(3000, 'A');  // 3000 字符，约 750 token
    messages.push_back(makeAssistantToolCallMsg("call_1", "read_file"));
    messages.push_back(makeToolMsg(longContent, "call_1"));

    CompactionConfig config;
    config.targetTokens = 5000;  // 不触发压缩，只测试截断
    config.keepTurns = 6;

    auto result = Compactor::compact(messages, config);

    // 验证 tool_result 被截断
    for (const auto& msg : messages) {
        if (msg.role == Role::Tool && msg.toolCallId == "call_1") {
            TEST_ASSERT(msg.content.size() < 3000);
            TEST_ASSERT(msg.content.find("...[truncated]...") != std::string::npos);
            break;
        }
    }

    TEST_PASS("testTruncateToolResult");
}

// 测试无压缩情况
void testNoCompactionNeeded() {
    std::cout << "  Testing no compaction needed...\n";

    std::vector<Message> messages;
    messages.push_back(makeUserMsg("Short message"));
    messages.push_back(makeAssistantMsg("Short response"));

    CompactionConfig config;
    config.targetTokens = 10000;  // 很大的 budget
    config.keepTurns = 6;

    auto result = Compactor::compact(messages, config);

    TEST_ASSERT(result.ok);  // 操作成功
    TEST_ASSERT(!result.compacted);  // 未触发压缩

    TEST_PASS("testNoCompactionNeeded");
}

int run_compaction_tests() {
    std::cout << "Compaction Tests:\n";
    RESET_FAILURES();
    testEnsureToolPairing();
    testTruncateOnly();
    testGreedySummary();
    testLLMSummary();
    testLLMSummaryFallback();
    testTruncateToolResult();
    testNoCompactionNeeded();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All compaction tests passed.\n";
    else std::cout << f << " compaction test(s) failed.\n";
    return f;
}
