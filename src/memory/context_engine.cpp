// ClawLite — 上下文引擎实现
// 组合 MemoryStore + Chunker + SearchManager + SessionStore + Compactor + FileStateCache

#include "memory/context_engine.h"
#include "memory/memory_store.h"
#include "memory/chunker.h"
#include "memory/search_manager.h"
#include "memory/session_store.h"
#include "memory/compaction.h"
#include "memory/file_state_cache.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <memory>
#include <functional>

namespace clawlite {

// 具体实现类
class DefaultContextEngine : public IContextEngine {
public:
    void initialize(const std::string& dataDir) override {
        m_dataDir = dataDir;
        std::string dbPath = dataDir + "/clawlite.db";
        m_store.open(dbPath);
        // SearchManager 仅用 FTS5，不再需要 embedding
        m_search = std::make_unique<SearchManager>(m_store);

        // 初始化文件状态缓存（容量 32，LRU 淘汰）
        m_fileCache = std::make_unique<FileStateCache>(32);

        // 加载持久化会话
        std::string sessionPath = dataDir + "/sessions.jsonl";
        m_sessions.load(sessionPath);
    }

    void dispose() override {
        m_store.close();
    }

    // ── 记忆管理 ──────────────────────────────────────────

    void indexFile(const std::string& filePath) override {
        // 读取文件内容
        std::ifstream file(filePath);
        if (!file.is_open()) return;
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();
        file.close();

        // 分块
        auto chunks = Chunker::chunkMarkdown(filePath, content);

        // 存入数据库（不再计算 embedding）
        m_store.beginTransaction();
        FileEntry entry;
        entry.path = filePath;
        // 文件内容 hash，用于 needsReindex() 检测变化
        size_t h = std::hash<std::string>{}(content);
        char buf[20];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
        entry.hash = std::string(buf);
        entry.size = static_cast<int64_t>(content.size());
        entry.mtimeMs = nowMs();
        m_store.upsertFile(entry);

        for (const auto& chunk : chunks) {
            m_store.upsertChunk(chunk);
        }
        m_store.commit();

        // 缓存文件内容（供压缩后状态重注入）
        m_fileCache->put(filePath, content, entry.mtimeMs);
    }

    void removeFile(const std::string& filePath) override {
        m_store.removeChunksByFile(filePath);
        m_store.removeFile(filePath);
        m_fileCache->invalidate(filePath);
    }

    bool needsReindex(const std::string& filePath) override {
        auto existing = m_store.getFile(filePath);
        if (!existing.has_value()) return true;
        // 简单检查：文件大小变化
        try {
            auto fileSize = std::filesystem::file_size(filePath);
            return static_cast<int64_t>(fileSize) != existing->size;
        } catch (...) {
            return true;
        }
    }

    // ── 消息写入 ──────────────────────────────────────────

    void ingest(const Message& msg) override {
        m_sessions.appendMessage(m_sessionKey, msg);
        // 每条消息都持久化
        m_sessions.save(m_dataDir + "/sessions.jsonl");
    }

    void ingestBatch(const std::vector<Message>& messages) override {
        for (const auto& msg : messages) {
            m_sessions.appendMessage(m_sessionKey, msg);
        }
        // 批量持久化
        m_sessions.save(m_dataDir + "/sessions.jsonl");
    }

    // ── 上下文组装 ────────────────────────────────────────

    AssembleResult assemble(int tokenBudget) override {
        AssembleResult result;

        // 1. 取最近对话
        auto history = m_sessions.getHistory(m_sessionKey, 20);

        // 2. 加入对话历史
        for (const auto& msg : history) {
            result.messages.push_back(msg);
        }

        // 3. 估算总 token
        result.estimatedTokens = Compactor::countTotalTokens(result.messages);
        return result;
    }

    // ── 上下文压缩 ────────────────────────────────────────

    CompactResult compact(int targetTokens) override {
        auto history = m_sessions.getAllMessages(m_sessionKey);
        CompactionConfig config;
        config.targetTokens = targetTokens;

        auto result = Compactor::compact(history, config);
        if (result.compacted) {
            // 状态重注入：从 FileStateCache 取最近 3 个文件内容拼回 context
            // 找到最近被引用的文件路径（从历史消息中提取）
            std::vector<std::string> recentFiles;
            for (const auto& msg : history) {
                // 简单实现：检查消息内容是否包含文件路径特征
                if (msg.content.find("file:") != std::string::npos ||
                    msg.content.find("path:") != std::string::npos) {
                    // 提取路径（简化实现）
                    size_t pos = msg.content.find("file:");
                    if (pos == std::string::npos) pos = msg.content.find("path:");
                    if (pos != std::string::npos) {
                        pos += 5;  // skip "file:" or "path:"
                        size_t end = msg.content.find_first_of(" \n", pos);
                        if (end == std::string::npos) end = msg.content.size();
                        std::string path = msg.content.substr(pos, end - pos);
                        recentFiles.push_back(path);
                    }
                }
            }

            // 从缓存恢复最近 3 个文件内容
            int injected = 0;
            for (int i = (int)recentFiles.size() - 1; i >= 0 && injected < 3; i--) {
                auto cached = m_fileCache->get(recentFiles[i]);
                if (cached.has_value()) {
                    Message sysMsg = Message::system("[File context: " + recentFiles[i] + "]\n" + cached.value());
                    history.insert(history.begin(), sysMsg);
                    injected++;
                }
            }

            // 替换会话中的消息
            m_sessions.clear();
            m_sessions.createSession(m_sessionKey);
            for (const auto& msg : history) {
                m_sessions.appendMessage(m_sessionKey, msg);
            }
            // 持久化压缩后的会话
            m_sessions.save(m_dataDir + "/sessions.jsonl");
        }
        return result;
    }

    // ── 搜索 ──────────────────────────────────────────────

    std::vector<SearchResult> search(const std::string& query, int topK) override {
        SearchConfig cfg;
        cfg.topK = topK;
        return m_search->search(query, cfg);
    }

    // ── 会话管理 ──────────────────────────────────────────

    void createSession(const std::string& sessionKey) override {
        m_sessionKey = sessionKey;
        m_sessions.createSession(sessionKey);
    }

    std::vector<Message> getSessionHistory(const std::string& sessionKey, int maxTurns) override {
        return m_sessions.getHistory(sessionKey, maxTurns);
    }

    int getIndexedFileCount() const override {
        return m_store.fileCount();
    }

    int getIndexedChunkCount() const override {
        return m_store.chunkCount();
    }

private:
    std::string m_dataDir;
    std::string m_sessionKey = "default";
    MemoryStore m_store;
    SessionStore m_sessions;
    std::unique_ptr<SearchManager> m_search;
    std::unique_ptr<FileStateCache> m_fileCache;
};

// 工厂函数
std::unique_ptr<IContextEngine> createContextEngine() {
    return std::make_unique<DefaultContextEngine>();
}

} // namespace clawlite
