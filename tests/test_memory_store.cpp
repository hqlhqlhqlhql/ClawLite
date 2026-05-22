// ClawLite — SQLite 存储层测试
// TODO: B 同学补充测试用例

#include "memory/memory_store.h"
#include "test_helpers.h"
#include <iostream>
#include <cstdio>

using namespace clawlite;

void testOpenClose() {
    MemoryStore store;
    bool ok = store.open(":memory:");
    TEST_ASSERT(ok);
    store.close();
    std::cout << "  [PASS] testOpenClose\n";
}

void testFileCrud() {
    MemoryStore store;
    store.open(":memory:");

    FileEntry entry;
    entry.path = "test.md";
    entry.hash = "abc123";
    entry.mtimeMs = 1000;
    entry.size = 500;

    store.upsertFile(entry);
    TEST_ASSERT(store.fileCount() == 1);

    auto got = store.getFile("test.md");
    TEST_ASSERT(got.has_value());
    if (got.has_value()) {
        TEST_ASSERT(got->path == "test.md");
        TEST_ASSERT(got->hash == "abc123");
    }

    store.removeFile("test.md");
    TEST_ASSERT(store.fileCount() == 0);

    store.close();
    std::cout << "  [PASS] testFileCrud\n";
}

void testChunkCrud() {
    MemoryStore store;
    store.open(":memory:");

    MemoryChunk chunk;
    chunk.path = "test.md";
    chunk.startLine = 1;
    chunk.endLine = 10;
    chunk.text = "Hello world";
    chunk.hash = "def456";

    store.upsertChunk(chunk);
    TEST_ASSERT(store.chunkCount() == 1);

    auto chunks = store.getChunksByFile("test.md");
    TEST_ASSERT(chunks.size() == 1);
    if (!chunks.empty()) {
        TEST_ASSERT(chunks[0].text == "Hello world");
    }

    store.removeChunksByFile("test.md");
    TEST_ASSERT(store.chunkCount() == 0);

    store.close();
    std::cout << "  [PASS] testChunkCrud\n";
}

void testFtsSearch() {
    // TODO: 测试 FTS5 全文搜索
    // 需要 SQLite 编译时启用 FTS5
    std::cout << "  [SKIP] testFtsSearch (需要 FTS5 支持)\n";
}

void testEmbeddingCache() {
    MemoryStore store;
    store.open(":memory:");

    std::vector<double> embedding = {0.1, 0.2, 0.3};
    store.cacheEmbedding("hash1", "local", "mock", embedding);

    auto cached = store.getCachedEmbedding("hash1", "local", "mock");
    TEST_ASSERT(cached.has_value());
    if (cached.has_value()) {
        TEST_ASSERT(cached->size() == 3);
        TEST_ASSERT((*cached)[0] == 0.1);
    }

    store.close();
    std::cout << "  [PASS] testEmbeddingCache\n";
}

int run_memory_store_tests() {
    std::cout << "Memory Store Tests:\n";
    RESET_FAILURES();
    testOpenClose();
    testFileCrud();
    testChunkCrud();
    testFtsSearch();
    testEmbeddingCache();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All memory store tests passed.\n";
    else std::cout << f << " memory store test(s) failed.\n";
    return f;
}
