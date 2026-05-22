// ClawLite — RemoteEmbedding 快速测试
// 需要 llama-server 在 http://127.0.0.1:8080 运行并启用 --embeddings

#include "memory/embedding.h"
#include <iostream>
#include <cmath>

using namespace clawlite;

int main() {
    RemoteEmbedding emb("http://127.0.0.1:8080/v1", "test");

    std::cout << "Testing RemoteEmbedding...\n";
    std::cout << "Provider: " << emb.id() << "\n";

    // 测试单条嵌入
    auto vec = emb.embedQuery("hello world");
    std::cout << "embedQuery dimensions: " << vec.size() << "\n";
    if (vec.empty()) {
        std::cout << "FAIL: embedQuery returned empty\n";
        return 1;
    }

    // 检查 L2 范数（应该接近 1.0，如果服务端做了归一化）
    double norm = 0;
    for (double v : vec) norm += v * v;
    norm = std::sqrt(norm);
    std::cout << "L2 norm: " << norm << "\n";

    // 测试语义相似度
    auto vec1 = emb.embedQuery("机器学习");
    auto vec2 = emb.embedQuery("人工智能");
    auto vec3 = emb.embedQuery("今天天气不错");

    double sim_12 = cosineSimilarity(vec1, vec2);
    double sim_13 = cosineSimilarity(vec1, vec3);
    std::cout << "similarity(机器学习, 人工智能) = " << sim_12 << "\n";
    std::cout << "similarity(机器学习, 天气)     = " << sim_13 << "\n";
    std::cout << "语义相关 > 不相关: " << (sim_12 > sim_13 ? "PASS" : "FAIL") << "\n";

    // 测试批量嵌入
    auto batch = emb.embedBatch({"hello", "world", "test"});
    std::cout << "embedBatch size: " << batch.size() << "\n";
    std::cout << "dimensions: " << emb.dimensions() << "\n";

    std::cout << "All tests done.\n";
    return 0;
}
