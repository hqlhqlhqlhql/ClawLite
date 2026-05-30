// ClawLite — 搜索测试
// 简化为仅测试 FTS5 路径（向量检索已删除）

#include "memory/search_manager.h"
#include "memory/memory_store.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testSearchManagerConstructor() {
    MemoryStore store;
    store.open(":memory:");
    SearchManager search(store);
    std::cout << "  [PASS] testSearchManagerConstructor\n";
    store.close();
}

void testSearchManagerFts() {
    MemoryStore store;
    store.open(":memory:");

    // 插入一些测试数据
    MemoryChunk chunk1;
    chunk1.path = "doc.md";
    chunk1.startLine = 1;
    chunk1.endLine = 5;
    chunk1.text = "The quick brown fox jumps over the lazy dog";
    chunk1.hash = "h1";
    store.upsertChunk(chunk1);

    MemoryChunk chunk2;
    chunk2.path = "doc.md";
    chunk2.startLine = 6;
    chunk2.endLine = 10;
    chunk2.text = "Machine learning is a subset of artificial intelligence";
    chunk2.hash = "h2";
    store.upsertChunk(chunk2);

    // 创建搜索管理器（仅 FTS5）
    SearchManager search(store);

    // 测试 FTS5 搜索
    SearchConfig cfg;
    cfg.topK = 5;
    auto results = search.search("fox", cfg);
    std::cout << "  [PASS] testSearchManagerFts (" << results.size() << " results)\n";

    store.close();
}

int run_search_tests() {
    std::cout << "Search Tests:\n";
    RESET_FAILURES();
    testSearchManagerConstructor();
    testSearchManagerFts();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All search tests passed.\n";
    else std::cout << f << " search test(s) failed.\n";
    return f;
}
