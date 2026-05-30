// ClawLite — 技能注册表实现

#include "skill/skill_registry.h"
#include "core/event_bus.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace clawlite {
namespace {

static constexpr const char* kPromptOpenTag = "<available_skills>\n";
static constexpr const char* kPromptCloseTag = "</available_skills>\n";

static std::string escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += ch; break;
        }
    }
    return out;
}

static std::string joinStrings(const std::vector<std::string>& items, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out += sep;
        out += items[i];
    }
    return out;
}

static std::vector<SkillEntry> loadWorkspaceEntries(
    const std::string& workspaceDir,
    const SkillFilterConfig& filterConfig
) {
    auto dirs = SkillLoader::getDefaultDirs(workspaceDir);
    auto loaded = SkillLoader::loadAndMerge(dirs);
    if (!filterConfig.os.empty()) {
        loaded = SkillFilter::filterSkills(loaded, filterConfig);
    }
    return loaded;
}

static std::string skillSummary(const SkillEntry& entry) {
    std::string s;
    s.reserve(entry.definition.name.size() + entry.definition.description.size() +
              entry.definition.body.size() + entry.definition.filePath.size() + 32);
    s += entry.definition.name;
    s += '\x1f';
    s += std::to_string(static_cast<int>(entry.source));
    s += '\x1f';
    s += entry.sourceDir;
    s += '\x1f';
    s += entry.definition.filePath;
    s += '\x1f';
    s += entry.definition.description;
    s += '\x1f';
    s += entry.definition.body;
    return s;
}

} // namespace

SkillRegistry::~SkillRegistry() {
    disableAutoReload();
}

void SkillRegistry::loadFromWorkspace(const std::string& workspaceDir,
                                      const SkillFilterConfig& filterConfig) {
    auto entries = loadWorkspaceEntries(workspaceDir, filterConfig);
    std::sort(entries.begin(), entries.end(), [](const SkillEntry& a, const SkillEntry& b) {
        return a.definition.name < b.definition.name;
    });

    const std::string fingerprint = makeFingerprint(entries);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_workspaceDir = workspaceDir;
    m_filterConfig = filterConfig;
    m_skills.clear();
    for (auto& entry : entries) {
        m_skills[entry.definition.name] = std::move(entry);
    }
    rebuildIndexesLocked();
    m_lastFingerprint = fingerprint;
}

void SkillRegistry::loadFromWorkspace(const std::string& workspaceDir) {
    loadFromWorkspace(workspaceDir, SkillFilterConfig{});
}

void SkillRegistry::registerSkill(const SkillEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (entry.definition.name.empty()) {
        return;
    }
    m_skills[entry.definition.name] = entry;
    rebuildIndexesLocked();

    std::vector<SkillEntry> snapshot;
    snapshot.reserve(m_skills.size());
    for (const auto& [name, item] : m_skills) {
        (void)name;
        snapshot.push_back(item);
    }
    m_lastFingerprint = makeFingerprint(snapshot);
}

std::vector<SkillEntry> SkillRegistry::getActiveSkills() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SkillEntry> result;
    result.reserve(m_skills.size());
    for (const auto& [name, entry] : m_skills) {
        (void)name;
        result.push_back(entry);
    }
    return result;
}

const SkillEntry* SkillRegistry::findSkill(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_skills.find(name);
    if (it != m_skills.end()) return &it->second;
    return nullptr;
}

std::vector<std::string> SkillRegistry::completeSkillNames(const std::string& prefix, size_t limit) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return completeSkillNamesLocked(prefix, limit);
}

std::string SkillRegistry::buildSkillPrompt(int charBudget) const {
    if (charBudget <= 0) {
        return std::string(kPromptOpenTag) + kPromptCloseTag;
    }

    std::vector<const SkillEntry*> skills;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        skills.reserve(m_skills.size());
        for (const auto& [name, entry] : m_skills) {
            (void)name;
            skills.push_back(&entry);
        }
    }

    std::sort(skills.begin(), skills.end(), [](const SkillEntry* a, const SkillEntry* b) {
        return a->definition.name < b->definition.name;
    });

    const size_t wrapperLength = std::char_traits<char>::length(kPromptOpenTag) +
                                 std::char_traits<char>::length(kPromptCloseTag);

    std::vector<size_t> fullPrefix;
    fullPrefix.reserve(skills.size() + 1);
    fullPrefix.push_back(0);
    for (const auto* skill : skills) {
        fullPrefix.push_back(fullPrefix.back() + formatSkillXml(*skill, false).size());
    }

    const size_t fullLength = wrapperLength + fullPrefix.back();
    if (static_cast<int>(fullLength) <= charBudget) {
        std::string prompt = kPromptOpenTag;
        for (const auto* skill : skills) {
            prompt += formatSkillXml(*skill, false);
        }
        prompt += kPromptCloseTag;
        return prompt;
    }

    std::vector<size_t> compactPrefix;
    compactPrefix.reserve(skills.size() + 1);
    compactPrefix.push_back(0);
    for (const auto* skill : skills) {
        compactPrefix.push_back(compactPrefix.back() + formatSkillXml(*skill, true).size());
    }

    const size_t compactLength = wrapperLength + compactPrefix.back();
    if (static_cast<int>(compactLength) <= charBudget) {
        std::string prompt = kPromptOpenTag;
        for (const auto* skill : skills) {
            prompt += formatSkillXml(*skill, true);
        }
        prompt += kPromptCloseTag;
        return prompt;
    }

    const int keep = binarySearchPromptLimit(compactPrefix, wrapperLength, charBudget);
    // keep == 0 means even the most compact single skill would overflow the budget.
    std::string prompt = kPromptOpenTag;
    for (int i = 0; i < keep; ++i) {
        prompt += formatSkillXml(*skills[static_cast<size_t>(i)], true);
    }
    prompt += kPromptCloseTag;
    return prompt;
}

