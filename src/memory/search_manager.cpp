// ClawLite — 混合检索管理器实现
// TODO: B 同学实现 — 数据结构课程重点！

#include "memory/search_manager.h"
#include <algorithm>
#include <unordered_map>

namespace clawlite {

SearchManager::SearchManager(MemoryStore& store, std::unique_ptr<IEmbeddingProvider> embedding)
    : m_store(store), m_embedding(std::move(embedding)) {}

std::vector<SearchResult> SearchManager::search(
    const std::string& query,
    const SearchConfig& config
) {
    // TODO: 实现 — 混合搜索算法
    //
    // 参考：openclaw-main/packages/memory-host-sdk/host/types.ts:search
    //
    // 步骤：
    //   1. vectorResults = vectorSearch(query, config.vectorCandidateLimit)
    //   2. textResults = ftsSearch(query, config.ftsCandidateLimit)
    //   3. merged = mergeResults(vectorResults, textResults, config.vectorWeight, config.textWeight)
    //   4. sort merged by score descending
    //   5. return top config.topK

    auto vectorResults = vectorSearch(query, config.vectorCandidateLimit);
    auto textResults = ftsSearch(query, config.ftsCandidateLimit);
    auto merged = mergeResults(vectorResults, textResults, config.vectorWeight, config.textWeight);

    // 按分数降序排序
    std::sort(merged.begin(), merged.end(),
        [](const SearchResult& a, const SearchResult& b) {
            return a.score > b.score;
        });

    // 取 top-k
    if ((int)merged.size() > config.topK) {
        merged.resize(config.topK);
    }
    return merged;
}

std::vector<SearchResult> SearchManager::vectorSearch(const std::string& query, int topK) {
    // 向量搜索：暴力遍历 + 排序，O(n*d)
    // 大规模数据需要 ANN 索引（如 HNSW），课程作业规模足够
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    auto queryEmbedding = m_embedding->embedQuery(query);
    auto allChunks = m_store.getAllChunks();

    for (const auto& chunk : allChunks) {
        if (chunk.embedding.empty()) continue;
        double score = cosineSimilarity(queryEmbedding, chunk.embedding);
        SearchResult r;
        r.path = chunk.path;
        r.startLine = chunk.startLine;
        r.endLine = chunk.endLine;
        r.score = score;
        r.vectorScore = score;
        r.snippet = chunk.text.substr(0, 200);
        r.source = SearchSource::Memory;
        results.push_back(r);
    }

    // 按分数降序排序，取 top-k
    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) {
            return a.score > b.score;
        });
    if ((int)results.size() > topK) results.resize(topK);
    return results;
}

std::vector<SearchResult> SearchManager::ftsSearch(const std::string& query, int topK) {
    // FTS5 全文搜索：调 memory_store 的 FTS，再查完整 chunk 信息
    std::vector<SearchResult> results;
    if (query.empty()) return results;

    auto ftsResults = m_store.ftsSearch(query, topK);
    for (const auto& [chunkId, bm25Score] : ftsResults) {
        // 用 chunkId（即 hash）查找完整 chunk
        auto allChunks = m_store.getAllChunks();
        for (const auto& chunk : allChunks) {
            if (chunk.hash == chunkId) {
                SearchResult r;
                r.path = chunk.path;
                r.startLine = chunk.startLine;
                r.endLine = chunk.endLine;
                r.score = bm25Score;
                r.textScore = bm25Score;
                r.snippet = chunk.text.substr(0, 200);
                r.source = SearchSource::Memory;
                results.push_back(r);
                break;
            }
        }
    }
    return results;
}

std::vector<SearchResult> SearchManager::mergeResults(
    const std::vector<SearchResult>& vectorResults,
    const std::vector<SearchResult>& textResults,
    double vectorWeight,
    double textWeight
) {
    // 结果合并：按 path+startLine 去重，加权求和
    // 数据结构：unordered_map 实现 O(1) 查重
    std::unordered_map<std::string, size_t> index;  // key → 在 merged 中的位置
    std::vector<SearchResult> merged;

    // 归一化两个列表
    auto normVec = vectorResults;
    auto normText = textResults;
    normalizeScores(normVec);
    normalizeScores(normText);

    // 加入向量搜索结果
    for (auto& r : normVec) {
        std::string key = r.path + ":" + std::to_string(r.startLine);
        r.score *= vectorWeight;
        merged.push_back(r);
        index[key] = merged.size() - 1;
    }

    // 加入全文搜索结果，重复的累加分数
    for (auto& r : normText) {
        std::string key = r.path + ":" + std::to_string(r.startLine);
        r.score *= textWeight;
        auto it = index.find(key);
        if (it != index.end()) {
            // 同一 chunk 在两个列表都出现，分数相加
            merged[it->second].score += r.score;
            merged[it->second].textScore = r.textScore;
        } else {
            merged.push_back(r);
            index[key] = merged.size() - 1;
        }
    }

    return merged;
}

void SearchManager::normalizeScores(std::vector<SearchResult>& results) {
    // TODO: 实现分数归一化
    // 找到 maxScore，然后每个分数 /= maxScore
    if (results.empty()) return;
    double maxScore = 0;
    for (const auto& r : results) {
        if (r.score > maxScore) maxScore = r.score;
    }
    if (maxScore > 0) {
        for (auto& r : results) {
            r.score /= maxScore;
        }
    }
}

} // namespace clawlite
