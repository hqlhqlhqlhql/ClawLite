// ClawLite — FileStateCache 测试
// 数据结构：双向链表 + HashMap，O(1) get/put

#include "memory/file_state_cache.h"
#include "test_helpers.h"
#include <iostream>

using namespace clawlite;

void testFileStateCacheBasic() {
    FileStateCache cache(4);  // 容量 4

    // 测试 put 和 get
    cache.put("file1.txt", "content1", 1000);
    cache.put("file2.txt", "content2", 2000);
    cache.put("file3.txt", "content3", 3000);

    auto r1 = cache.get("file1.txt");
    TEST_ASSERT(r1.has_value());
    TEST_ASSERT(r1.value() == "content1");

    auto r2 = cache.get("file2.txt");
    TEST_ASSERT(r2.has_value());
    TEST_ASSERT(r2.value() == "content2");

    std::cout << "  [PASS] testFileStateCacheBasic\n";
}

void testFileStateCacheLRU() {
    FileStateCache cache(2);  // 容量 2

    // 插入 2 个，满了
    cache.put("a.txt", "aaa", 100);
    cache.put("b.txt", "bbb", 200);
    TEST_ASSERT(cache.cacheSize() == 2);

    // 访问 a，使其移到队首
    cache.get("a.txt");

    // 插入 c，淘汰 b（最久未用）
    cache.put("c.txt", "ccc", 300);
    TEST_ASSERT(cache.cacheSize() == 2);

    // a 应该还在
    auto ra = cache.get("a.txt");
    TEST_ASSERT(ra.has_value());
    TEST_ASSERT(ra.value() == "aaa");

    // b 应该被淘汰
    auto rb = cache.get("b.txt");
    TEST_ASSERT(!rb.has_value());

    // c 应该在
    auto rc = cache.get("c.txt");
    TEST_ASSERT(rc.has_value());
    TEST_ASSERT(rc.value() == "ccc");

    std::cout << "  [PASS] testFileStateCacheLRU\n";
}

void testFileStateCacheInvalidate() {
    FileStateCache cache(4);

    cache.put("file.txt", "old content", 1000);
    TEST_ASSERT(cache.get("file.txt").has_value());

    cache.invalidate("file.txt");
    TEST_ASSERT(!cache.get("file.txt").has_value());
    TEST_ASSERT(cache.cacheSize() == 0);

    std::cout << "  [PASS] testFileStateCacheInvalidate\n";
}

void testFileStateCacheHitRate() {
    FileStateCache cache(10);

    // 插入 5 个文件
    for (int i = 0; i < 5; i++) {
        std::string path = "file" + std::to_string(i) + ".txt";
        cache.put(path, "content" + std::to_string(i), 1000 + i);
    }

    // 访问 file0 多次（命中）
    for (int i = 0; i < 10; i++) {
        cache.get("file0.txt");
    }

    // 访问 file4 一次（命中）
    cache.get("file4.txt");

    // 访问不存在的文件（未命中）
    cache.get("nonexistent.txt");

    int total = cache.hitCount() + cache.missCount();
    TEST_ASSERT(total == 12);  // 10 + 1 hit + 1 miss
    TEST_ASSERT(cache.hitCount() == 11);
    TEST_ASSERT(cache.missCount() == 1);

    std::cout << "  [PASS] testFileStateCacheHitRate\n";
}

int run_file_state_cache_tests() {
    std::cout << "FileStateCache Tests:\n";
    RESET_FAILURES();
    testFileStateCacheBasic();
    testFileStateCacheLRU();
    testFileStateCacheInvalidate();
    testFileStateCacheHitRate();
    int f = GET_FAILURES();
    if (f == 0) std::cout << "All FileStateCache tests passed.\n";
    else std::cout << f << " FileStateCache test(s) failed.\n";
    return f;
}
