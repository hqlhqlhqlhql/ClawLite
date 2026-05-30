#pragma once
// ClawLite — 向量嵌入接口
// 参考：openclaw-main/packages/memory-host-sdk/host/embeddings.types.ts — EmbeddingProvider
// 设计模式：策略模式 — 可插拔不同嵌入实现
//
// 简化实现：提供一个本地模拟嵌入（随机向量/TF-IDF）和一个远程 API 调用接口

#include <vector>
#include <string>

namespace clawlite {

class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;

    // 嵌入单条查询文本
    // 参考：openclaw-main/packages/memory-host-sdk/host/embeddings.types.ts:embedQuery
    virtual std::vector<double> embedQuery(const std::string& text) = 0;

    // 批量嵌入多条文本
    // 参考：openclaw-main/packages/memory-host-sdk/host/embeddings.types.ts:embedBatch
    virtual std::vector<std::vector<double>> embedBatch(
        const std::vector<std::string>& texts
    ) = 0;

    // 嵌入维度
    virtual int dimensions() const = 0;

    // 提供者标识
    virtual std::string id() const = 0;
};

// ── 本地模拟嵌入 ──────────────────────────────────────────
// 基于字符频率的简单向量化（用于离线测试）
// 不是真正的语义嵌入，但足以演示向量搜索流程

class LocalMockEmbedding : public IEmbeddingProvider {
public:
    explicit LocalMockEmbedding(int dims = 128);

    std::vector<double> embedQuery(const std::string& text) override;
    std::vector<std::vector<double>> embedBatch(
        const std::vector<std::string>& texts
    ) override;
    int dimensions() const override;
    std::string id() const override;

private:
    int m_dims;

    // 将文本转为固定维度向量（基于字符频率 + hash）
    std::vector<double> textToVector(const std::string& text);
};

// ── 远程嵌入（OpenAI 兼容 API）────────────────────────────
// 参考：openclaw-main/packages/memory-host-sdk/host/embeddings-remote-provider.ts
// 兼容：OpenAI / Ollama / LM Studio / llama-server
// API 格式：POST {baseUrl}/embeddings, 请求 {model, input}, 响应 {data: [{embedding}]}

class RemoteEmbedding : public IEmbeddingProvider {
public:
    // baseUrl: 如 "http://localhost:11434/v1"（Ollama）或 "https://api.openai.com/v1"
    // model: 如 "nomic-embed-text" 或 "text-embedding-3-small"
    // apiKey: OpenAI 需要，Ollama/LM Studio 不需要
    RemoteEmbedding(const std::string& baseUrl,
                    const std::string& model,
                    const std::string& apiKey = "");

    std::vector<double> embedQuery(const std::string& text) override;
    std::vector<std::vector<double>> embedBatch(
        const std::vector<std::string>& texts
    ) override;
    int dimensions() const override;
    std::string id() const override;

private:
    std::string m_baseUrl;
    std::string m_model;
    std::string m_apiKey;
    int m_dims = 0;

    // 调用 API 并解析响应
    std::vector<std::vector<double>> callApi(const std::vector<std::string>& inputs);
};

// ── 余弦相似度计算 ────────────────────────────────────────
// 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:cosineSimilarity
// 算法：dot(a,b) / (||a|| * ||b||)
// 这是数据结构/线性代数课程重点！

double cosineSimilarity(const std::vector<double>& a, const std::vector<double>& b);

} // namespace clawlite