size_t SkillRegistry::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_skills.size();
}

void SkillRegistry::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_skills.clear();
    m_trieRoot.reset();
    m_lastFingerprint.clear();
}

void SkillRegistry::enableAutoReload(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_watchInterval = interval;
    if (m_watchRunning.load()) {
        return;
    }
    if (m_workspaceDir.empty()) {
        return;
    }

    const auto watchInterval = m_watchInterval;
    m_watchRunning.store(true);
    m_watchThread = std::thread([this, watchInterval]() {
        while (m_watchRunning.load()) {
            std::this_thread::sleep_for(watchInterval);
            if (!m_watchRunning.load()) {
                break;
            }
            this->refreshFromWorkspace();
        }
    });
}

void SkillRegistry::disableAutoReload() {
    bool expected = true;
    if (!m_watchRunning.compare_exchange_strong(expected, false)) {
        return;
    }

    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
}

bool SkillRegistry::refreshFromWorkspace() {
    std::string workspaceDir;
    SkillFilterConfig filterConfig;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        workspaceDir = m_workspaceDir;
        filterConfig = m_filterConfig;
    }

    std::vector<SkillEntry> entries = loadWorkspaceEntries(workspaceDir, filterConfig);
    std::sort(entries.begin(), entries.end(), [](const SkillEntry& a, const SkillEntry& b) {
        return a.definition.name < b.definition.name;
    });

    const std::string fingerprint = makeFingerprint(entries);

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool workspaceChanged =
            workspaceDir != m_workspaceDir ||
            filterConfig.os != m_filterConfig.os ||
            filterConfig.availableBins != m_filterConfig.availableBins ||
            filterConfig.availableEnvs != m_filterConfig.availableEnvs;
        if (workspaceChanged) {
            return false;
        }
        if (fingerprint == m_lastFingerprint) {
            return false;
        }

        m_skills.clear();
        for (auto& entry : entries) {
            m_skills[entry.definition.name] = std::move(entry);
        }
        rebuildIndexesLocked();
        m_lastFingerprint = fingerprint;
        changed = true;
    }

    if (changed) {
        EventBus::instance().emit(Event{
            EventType::SkillChanged,
            workspaceDir,
            std::to_string(entries.size())
        });
    }

    return changed;
}

void SkillRegistry::rebuildIndexesLocked() {
    m_trieRoot = std::make_unique<TrieNode>();
    for (const auto& [name, entry] : m_skills) {
        (void)entry;
        insertTrieNameLocked(name);
    }
}

void SkillRegistry::insertTrieNameLocked(const std::string& name) {
    if (!m_trieRoot) {
        m_trieRoot = std::make_unique<TrieNode>();
    }

    TrieNode* node = m_trieRoot.get();
    for (char ch : name) {
        auto& child = node->children[ch];
        if (!child) {
            child = std::make_unique<TrieNode>();
        }
        node = child.get();
    }
    node->terminal = true;
    node->word = name;
}

std::vector<std::string> SkillRegistry::completeSkillNamesLocked(const std::string& prefix, size_t limit) const {
    std::vector<std::string> result;
    if (limit == 0 || !m_trieRoot) {
        return result;
    }

    const TrieNode* node = m_trieRoot.get();
    for (char ch : prefix) {
        auto it = node->children.find(ch);
        if (it == node->children.end()) {
            return result;
        }
        node = it->second.get();
    }

    std::function<void(const TrieNode*)> dfs = [&](const TrieNode* cur) {
        if (result.size() >= limit) {
            return;
        }
        if (cur->terminal) {
            result.push_back(cur->word);
            if (result.size() >= limit) {
                return;
            }
        }
        for (const auto& [ch, child] : cur->children) {
            (void)ch;
            if (result.size() >= limit) {
                break;
            }
            dfs(child.get());
        }
    };

    dfs(node);
    return result;
}

std::string SkillRegistry::makeFingerprint(const std::vector<SkillEntry>& skills) {
    std::vector<size_t> hashes;
    hashes.reserve(skills.size());
    std::hash<std::string> hasher;
    for (const auto& skill : skills) {
        hashes.push_back(hasher(skillSummary(skill)));
    }
    std::sort(hashes.begin(), hashes.end());

    size_t seed = 0xcbf29ce484222325ULL;
    for (size_t h : hashes) {
        seed ^= h + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    }
    seed ^= hashes.size() + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);

    std::ostringstream oss;
    oss << std::hex << seed;
    return oss.str();
}

int SkillRegistry::binarySearchPromptLimit(
    const std::vector<size_t>& prefixLengths,
    size_t wrapperLength,
    int charBudget
) {
    int lo = 0;
    int hi = static_cast<int>(prefixLengths.size()) - 1;

    while (lo < hi) {
        const int mid = lo + (hi - lo + 1) / 2;
        const size_t promptLength = wrapperLength + prefixLengths[static_cast<size_t>(mid)];
        if (static_cast<int>(promptLength) <= charBudget) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    return lo;
}

std::string SkillRegistry::formatSkillXml(const SkillEntry& entry, bool compact) {
    std::string xml = "  <skill>\n";
    xml += "    <name>" + escapeXml(entry.definition.name) + "</name>\n";
    if (!compact) {
        xml += "    <description>" + escapeXml(entry.definition.description) + "</description>\n";
        if (!entry.definition.body.empty()) {
            xml += "    <instructions>" + escapeXml(entry.definition.body) + "</instructions>\n";
        }
    }
    xml += "    <location>" + escapeXml(entry.definition.filePath) + "</location>\n";
    xml += "  </skill>\n";
    return xml;
}

} // namespace clawlite
