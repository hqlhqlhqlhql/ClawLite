// ClawLite — 分块算法测试
// TODO: B 同学补充测试用例

#include "memory/chunker.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testBasicChunking() {
    std::string content;
    for (int i = 0; i < 100; i++) {
        content += "Line " + std::to_string(i) + ": This is a test line with some content.\n";
    }

    ChunkerConfig config;
    config.chunkTokens = 50;
    config.overlapTokens = 10;

    auto chunks = Chunker::chunkMarkdown("test.md", content, config);

    TEST_ASSERT(!chunks.empty());
    // 验证块数大致正确
    // 100 行 * ~40 chars/line = 4000 chars ≈ 1000 tokens
    // chunk size 50 tokens, step 40 tokens → ~25 chunks
    std::cout << "  [PASS] testBasicChunking (" << chunks.size() << " chunks)\n";

    // 验证块的连续性
    for (size_t i = 1; i < chunks.size(); i++) {
        TEST_ASSERT(chunks[i].startLine >= chunks[i-1].startLine);
    }
    std::cout << "  [PASS] testChunkContinuity\n";
}

void testEmptyContent() {
    auto chunks = Chunker::chunkMarkdown("empty.md", "");
    TEST_ASSERT(chunks.empty());
    std::cout << "  [PASS] testEmptyContent\n";
}

void testSmallContent() {
    std::string content = "Hello world";
    auto chunks = Chunker::chunkMarkdown("small.md", content);
    TEST_ASSERT(chunks.size() == 1);
    if (!chunks.empty()) {
        TEST_ASSERT(chunks[0].text == "Hello world");
    }
    std::cout << "  [PASS] testSmallContent\n";
}

void testOverlap() {
    // TODO: 验证重叠是否正确
    // 创建内容，检查相邻块是否有重叠行
    std::cout << "  [SKIP] testOverlap (需要更细致的验证)\n";
}

int run_chunker_tests() {
    std::cout << "Chunker Tests:\n";
    RESET_FAILURES();
    testBasicChunking();
    testEmptyContent();
    testSmallContent();
    testOverlap();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All chunker tests passed.\n";
    else std::cout << f << " chunker test(s) failed.\n";
    return f;
}
