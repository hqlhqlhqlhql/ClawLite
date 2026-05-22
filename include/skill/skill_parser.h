#pragma once
// ClawLite — SKILL.md Frontmatter 解析器
// 参考：openclaw-main/src/markdown/frontmatter.ts:parseFrontmatterBlock
// 数据结构：unordered_map<string, string> 存储解析出的 KV 对
// 算法：双策略解析（行正则 + YAML 库），结果合并时 YAML 优先

#include <string>
#include <unordered_map>
#include <optional>

namespace clawlite {

using ParsedFrontmatter = std::unordered_map<std::string, std::string>;

struct SkillDefinition {
    std::string name;
    std::string description;
    ParsedFrontmatter frontmatter;   // 完整的 KV 对
    std::string body;                // frontmatter 之后的 Markdown 正文
    std::string filePath;            // SKILL.md 的路径
};

class SkillParser {
public:
    // 解析一个 SKILL.md 文件，返回 SkillDefinition
    // 如果解析失败（无 name 或 description），返回 nullopt
    static std::optional<SkillDefinition> parse(const std::string& filePath);

    // 仅解析 frontmatter 块（--- 之间的内容）
    // 参考：openclaw-main/src/markdown/frontmatter.ts:parseFrontmatterBlock
    static ParsedFrontmatter parseFrontmatter(const std::string& rawBlock);

    // 从原始文本中提取 frontmatter 块和 body
    // 返回 {frontmatterBlock, body}
    static std::pair<std::string, std::string> extractSections(const std::string& content);

private:
    // 行正则解析策略：逐行匹配 key: value
    static ParsedFrontmatter parseByLine(const std::string& block);

    // YAML 库解析策略（如果链接了 yaml-cpp）
    static ParsedFrontmatter parseByYaml(const std::string& block);
};

} // namespace clawlite
