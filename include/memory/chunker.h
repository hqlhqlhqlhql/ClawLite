#pragma once
// ClawLite — Markdown 滑动窗口分块算法
// 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown
// 数据结构：滑动窗口（双指针/队列）遍历文本行
//          vector<MemoryChunk> 存储分块结果
//
// 算法核心：
//   按行遍历 Markdown，维护一个 [start, end] 滑动窗口
//   窗口大小 ≈ chunkTokens（如 200 token），步长 ≈ chunkTokens - overlap（如 50 token）
//   每当窗口大小达到阈值，输出一个 chunk，窗口起点前进一个步长
//
// 这是数据结构课程重点！要在答辩中展示窗口滑动过程。

#include "core/types.h"
#include <vector>
#include <string>

namespace clawlite {

struct ChunkerConfig {
    int chunkTokens = 200;       // 每个块的目标 token 数
    int overlapTokens = 50;      // 相邻块的重叠 token 数
    double charsPerToken = 4.0;  // 每个 token 的平均字符数（用于估算）
};

class Chunker {
public:
    // 将 Markdown 文本分块
    // 输入：文件路径 + 文件内容
    // 输出：MemoryChunk 列表
    //
    // 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown
    // 滑动窗口过程：
    //   lines = split(content, '\n')
    //   start = 0
    //   while start < lines.size():
    //       end = start
    //       charCount = 0
    //       while end < lines.size() && charCount < maxChars:
    //           charCount += len(lines[end]) + 1
    //           end++
    //       chunk = join(lines[start:end])
    //       output(chunk)
    //       start += stepSize  // stepSize = chunkSize - overlap
    static std::vector<MemoryChunk> chunkMarkdown(
        const std::string& filePath,
        const std::string& content,
        const ChunkerConfig& config = {}
    );

    // 估算文本的 token 数
    static int estimateTokens(const std::string& text, double charsPerToken = 4.0);

private:
    // 计算内容 hash（用于去重/缓存判断）
    static std::string computeHash(const std::string& text);
};

} // namespace clawlite
