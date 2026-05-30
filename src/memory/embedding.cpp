// ClawLite — 向量嵌入实现
// TODO: B 同学实现

#include "memory/embedding.h"
#include "httplib.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <functional>
#include <sstream>

namespace clawlite {

// ── LocalMockEmbedding ────────────────────────────────────

LocalMockEmbedding::LocalMockEmbedding(int dims) : m_dims(dims) {}

std::vector<double> LocalMockEmbedding::embedQuery(const std::string& text) {
    return textToVector(text);
}

std::vector<std::vector<double>> LocalMockEmbedding::embedBatch(
    const std::vector<std::string>& texts
) {
    std::vector<std::vector<double>> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        results.push_back(textToVector(text));
    }
    return results;
}

int LocalMockEmbedding::dimensions() const { return m_dims; }
std::string LocalMockEmbedding::id() const { return "local-mock"; }

std::vector<double> LocalMockEmbedding::textToVector(const std::string& text) {
    // TODO: 实现 — 基于字符频率的简单向量化
    //
    // 算法：
    //   1. 初始化 m_dims 维零向量
    //   2. 遍历文本的每个字符
    //   3. 用 hash(ch) % m_dims 确定位置
    //   4. 在该位置累加
    //   5. L2 归一化
    //
    // 这不是真正的语义嵌入，但足以演示向量搜索流程
    // 真正的实现应该调用 OpenAI / 本地模型的 embedding API

    std::vector<double> vec(m_dims, 0.0);

    for (size_t i = 0; i < text.size(); i++) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        int idx = (ch * 31 + static_cast<int>(i)) % m_dims;
        if (idx < 0) idx += m_dims;
        vec[idx] += 1.0;
    }

    // L2 归一化
    double norm = 0.0;
    for (double v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0) {
        for (double& v : vec) v /= norm;
    }

    return vec;
}

// ── RemoteEmbedding（OpenAI 兼容 API）─────────────────────
// 参考：openclaw-main/packages/memory-host-sdk/host/embeddings-remote-provider.ts
// 参考：openclaw-main/packages/memory-host-sdk/host/embeddings-remote-fetch.ts

// 简易 JSON 解析：从响应中提取 "embedding":[[...], [...]]
// 参考 openclaw 的 parse 回调：data.map(entry => entry.embedding)
static std::vector<std::vector<double>> parseEmbeddingResponse(const std::string& json) {
    std::vector<std::vector<double>> results;
    size_t pos = 0;

    // 查找所有 "embedding" : [...] 块
    while (pos < json.size()) {
        // 找 "embedding"
        size_t keyPos = json.find("\"embedding\"", pos);
        if (keyPos == std::string::npos) break;

        // 找对应的 '['
        size_t arrStart = json.find('[', keyPos);
        if (arrStart == std::string::npos) break;

        // 找匹配的 ']'（处理嵌套数组）
        int depth = 1;
        size_t arrEnd = arrStart + 1;
        while (arrEnd < json.size() && depth > 0) {
            if (json[arrEnd] == '[') depth++;
            else if (json[arrEnd] == ']') depth--;
            arrEnd++;
        }

        // 解析数组内的数字
        std::vector<double> embedding;
        std::string arrStr = json.substr(arrStart + 1, arrEnd - arrStart - 2);
        std::istringstream ss(arrStr);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // 去掉空格
            size_t start = token.find_first_not_of(" \t\n\r");
            if (start != std::string::npos) {
                try {
                    embedding.push_back(std::stod(token.substr(start)));
                } catch (...) {}
            }
        }

        if (!embedding.empty()) {
            results.push_back(std::move(embedding));
        }
        pos = arrEnd;
    }

    return results;
}

RemoteEmbedding::RemoteEmbedding(const std::string& baseUrl,
                                  const std::string& model,
                                  const std::string& apiKey)
    : m_baseUrl(baseUrl), m_model(model), m_apiKey(apiKey) {}

