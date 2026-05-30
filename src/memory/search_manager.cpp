// ClawLite — FTS5 全文检索管理器实现
// 简化为仅 FTS5 路径（向量检索已删除，主循环不需要）。
// 仅在 /memory search 命令式触发时使用。

#include "memory/search_manager.h"
#include <algorithm>
#include <unordered_map>

namespace clawlite {

SearchManager::SearchManager(MemoryStore& store)
    : m_store(store) {}

std::vector<SearchResult> SearchManager::search(
    const std::string& query,
    const SearchConfig& config
) {
    auto results = ftsSearch(query, config.topK);

    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) {
            return a.score > b.score;
        });

    if ((int)results.size() > config.topK) {
        results.resize(config.topK);
    }
    return results;
}

std::vector<SearchResult> SearchManager::ftsSearch(const std::string& query, int topK) {
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    auto ftsResults = m_store.ftsSearch(query, topK);
    if (ftsResults.empty()) return results;

    // 一次性建立 hash->chunk 映射，O(n)，避免对每个结果做全表扫描 O(n^2)
    auto allChunks = m_store.getAllChunks();
    std::unordered_map<std::string, MemoryChunk> chunkMap;
    chunkMap.reserve(allChunks.size());
    for (auto& chunk : allChunks) {
        chunkMap[chunk.hash] = std::move(chunk);
    }

    for (const auto& [chunkId, bm25Score] : ftsResults) {
        auto it = chunkMap.find(chunkId);
        if (it != chunkMap.end()) {
            const auto& chunk = it->second;
            SearchResult r;
            r.path = chunk.path;
            r.startLine = chunk.startLine;
            r.endLine = chunk.endLine;
            r.score = bm25Score;
            r.textScore = bm25Score;
            r.snippet = chunk.text.size() > 200 ? chunk.text.substr(0, 200) : chunk.text;
            r.source = SearchSource::Memory;
            results.push_back(r);
        }
    }
    return results;
}

} // namespace clawlite
