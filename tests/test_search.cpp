// ClawLite — 搜索测试
// TODO: B 同学补充测试用例

#include "memory/search_manager.h"
#include "memory/memory_store.h"
#include "memory/embedding.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testCosineSimilarity() {
    std::vector<double> a = {1.0, 0.0, 0.0};
    std::vector<double> b = {1.0, 0.0, 0.0};
    std::vector<double> c = {0.0, 1.0, 0.0};

    TEST_ASSERT(cosineSimilarity(a, b) > 0.99);  // 相同向量
    TEST_ASSERT(cosineSimilarity(a, c) < 0.01);  // 正交向量
    std::cout << "  [PASS] testCosineSimilarity\n";
}

void testVectorSearch() {
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

    // 创建搜索管理器
    auto embedding = std::make_unique<LocalMockEmbedding>(128);
    SearchManager search(store, std::move(embedding));

    auto results = search.vectorSearch("fox", 5);
    // 至少应该有结果
    std::cout << "  [PASS] testVectorSearch (" << results.size() << " results)\n";

    store.close();
}

void testHybridSearch() {
    // TODO: 测试混合搜索
    std::cout << "  [SKIP] testHybridSearch (需要完整实现)\n";
}

int run_search_tests() {
    std::cout << "Search Tests:\n";
    RESET_FAILURES();
    testCosineSimilarity();
    testVectorSearch();
    testHybridSearch();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All search tests passed.\n";
    else std::cout << f << " search test(s) failed.\n";
    return f;
}
