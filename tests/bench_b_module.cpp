// ClawLite — B 模块 Benchmark
// 三张对比图的数据生成：
//   1. FileStateCache 命中率 vs 缓存容量
//   2. 压缩稳定性曲线（50 轮对话 token 占用）
//   3. FTS5 性能 vs 文档数

#include "memory/file_state_cache.h"
#include "memory/search_manager.h"
#include "memory/memory_store.h"
#include "memory/session_store.h"
#include "memory/compaction.h"
#include "core/types.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>

using namespace clawlite;

// 简单的 JSON 输出辅助
static void printJsonHeader() {
    std::cout << "{\n";
}
static void printJsonFooter() {
    std::cout << "}\n";
}

// ── Benchmark 1: FileStateCache 命中率 vs 缓存容量 ─────────
// 20 个文件，zipf 分布，1000 次随机访问
// 容量 = 1/2/4/8/16/32/64

static void benchFileStateCacheHitRate() {
    const int NUM_FILES = 20;
    const int NUM_REQUESTS = 1000;
    const std::vector<int> CACHE_SIZES = {1, 2, 4, 8, 16, 32, 64};

    std::mt19937 rng(42);

    std::cout << "  \"file_state_cache_hit_rate\": [\n";

    for (size_t ci = 0; ci < CACHE_SIZES.size(); ci++) {
        int capacity = CACHE_SIZES[ci];
        FileStateCache cache(capacity);

        // 生成请求序列（Zipf-like：低编号文件访问频率高）
        std::vector<int> requests(NUM_REQUESTS);
        for (int i = 0; i < NUM_REQUESTS; i++) {
            double r = std::uniform_real_distribution<double>(0.0001, 1.0)(rng);
            int fileId = std::min(NUM_FILES - 1, (int)(1.0 / r) - 1);
            requests[i] = std::max(0, fileId);
        }

        // 预填充：先插入所有文件
        for (int f = 0; f < NUM_FILES; f++) {
            std::string path = "file" + std::to_string(f) + ".txt";
            std::string content = "content_of_file_" + std::to_string(f);
            cache.put(path, content, 1000 + f);
        }

        // 执行请求序列
        for (int req : requests) {
            std::string path = "file" + std::to_string(req) + ".txt";
            auto result = cache.get(path);
            if (!result.has_value()) {
                // 未命中，重新填充
                std::string content = "content_of_file_" + std::to_string(req);
                cache.put(path, content, 1000 + req);
            }
        }

        int total = cache.hitCount() + cache.missCount();
        double hitRate = total > 0 ? 100.0 * cache.hitCount() / total : 0;

        std::cout << "    {\"capacity\":" << capacity
                  << ", \"hit_rate\":" << hitRate
                  << ", \"hits\":" << cache.hitCount()
                  << ", \"misses\":" << cache.missCount()
                  << ", \"cache_size\":" << cache.cacheSize() << "}";
        if (ci < CACHE_SIZES.size() - 1) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ],\n";
}

// ── Benchmark 2: 压缩稳定性曲线（50 轮对话 token 占用）─────
// 模拟 50 轮对话，每轮 user + assistant
// 每 10 轮触发一次压缩，观察 token 占用曲线

