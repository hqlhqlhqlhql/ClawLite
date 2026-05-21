#pragma once
// ClawLite — 测试辅助宏
// 用 TEST_ASSERT 替代 assert，失败时不终止进程，只计数

#include <iostream>

inline int& _test_fail_count() {
    static int f = 0;
    return f;
}

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        std::cerr << "  [FAIL] " << #expr \
                  << "  (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        _test_fail_count()++; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    std::cout << "  [PASS] " << (name) << "\n"; \
} while(0)

#define TEST_SKIP(name, reason) do { \
    std::cout << "  [SKIP] " << (name) << " (" << (reason) << ")\n"; \
} while(0)

#define RESET_FAILURES() do { _test_fail_count() = 0; } while(0)
#define GET_FAILURES() _test_fail_count()
