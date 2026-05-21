#pragma once
// ClawLite — 系统提示词构建器
// 参考：openclaw-main/src/agents/pi-embedded-runner/system-prompt.ts:buildEmbeddedSystemPrompt
// 数据结构：vector<string> 存储各段落，最后 join 拼接
//
// 系统提示词由多个来源拼接而成：
//   1. 基础 prompt（身份描述）
//   2. 技能列表（来自 SkillRegistry.buildSkillPrompt()）
//   3. 工具列表
//   4. 内存上下文（来自 ContextEngine.assemble()）
//   5. 运行时信息（OS、时间、模型名等）

#include "skill/skill_registry.h"
#include "memory/context_engine.h"
#include <string>
#include <vector>

namespace clawlite {

struct PromptBuildContext {
    std::string basePrompt;              // 基础身份描述
    std::string workspaceDir;            // 工作区目录
    std::string model;                   // 当前模型名
    std::string os;                      // 操作系统
    int skillCharBudget = 18000;         // 技能列表字符预算
    int memoryTokenBudget = 2000;        // 内存上下文 token 预算
};

class PromptBuilder {
public:
    // 构建完整的系统提示词
    // 参考：openclaw-main/src/agents/pi-embedded-runner/system-prompt.ts:buildEmbeddedSystemPrompt
    // 算法：多来源段落拼接
    static std::string buildSystemPrompt(
        const PromptBuildContext& ctx,
        const SkillRegistry& skills,
        IContextEngine* memory = nullptr  // 可选
    );

    // 构建工具描述部分
    static std::string buildToolsPrompt(const std::vector<Tool>& tools);

private:
    // 各段落构建器
    static std::string buildBaseSection(const PromptBuildContext& ctx);
    static std::string buildSkillsSection(const SkillRegistry& skills, int charBudget);
    static std::string buildToolsSection(const std::vector<Tool>& tools);
    static std::string buildMemorySection(IContextEngine* memory, int tokenBudget);
    static std::string buildRuntimeSection(const PromptBuildContext& ctx);
};

} // namespace clawlite