static void benchCompressionStability() {
    const int NUM_TURNS = 50;
    const int COMPACT_INTERVAL = 10;  // 每 10 轮压缩一次
    const int TARGET_TOKENS = 1500;

    std::mt19937 rng(789);
    std::uniform_int_distribution<int> msgLenDist(50, 200);

    std::vector<std::pair<std::string, CompactionStrategy>> strategies = {
        {"truncate_only", CompactionStrategy::TruncateOnly},
        {"greedy_summary", CompactionStrategy::GreedySummary}
    };

    std::cout << "  \"compression_stability\": [\n";

    for (size_t si = 0; si < strategies.size(); si++) {
        const auto& [name, strategy] = strategies[si];

        std::cout << "    {\"strategy\":\"" << name << "\", \"turns\": [\n";

        std::vector<Message> messages;

        for (int turn = 0; turn < NUM_TURNS; turn++) {
            // 生成 user 消息
            std::string userMsg = "Turn " + std::to_string(turn) + ": ";
            int userLen = msgLenDist(rng);
            for (int i = 0; i < userLen; i++) userMsg += "a";

            Message umsg;
            umsg.role = Role::User;
            umsg.content = userMsg;
            messages.push_back(umsg);

            // 生成 assistant 消息
            std::string asstMsg = "Response to turn " + std::to_string(turn) + ": ";
            int asstLen = msgLenDist(rng);
            for (int i = 0; i < asstLen; i++) asstMsg += "b";

            Message amsg;
            amsg.role = Role::Assistant;
            amsg.content = asstMsg;
            messages.push_back(amsg);

            // 计算当前 token 数
            int currentTokens = Compactor::countTotalTokens(messages);

            // 每 COMPACT_INTERVAL 轮压缩一次
            bool compacted = false;
            if ((turn + 1) % COMPACT_INTERVAL == 0 && currentTokens > TARGET_TOKENS) {
                CompactionConfig config;
                config.targetTokens = TARGET_TOKENS;
                config.keepTurns = 6;
                config.strategy = strategy;

                auto result = Compactor::compact(messages, config);
                compacted = result.compacted;
                currentTokens = Compactor::countTotalTokens(messages);
            }

            std::cout << "      {\"turn\":" << turn
                      << ", \"tokens\":" << currentTokens
                      << ", \"compacted\":" << (compacted ? "true" : "false")
                      << "}";
            if (turn < NUM_TURNS - 1) std::cout << ",";
            std::cout << "\n";
        }

        std::cout << "    ]}";
        if (si < strategies.size() - 1) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "  ],\n";
}

// ── Benchmark 3: FTS5 性能 vs 文档数 ─────────────────────

static void benchFts5Performance() {
    const int WORDS_PER_DOC = 50;
    const int VOCAB_SIZE = 2000;
    const std::vector<int> DOC_COUNTS = {100, 500, 1000, 5000, 10000};
    const int TOP_K = 5;
    const int WARMUP = 3;
    const int REPEATS = 20;

    std::mt19937 rng(42);
    std::vector<std::string> vocab;
    for (int i = 0; i < VOCAB_SIZE; i++) {
        vocab.push_back("term_" + std::to_string(i));
    }

    std::cout << "  \"fts5_performance\": [\n";

    for (size_t di = 0; di < DOC_COUNTS.size(); di++) {
        int N = DOC_COUNTS[di];

        MemoryStore store;
        store.open(":memory:");

        SearchManager searchMgr(store);

        // 生成文档并插入
        std::uniform_int_distribution<int> termDist(0, VOCAB_SIZE - 1);
        for (int d = 0; d < N; d++) {
            std::ostringstream doc;
            for (int w = 0; w < WORDS_PER_DOC; w++) {
                if (w > 0) doc << " ";
                doc << vocab[termDist(rng)];
            }

            MemoryChunk chunk;
            chunk.hash = "doc_" + std::to_string(d);
            chunk.path = "benchmark.md";
            chunk.startLine = d;
            chunk.endLine = d;
            chunk.text = doc.str();
            store.upsertChunk(chunk);
        }

        // 测试查询
        std::vector<std::string> queries = {
            "term_42 term_100",
            "term_7 term_777",
            "term_1 term_2 term_3"
        };

        double totalMs = 0;
        int queryCount = 0;

        for (const auto& q : queries) {
            // 预热
            for (int w = 0; w < WARMUP; w++) {
                searchMgr.ftsSearch(q, TOP_K);
            }

            // 计时
            for (int r = 0; r < REPEATS; r++) {
                auto t0 = std::chrono::high_resolution_clock::now();
                auto results = searchMgr.ftsSearch(q, TOP_K);
                auto t1 = std::chrono::high_resolution_clock::now();
                totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
                queryCount++;
            }
        }

        double avgMs = totalMs / queryCount;

        std::cout << "    {\"N\":" << N << ", \"avg_ms\":" << avgMs << "}";
        if (di < DOC_COUNTS.size() - 1) std::cout << ",";
        std::cout << "\n";

        store.close();
    }
    std::cout << "  ]\n";
}

int main() {
    printJsonHeader();
    benchFileStateCacheHitRate();
    benchCompressionStability();
    benchFts5Performance();
    printJsonFooter();
    return 0;
}