std::vector<double> RemoteEmbedding::embedQuery(const std::string& text) {
    // 参考 openclaw：embedQuery 把单条文本包成数组发送
    auto results = callApi({text});
    if (!results.empty()) return results[0];
    return {};
}

std::vector<std::vector<double>> RemoteEmbedding::embedBatch(
    const std::vector<std::string>& texts
) {
    return callApi(texts);
}

int RemoteEmbedding::dimensions() const { return m_dims; }
std::string RemoteEmbedding::id() const { return "remote-" + m_model; }

std::vector<std::vector<double>> RemoteEmbedding::callApi(
    const std::vector<std::string>& inputs
) {
    // 参考：openclaw-main/packages/memory-host-sdk/host/embeddings-remote-provider.ts
    // POST {baseUrl}/embeddings, body: {model, input}
    std::vector<std::vector<double>> results;
    if (inputs.empty()) return results;

    // 构造请求体 JSON
    // {"model":"...","input":["text1","text2"]}
    std::ostringstream body;
    body << "{\"model\":\"" << m_model << "\",\"input\":[";
    for (size_t i = 0; i < inputs.size(); i++) {
        if (i > 0) body << ",";
        body << "\"";
        // 转义 JSON 特殊字符
        for (char c : inputs[i]) {
            if (c == '"') body << "\\\"";
            else if (c == '\\') body << "\\\\";
            else if (c == '\n') body << "\\n";
            else if (c == '\r') body << "\\r";
            else if (c == '\t') body << "\\t";
            else body << c;
        }
        body << "\"";
    }
    body << "]}";

    // 解析 baseUrl，分离 host 和路径前缀
    std::string host;
    std::string basePath;
    std::string url = m_baseUrl;

    // 去掉协议前缀
    if (url.substr(0, 8) == "https://") url = url.substr(8);
    else if (url.substr(0, 7) == "http://") url = url.substr(7);

    // 分离 host 和 path
    size_t slashPos = url.find('/');
    if (slashPos != std::string::npos) {
        host = url.substr(0, slashPos);
        basePath = url.substr(slashPos);
    } else {
        host = url;
        basePath = "";
    }

    // 发送请求（本地服务用 HTTP，不需要 SSL）
    httplib::Client cli(host);
    cli.set_connection_timeout(60);
    cli.set_read_timeout(60);

    httplib::Headers headers = {{"Content-Type", "application/json"}};
    if (!m_apiKey.empty()) {
        headers.emplace("Authorization", "Bearer " + m_apiKey);
    }

    std::string endpoint = basePath + "/embeddings";
    auto res = cli.Post(endpoint, headers, body.str(), "application/json");

    if (!res) return results;
    if (res->status != 200) return results;

    // 解析响应
    results = parseEmbeddingResponse(res->body);

    // 记录维度（首次成功请求后）
    if (m_dims == 0 && !results.empty() && !results[0].empty()) {
        m_dims = static_cast<int>(results[0].size());
    }

    return results;
}

// ── 余弦相似度 ────────────────────────────────────────────

double cosineSimilarity(const std::vector<double>& a, const std::vector<double>& b) {
    // TODO: 实现 — 数据/线性代数课程重点！
    //
    // 参考：openclaw-main/packages/memory-host-sdk/host/internal.ts:cosineSimilarity
    //
    // 公式：cos(a, b) = dot(a, b) / (||a|| * ||b||)
    //
    // 实现：
    //   if (a.size() != b.size()) return 0.0;
    //   double dot = 0, normA = 0, normB = 0;
    //   for (int i = 0; i < a.size(); i++) {
    //       dot   += a[i] * b[i];
    //       normA += a[i] * a[i];
    //       normB += b[i] * b[i];
    //   }
    //   if (normA == 0 || normB == 0) return 0.0;
    //   return dot / (sqrt(normA) * sqrt(normB));

    if (a.size() != b.size() || a.empty()) return 0.0;

    double dot = 0.0, normA = 0.0, normB = 0.0;
    for (size_t i = 0; i < a.size(); i++) {
        dot   += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    if (normA == 0.0 || normB == 0.0) return 0.0;
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

} // namespace clawlite
