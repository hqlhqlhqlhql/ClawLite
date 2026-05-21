// ClawLite — 上下文引擎实现
// 组合 MemoryStore + Chunker + SearchManager + SessionStore + Compactor

#include "memory/context_engine.h"
#include "memory/memory_store.h"
#include "memory/chunker.h"
#include "memory/embedding.h"
#include "memory/search_manager.h"
#include "memory/session_store.h"
#include "memory/compaction.h"
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
        m_embedding = std::make_unique<LocalMockEmbedding>(128);
        m_search = std::make_unique<SearchManager>(m_store, std::make_unique<LocalMockEmbedding>(128));
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

        // 计算嵌入
        std::vector<std::string> texts;
        for (const auto& chunk : chunks) {
            texts.push_back(chunk.text);
        }
        auto embeddings = m_embedding->embedBatch(texts);

        // 存入数据库
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

        for (size_t i = 0; i < chunks.size(); i++) {
            MemoryChunk chunk = chunks[i];
            if (i < embeddings.size()) {
                chunk.embedding = embeddings[i];
            }
            m_store.upsertChunk(chunk);
        }
        m_store.commit();
    }

    void removeFile(const std::string& filePath) override {
        m_store.removeChunksByFile(filePath);
        m_store.removeFile(filePath);
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
    }

    void ingestBatch(const std::vector<Message>& messages) override {
        for (const auto& msg : messages) {
            m_sessions.appendMessage(m_sessionKey, msg);
        }
    }

    // ── 上下文组装 ────────────────────────────────────────

    AssembleResult assemble(int tokenBudget) override {
        AssembleResult result;

        // 1. 取最近对话
        auto history = m_sessions.getHistory(m_sessionKey, 20);
        int historyTokens = Compactor::countTotalTokens(history);

        // 2. 搜索相关记忆（用最后一条用户消息作为查询）
        int remaining = tokenBudget - historyTokens;
        if (remaining > 200) {
            std::string query;
            for (int i = (int)history.size() - 1; i >= 0; i--) {
                if (history[i].role == Role::User) {
                    query = history[i].content;
                    break;
                }
            }
            if (!query.empty()) {
                SearchConfig cfg;
                cfg.topK = 3;
                auto memResults = m_search->search(query, cfg);
                // 把搜索结果转为 system 消息，插到对话前面
                for (const auto& sr : memResults) {
                    std::string memMsg = "[Memory: " + sr.path + ":" +
                        std::to_string(sr.startLine) + "-" +
                        std::to_string(sr.endLine) + "] " + sr.snippet;
                    Message sysMsg = Message::system(memMsg);
                    int msgTokens = estimateTokens(sysMsg.content);
                    if (remaining >= msgTokens) {
                        result.messages.insert(result.messages.begin(), sysMsg);
                        remaining -= msgTokens;
                    }
                }
            }
        }

        // 3. 加入对话历史
        for (const auto& msg : history) {
            result.messages.push_back(msg);
        }

        // 4. 估算总 token
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
            // 替换会话中的消息
            m_sessions.clear();
            m_sessions.createSession(m_sessionKey);
            for (const auto& msg : history) {
                m_sessions.appendMessage(m_sessionKey, msg);
            }
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
    std::unique_ptr<IEmbeddingProvider> m_embedding;
    std::unique_ptr<SearchManager> m_search;
};

// 工厂函数
std::unique_ptr<IContextEngine> createContextEngine() {
    return std::make_unique<DefaultContextEngine>();
}

} // namespace clawlite
