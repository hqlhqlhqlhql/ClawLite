#pragma once
// ClawLite — 技能加载器
// 参考：openclaw-main/src/agents/skills/local-loader.ts:loadSkillsFromDirSafe
//       openclaw-main/src/agents/skills/workspace.ts:loadSkillEntries
// 数据结构：vector<CandidateSkillDir> 存储候选目录
//          Map 按 name 去重合并（优先级覆盖）

#include "skill/skill_parser.h"
#include <vector>
#include <string>

namespace clawlite {

// 技能来源（决定优先级，数值越大优先级越高）
enum class SkillSource {
    Builtin = 1,    // skills/ 内置
    User = 2,       // ~/.clawlite/skills/
    Project = 3     // ./skills/ 项目级
};

struct SkillEntry {
    SkillDefinition definition;
    SkillSource source;
    std::string sourceDir;  // 来源目录
};

class SkillLoader {
public:
    // 从单个目录加载所有技能
    // 扫描目录下的子目录，每个子目录找 SKILL.md
    // 参考：openclaw-main/src/agents/skills/local-loader.ts:loadSkillsFromDirSafe
    static std::vector<SkillEntry> loadFromDir(const std::string& dir, SkillSource source);

    // 从多个目录加载并合并（高优先级覆盖低优先级）
    // 参考：openclaw-main/src/agents/skills/workspace.ts:loadSkillEntries
    // 数据结构：用 unordered_map<string, SkillEntry> 按 name 去重
    static std::vector<SkillEntry> loadAndMerge(
        const std::vector<std::pair<std::string, SkillSource>>& dirs
    );

    // 获取默认搜索目录
    static std::vector<std::pair<std::string, SkillSource>> getDefaultDirs(
        const std::string& workspaceDir
    );

private:
    // 安全读取文件（防止路径穿越）
    // 参考：openclaw 使用 openRootFileSync 防止路径穿越
    static bool isSafePath(const std::string& path, const std::string& root);
};

} // namespace clawlite
