#pragma once
// ClawLite — 技能注册表
// 参考：openclaw-main/src/agents/skills/workspace.ts:resolveWorkspaceSkillPromptState
// 数据结构：unordered_map<string, SkillEntry> 按 name 索引
// 算法：排序 + 二分搜索裁剪（字符预算内尽可能多的技能）
// 额外：Trie 前缀补全 + 文件变更轮询监听

#include "skill/skill_loader.h"
#include "skill/skill_filter.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace clawlite {

class SkillRegistry {
public:
    SkillRegistry() = default;
    ~SkillRegistry();

    // 加载并注册技能（调用 SkillLoader::loadAndMerge），可选过滤
    // 新重载：传入 filterConfig，会在合并后过滤掉不适用的技能
    void loadFromWorkspace(const std::string& workspaceDir,
                           const SkillFilterConfig& filterConfig);

    // 保持兼容的原有接口（无过滤）
    void loadFromWorkspace(const std::string& workspaceDir);

    // 手动注册一个技能
    void registerSkill(const SkillEntry& entry);

    // 获取所有活跃技能
    std::vector<SkillEntry> getActiveSkills() const;

    // 按 name 查找技能
    const SkillEntry* findSkill(const std::string& name) const;

    // Trie 前缀补全：用于 `/skills <prefix>`
    std::vector<std::string> completeSkillNames(const std::string& prefix, size_t limit = 10) const;

    // 构建技能提示词（XML 格式，带字符预算裁剪）
    // 参考：openclaw-main/src/agents/skills/workspace.ts:resolveWorkspaceSkillPromptState
    std::string buildSkillPrompt(int charBudget = 18000) const;

    // 获取技能数量
    size_t size() const;

    // 清空
    void clear();

    // 启用/关闭 workspace 轮询监听；检测到变更时自动通过 EventBus 广播 SkillChanged
    void enableAutoReload(std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    void disableAutoReload();

    // 手动刷新；如果内容发生变化则返回 true，并广播事件
    bool refreshFromWorkspace();

private:
    struct TrieNode {
        std::map<char, std::unique_ptr<TrieNode>> children;
        bool terminal = false;
        std::string word;
    };

    // 数据结构：HashMap 按 name 索引
    std::unordered_map<std::string, SkillEntry> m_skills;

    // Trie：前缀补全
    std::unique_ptr<TrieNode> m_trieRoot;

    // Workspace / watcher 状态
    std::string m_workspaceDir;
    SkillFilterConfig m_filterConfig;
    std::string m_lastFingerprint;
    std::chrono::milliseconds m_watchInterval{1000};
    std::thread m_watchThread;
    std::atomic<bool> m_watchRunning{false};

    mutable std::mutex m_mutex;

    void rebuildIndexesLocked();
    void insertTrieNameLocked(const std::string& name);
    std::vector<std::string> completeSkillNamesLocked(const std::string& prefix, size_t limit) const;
    static std::string makeFingerprint(const std::vector<SkillEntry>& skills);
    static std::string formatSkillXml(const SkillEntry& entry, bool compact = false);

    // 二分搜索：在排序后的技能列表中，找到不超过字符预算的最大前缀长度
    static int binarySearchPromptLimit(
        const std::vector<size_t>& prefixLengths,
        size_t wrapperLength,
        int charBudget
    );
};

} // namespace clawlite
