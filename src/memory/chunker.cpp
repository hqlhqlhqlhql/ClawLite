// ClawLite — 滑动窗口分块算法实现
// TODO: B 同学实现 — 数据结构课程重点！

#include "memory/chunker.h"
#include <sstream>
#include <algorithm>
#include <functional>

namespace clawlite {

std::vector<MemoryChunk> Chunker::chunkMarkdown(
    const std::string& filePath,
    const std::string& content,
    const ChunkerConfig& config
) {
    // TODO: 实现 — 滑动窗口算法
    //
    // 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown
    //
    // 伪代码：
    //   lines = split(content, '\n')
    //   maxChars = chunkTokens * charsPerToken
    //   stepChars = (chunkTokens - overlapTokens) * charsPerToken
    //   start = 0
    //   chunks = []
    //   while start < lines.size():
    //       end = start
    //       charCount = 0
    //       // 扩展窗口右边界
    //       while end < lines.size() && charCount < maxChars:
    //           charCount += len(lines[end]) + 1  // +1 for newline
    //           end++
    //       // 生成 chunk
    //       text = join(lines[start:end], '\n')
    //       chunk = MemoryChunk {
    //           path: filePath,
    //           startLine: start + 1,  // 1-indexed
    //           endLine: end,
    //           text: text,
    //           hash: computeHash(text)
    //       }
    //       chunks.push_back(chunk)
    //       // 滑动窗口：起点前进 stepSize 行
    //       stepLines = 0
    //       stepCharsUsed = 0
    //       while stepCharsUsed < stepChars && start < end:
    //           stepCharsUsed += len(lines[start]) + 1
    //           start++
    //           stepLines++
    //   return chunks
    //
    // 关键点：
    //   - 滑动窗口的大小由字符数决定（近似 token 数）
    //   - 重叠由 stepSize < windowSize 实现
    //   - CJK 文本特殊处理（按字符而非空格分割）

    std::vector<MemoryChunk> chunks;

    // 按行分割
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    if (lines.empty()) return chunks;

    // CJK 检测：统计 CJK 字符占比，自动调整 charsPerToken
    double charsPerToken = config.charsPerToken;
    bool isCJK = false;
    if (config.autoDetectCJK) {
        int cjkChars = 0;
        int totalChars = 0;
        for (const auto& ln : lines) {
            for (size_t i = 0; i < ln.size(); ) {
                totalChars++;
                unsigned char c = static_cast<unsigned char>(ln[i]);
                if (c >= 0x80) {
                    unsigned int cp = 0;
                    int bytes = 0;
                    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; bytes = 1; }
                    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; bytes = 2; }
                    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; bytes = 3; }
                    for (int b = 0; b < bytes && i + 1 + b < ln.size(); b++) {
                        cp = (cp << 6) | (static_cast<unsigned char>(ln[i + 1 + b]) & 0x3F);
                    }
                    if ((cp >= 0x4E00 && cp <= 0x9FFF) ||
                        (cp >= 0x3400 && cp <= 0x4DBF) ||
                        (cp >= 0xF900 && cp <= 0xFAFF) ||
                        (cp >= 0x3040 && cp <= 0x30FF) ||
                        (cp >= 0xAC00 && cp <= 0xD7AF) ||
                        (cp >= 0xFF00 && cp <= 0xFFEF)) {
                        cjkChars++;
                    }
                    i += 1 + bytes;
                } else {
                    i++;
                }
            }
        }
        if (totalChars > 0 && (double)cjkChars / totalChars > 0.3) {
            charsPerToken = 1.5;
            isCJK = true;
        }
    }

    // CJK 场景：预处理——将超长行在标点边界处拆分为多行
    if (isCJK) {
        std::vector<std::string> processed;
        processed.reserve(lines.size() * 2);
        // 标点字符集（CJK 停顿点 + ASCII 标点）
        auto isBoundary = [](unsigned int cp) -> bool {
            return (cp >= 0x3000 && cp <= 0x303F) ||
                   cp == 0xFF0C || cp == 0xFF0E ||
                   cp == ',' || cp == '.' || cp == '!' || cp == '?' ||
                   cp == ';' || cp == ':' || cp == ' ';
        };

        // 预估的每行字符上限
        int lineLimit = static_cast<int>(config.chunkTokens * charsPerToken) / 2;

        for (const auto& ln : lines) {
            if ((int)ln.size() <= lineLimit) {
                processed.push_back(ln);
                continue;
            }
            int pos = 0;
            while (pos < (int)ln.size()) {
                int endPos = std::min(pos + lineLimit, (int)ln.size());
                if (endPos >= (int)ln.size()) {
                    processed.push_back(ln.substr(pos));
                    break;
                }
                int split = endPos;
                for (int j = endPos - 1; j > pos; j--) {
                    unsigned char c = static_cast<unsigned char>(ln[j]);
                    if (c < 0x80) {
                        if (isBoundary(c)) { split = j + 1; break; }
                    } else if ((c & 0xF0) == 0xE0 && j + 2 < endPos) {
                        unsigned int cp = ((c & 0x0F) << 12) |
                            ((static_cast<unsigned char>(ln[j+1]) & 0x3F) << 6) |
                            (static_cast<unsigned char>(ln[j+2]) & 0x3F);
                        if (isBoundary(cp)) { split = j + 3; break; }
                    }
                }
                if (split == pos) split = endPos;
                processed.push_back(ln.substr(pos, split - pos));
                pos = split;
            }
        }
        lines = std::move(processed);
    }

    int maxChars = static_cast<int>(config.chunkTokens * charsPerToken);
    int stepChars = static_cast<int>((config.chunkTokens - config.overlapTokens) * charsPerToken);

    // 滑动窗口算法
    int start = 0;
    while (start < (int)lines.size()) {
        int end = start;
        int charCount = 0;
        // 扩展窗口右边界
        while (end < (int)lines.size() && charCount < maxChars) {
            charCount += (int)lines[end].size() + 1;
            end++;
        }

        // 拼接文本
        std::string text;
        for (int i = start; i < end; i++) {
            if (i > start) text += '\n';
            text += lines[i];
        }
        MemoryChunk chunk;
        chunk.path = filePath;
        chunk.startLine = start + 1;  // 1-indexed
        chunk.endLine = end;
        chunk.text = text;
        chunk.hash = computeHash(text);
        chunks.push_back(chunk);
        // 滑动窗口前进
        int stepUsed = 0;
        while (stepUsed < stepChars && start < end) {
            stepUsed += (int)lines[start].size() + 1;
            start++;
        }
        // 防止无限循环：至少前进 1 行
        if (start == end && end < (int)lines.size()) start++;
    }

    return chunks;
}

int Chunker::estimateTokens(const std::string& text, double charsPerToken) {
    return static_cast<int>(text.size() / charsPerToken) + 1;
}

std::string Chunker::computeHash(const std::string& text) {
    // 简单的 hash 实现（std::hash）
    size_t h = std::hash<std::string>{}(text);
    // 转为 hex 字符串
    char buf[20];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

} // namespace clawlite
