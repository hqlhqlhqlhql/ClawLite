// ClawLite — 提示词构建器测试
// TODO: C 同学补充测试用例

#include "llm/prompt_builder.h"
#include "skill/skill_registry.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testBuildBasePrompt() {
    PromptBuildContext ctx;
    ctx.basePrompt = "You are a helpful assistant.";
    ctx.model = "gpt-4o-mini";
    ctx.os = "windows";
    ctx.workspaceDir = "/tmp";

    SkillRegistry skills;
    auto prompt = PromptBuilder::buildSystemPrompt(ctx, skills);

    TEST_ASSERT(!prompt.empty());
    TEST_ASSERT(prompt.find("helpful assistant") != std::string::npos);
    std::cout << "  [PASS] testBuildBasePrompt\n";
}

void testBuildWithSkills() {
    PromptBuildContext ctx;
    ctx.basePrompt = "You are ClawLite.";
    ctx.model = "gpt-4o-mini";
    ctx.os = "linux";
    ctx.workspaceDir = "/home/user/project";

    SkillRegistry skills;
    SkillEntry entry;
    entry.definition.name = "weather";
    entry.definition.description = "Get weather info";
    entry.definition.filePath = "skills/weather/SKILL.md";
    skills.registerSkill(entry);

    auto prompt = PromptBuilder::buildSystemPrompt(ctx, skills);
    TEST_ASSERT(prompt.find("weather") != std::string::npos);
    std::cout << "  [PASS] testBuildWithSkills\n";
}

void testBuildToolsPrompt() {
    std::vector<Tool> tools;
    Tool t1;
    t1.name = "calculator";
    t1.description = "Calculate math";
    tools.push_back(t1);

    auto prompt = PromptBuilder::buildToolsPrompt(tools);
    TEST_ASSERT(prompt.find("calculator") != std::string::npos);
    TEST_ASSERT(prompt.find("Calculate math") != std::string::npos);
    std::cout << "  [PASS] testBuildToolsPrompt\n";
}

int run_prompt_builder_tests() {
    std::cout << "Prompt Builder Tests:\n";
    RESET_FAILURES();
    testBuildBasePrompt();
    testBuildWithSkills();
    testBuildToolsPrompt();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All prompt builder tests passed.\n";
    else std::cout << f << " prompt builder test(s) failed.\n";
    return f;
}
