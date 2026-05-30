#pragma once
// ClawLite — FTS5 全文检索管理器
// 参考：openclaw-main/packages/memory-host-sdk/host/types.ts — MemorySearchManager
//
// 简化为仅 FTS5 路径（向量检索已删除，主循环不需要）。
// 仅在 /memory search 命令式触发时使用。

#include "core/types.h"
#include "memory/memory_store.h"
#include <vector>
#include <string>

namespace clawlite {

struct SearchConfig {
    int topK = 5;                    // 返回结果数
    double vectorWeight = 0.7;       // 保留字段，兼容旧代码
    double textWeight = 0.3;         // 保留字段，兼容旧代码
    int vectorCandidateLimit = 50;   // 保留字段
    int ftsCandidateLimit = 20;      // 保留字段
    bool useHandwrittenFts = false;  // 保留字段
};

class SearchManager {
public:
    explicit SearchManager(MemoryStore& store);

    // 全文搜索：FTS5 MATCH 查询，BM25 排序
    // 仅通过 /memory search 命令式触发，不在主循环自动运行
    std::vector<SearchResult> search(
        const std::string& query,
        const SearchConfig& config = {}
    );

    // 仅全文搜索（FTS5）
    std::vector<SearchResult> ftsSearch(
        const std::string& query,
        int topK = 10
    );

private:
    MemoryStore& m_store;
};

} // namespace clawlite
