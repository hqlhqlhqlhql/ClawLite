#pragma once
// ClawLite — 上下文引擎接口（策略模式）
// 参考：openclaw-main/src/context-engine/types.ts — ContextEngine 接口
// 设计模式：策略模式 — 定义统一接口，可插拔不同实现
//
// 这是 B 模块的核心接口，C 模块通过此接口访问上下文系统

#include "core/types.h"
#include <vector>
#include <string>
#include <optional>
#include <memory>

namespace clawlite {

struct AssembleResult {
    std::vector<Message> messages;   // 组装好的消息列表
    int estimatedTokens = 0;         // 估算的 token 数
};

class IContextEngine {
public:
    virtual ~IContextEngine() = default;

    // ── 生命周期 ──────────────────────────────────────────

    // 初始化引擎（创建数据库表等）
    virtual void initialize(const std::string& dataDir) = 0;

    // 释放资源
    virtual void dispose() = 0;

    // ── 记忆管理 ──────────────────────────────────────────

    // 将文件内容索引到记忆系统
    // 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts — listMemoryFiles + chunkMarkdown
    virtual void indexFile(const std::string& filePath) = 0;

    // 删除文件索引
    virtual void removeFile(const std::string& filePath) = 0;

    // 检查文件是否需要重新索引（mtime/hash 变化）
    virtual bool needsReindex(const std::string& filePath) = 0;

    // ── 消息写入 ──────────────────────────────────────────

    // 写入一条消息到当前会话
    // 参考：openclaw-main/src/context-engine/types.ts — ingest()
    virtual void ingest(const Message& msg) = 0;

    // 批量写入一轮对话（用户+助手）
    // 参考：openclaw-main/src/context-engine/types.ts — ingestBatch()
    virtual void ingestBatch(const std::vector<Message>& messages) = 0;

    // ── 上下文组装 ────────────────────────────────────────

    // 在 token 预算内组装上下文（从记忆和会话中选取最相关的内容）
    // 参考：openclaw-main/src/context-engine/types.ts — assemble()
    // 这是核心方法，需要：
    //   1. 取最近 N 轮对话
    //   2. 搜索相关记忆块
    //   3. 合并去重
    //   4. 裁剪到 token 预算内
    virtual AssembleResult assemble(int tokenBudget) = 0;

    // ── 上下文压缩 ────────────────────────────────────────

    // 压缩上下文（当 token 超限时调用）
    // 参考：openclaw-main/src/agents/pi-embedded-runner/compact.ts
    // 简化实现：截断旧对话，保留最近 N 轮 + 摘要
    virtual CompactResult compact(int targetTokens) = 0;

    // ── 搜索 ──────────────────────────────────────────────

    // 语义搜索（混合：向量余弦 + FTS5 全文）
    // 参考：openclaw-main/packages/memory-host-sdk/host/types.ts — MemorySearchManager.search()
    virtual std::vector<SearchResult> search(const std::string& query, int topK = 5) = 0;

    // ── 会话管理 ──────────────────────────────────────────

    // 创建会话
    virtual void createSession(const std::string& sessionKey) = 0;

    // 获取会话历史
    virtual std::vector<Message> getSessionHistory(
        const std::string& sessionKey,
        int maxTurns = 20
    ) = 0;

    // 获取索引的文件/块数量（用于状态显示）
    virtual int getIndexedFileCount() const = 0;
    virtual int getIndexedChunkCount() const = 0;
};

// 创建默认上下文引擎实例
std::unique_ptr<IContextEngine> createContextEngine();

} // namespace clawlite
