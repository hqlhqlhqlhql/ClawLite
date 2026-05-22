#pragma once
// ClawLite — 工具注册与执行
// 参考：openclaw-main/src/agents/tools/common.ts — AnyAgentTool, result types
// 数据结构：unordered_map<string, Tool> 工具注册表
// 设计模式：工厂模式 — 工具通过 registerTool() 注册，通过 execute() 按名调用

#include "core/types.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace clawlite {

class ToolExecutor {
public:
    ToolExecutor() = default;

    // 注册工具
    void registerTool(const Tool& tool);

    // 注册内置工具
    void registerBuiltinTools();

    // 执行工具调用
    // 参考：openclaw-main/src/agents/tools/common.ts — tool execution
    // 算法：
    //   1. 从 m_tools 中查找 toolCall.name
    //   2. 如果找到，调用 handler(toolCall.arguments)
    //   3. 如果未找到，返回错误信息
    ToolResult execute(const ToolCall& toolCall);

    // 获取所有已注册工具（用于传给 LLM）
    std::vector<Tool> getAllTools() const;

    // 检查工具是否存在
    bool hasTool(const std::string& name) const;

    // 获取工具数量
    size_t size() const;

private:
    // 数据结构：HashMap 按 name 索引
    std::unordered_map<std::string, Tool> m_tools;
};

// ── 内置工具实现 ──────────────────────────────────────────

// 计算器工具
std::string toolCalculator(const std::string& argsJson);

// 时间工具
std::string toolCurrentTime(const std::string& argsJson);

// 文件读取工具（简化版）
std::string toolReadFile(const std::string& argsJson);

} // namespace clawlite
