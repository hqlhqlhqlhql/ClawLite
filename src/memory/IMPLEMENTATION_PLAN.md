# Module B: Context Memory System — 实现方案

## 一、总体架构

```
context_engine (组装层，对外统一接口)
 ├── memory_store   (SQLite 持久化，所有数据的底层)
 ├── chunker        (Markdown 滑动窗口分块)
 ├── embedding      (文本→向量，策略模式可插拔)
 ├── search_manager (向量余弦 + FTS5 全文，混合检索)
 ├── session_store  (会话历史，HashMap 索引)
 └── compaction     (上下文压缩，截断+摘要)
```

数据流：
```
文件 → chunker 分块 → embedding 转向量 → memory_store 存储
                                               ↓
用户查询 → search_manager（向量+全文）→ memory_store 读取 → 返回结果
对话消息 → session_store 存储 → compaction 压缩 → context_engine 组装
```

---

## 二、实现顺序与详细步骤

### 第 1 步：embedding.cpp（已有基础，最快完成）

**当前状态**：`textToVector()` 和 `cosineSimilarity()` 已实现，完成度最高。

**需要做的**：无。直接跳过，进入下一步。

**预估时间**：0.5h（仅验证编译通过）

---

### 第 2 步：session_store.cpp — 补完 parseSessionKey

**当前状态**：`createSession`、`appendMessage`、`getHistory`、`buildSessionKey` 已实现。只有 `parseSessionKey()` 的字段解析是 TODO。

**实现内容**：

```cpp
// parseSessionKey() — 取消注释并实现
// 格式：agent:<agentId>:<channel>:<peerKind>:<peerId>[:thread:<threadId>]
// 算法：按 ':' 分割，逐段提取
SessionKeyInfo info;
info.rawKey = key;
// ... 按 ':' 分割 ...
if (parts.size() >= 1 && parts[0] == "agent") {
    if (parts.size() >= 2) info.agentId = parts[1];
    if (parts.size() >= 3) info.channel = parts[2];
    if (parts.size() >= 4) info.peerKind = parts[3];
    if (parts.size() >= 5) info.peerId = parts[4];
    // 查找 "thread:" 标记
    for (size_t i = 5; i < parts.size() - 1; i++) {
        if (parts[i] == "thread") {
            info.threadId = parts[i + 1];
            break;
        }
    }
}
```

**预估时间**：0.5h

---

### 第 3 步：memory_store.cpp — 工作量最大，核心基础

**当前状态**：全空，所有函数都是 TODO。SQL 语句已写在注释里。

**参考**：`openclaw-main/packages/memory-host-sdk/host/memory-schema.ts`

**实现顺序（函数级别）**：

#### 3.1 基础连接与表创建
- `open()` → `sqlite3_open()` + WAL 模式 + 调用 `createSchema()` + `createFtsTable()`
- `close()` → `sqlite3_close()`
- `createSchema()` → 4 张表（meta / files / chunks / embedding_cache）+ 2 个索引
- `createFtsTable()` → FTS5 虚拟表（需启用 `SQLITE_ENABLE_FTS5`，CMakeLists.txt 已配置）

#### 3.2 文件索引 CRUD
- `upsertFile()` → `INSERT OR REPLACE INTO files`
- `removeFile()` → `DELETE FROM files WHERE path = ?`
- `getFile()` → `SELECT ... FROM files WHERE path = ?`
- `getAllFiles()` → `SELECT ... FROM files`

#### 3.3 块存储 CRUD
- `upsertChunk()` → `INSERT OR REPLACE INTO chunks` + 同步 FTS5 索引
- `removeChunksByFile()` → `DELETE FROM chunks WHERE path = ?` + `DELETE FROM chunks_fts WHERE path = ?`
- `getAllChunks()` → `SELECT ... FROM chunks`
- `getChunksByFile()` → `SELECT ... FROM chunks WHERE path = ?`

**关键细节**：
- `embedding` 列存 JSON 字符串 `"[0.1, 0.2, ...]"`，读取时解析回 `vector<double>`
- `upsertChunk()` 需要同时写 `chunks` 表和 `chunks_fts` 虚拟表，保持同步
- 使用 `beginTransaction()` / `commit()` 包裹批量操作

#### 3.4 FTS5 全文搜索
- `ftsSearch()` → `SELECT id, bm25(chunks_fts) as score FROM chunks_fts WHERE chunks_fts MATCH ? ORDER BY score LIMIT ?`
- BM25 分数在 SQLite FTS5 中越小越好（负数），需要取反
- query 需要转义 FTS5 特殊字符（`"`、`*`、`-`、`OR`、`AND`、`NOT`）

#### 3.5 嵌入缓存
- `cacheEmbedding()` → `INSERT OR REPLACE INTO embedding_cache`
- `getCachedEmbedding()` → `SELECT embedding FROM embedding_cache WHERE provider=? AND model=? AND hash=?`

#### 3.6 元数据与事务
- `setMeta()` / `getMeta()` → 简单 KV 操作
- `beginTransaction()` / `commit()` / `rollback()` → `sqlite3_exec()`
- `fileCount()` / `chunkCount()` → `SELECT COUNT(*)`

#### 3.7 辅助：SQLite C API 封装技巧

建议写一个私有辅助函数 `execSql()` 简化重复的 `sqlite3_prepare_v2` / `sqlite3_step` / `sqlite3_finalize` 调用：

```cpp
// 辅助：执行无返回值的 SQL
bool execSql(const char* sql);
// 辅助：执行带参数的 prepared statement
bool execPrepared(const char* sql, std::function<void(sqlite3_stmt*)> bind);
```

**预估时间**：4-6h

---

### 第 4 步：chunker.cpp — 取消注释，补完逻辑

**当前状态**：伪代码已写在注释里，`estimateTokens()` 和 `computeHash()` 已实现。

**参考**：`openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown`

**实现内容**：取消 `chunkMarkdown()` 中注释掉的滑动窗口代码，并补充 CJK 处理：

```cpp
int start = 0;
while (start < (int)lines.size()) {
    int end = start;
    int charCount = 0;
    // 扩展窗口右边界
    while (end < (int)lines.size() && charCount < maxChars) {
        charCount += (int)lines[end].size() + 1;
        end++;
    }
    // 拼接文本
    std::string text;
    for (int i = start; i < end; i++) {
        if (i > start) text += '\n';
        text += lines[i];
    }
    MemoryChunk chunk;
    chunk.path = filePath;
    chunk.startLine = start + 1;  // 1-indexed
    chunk.endLine = end;
    chunk.text = text;
    chunk.hash = computeHash(text);
    chunks.push_back(chunk);
    // 滑动窗口前进
    int stepUsed = 0;
    while (stepUsed < stepChars && start < end) {
        stepUsed += (int)lines[start].size() + 1;
        start++;
    }
}
```

**CJK 文本处理**（参考 openclaw 的两阶段分割）：
- 如果单行超过 `maxChars`，需要在行内切割
- 粗切：在 `maxChars` 处切
- 细切：检查是否切在 UTF-8 多字节字符中间，向后调整到字符边界

**预估时间**：2-3h

---

### 第 5 步：search_manager.cpp — 混合检索核心

**当前状态**：`search()` 主流程和 `normalizeScores()` 已实现。`vectorSearch()`、`ftsSearch()`、`mergeResults()` 为空。

**参考**：`openclaw-main/packages/memory-host-sdk/host/types.ts:search`

**实现顺序**：

#### 5.1 vectorSearch()
```cpp
// 1. queryEmbedding = m_embedding->embedQuery(query)
// 2. allChunks = m_store.getAllChunks()
// 3. 对每个 chunk，计算 chunk.embedding 与 queryEmbedding 的余弦相似度
// 4. 按 score 降序排序，取 top-k
```

**关键**：chunk 的 embedding 从 `MemoryChunk.embedding` 取（`memory_store` 读取时已解析 JSON）。如果 chunk 没有 embedding，跳过。

#### 5.2 ftsSearch()
```cpp
// 1. ftsResults = m_store.ftsSearch(query, topK)  → 返回 (chunkId, bm25Score) 列表
// 2. 对每个 chunkId，从 m_store 获取完整 MemoryChunk
// 3. 构造 SearchResult（textScore = BM25 分数归一化后的值）
```

#### 5.3 mergeResults()
```cpp
// 1. 对两个列表分别 normalizeScores()
// 2. 用 unordered_map<string, double> 按 "path:startLine" 去重
// 3. 向量结果：score = vectorWeight * normalizedScore
// 4. 全文结果：score = textWeight * normalizedScore
// 5. 同一 chunk 在两个列表都出现 → 分数相加
// 6. 返回合并列表
```

**预估时间**：3-4h

---

### 第 6 步：compaction.cpp — 已基本实现

**当前状态**：`compact()`、`countTotalTokens()`、`generateSummary()` 都已写好。

**需要做的**：验证编译通过，补充边界测试（空消息列表、单条消息、全部是 system 消息）。

**预估时间**：0.5h

---

### 第 7 步：context_engine.cpp — 组装层，最后做

**当前状态**：全空，只有注释里的伪代码。

**实现内容**：组合上面 6 个组件，实现 `IContextEngine` 接口。

#### 7.1 成员变量
```cpp
class DefaultContextEngine : public IContextEngine {
    MemoryStore m_store;
    std::unique_ptr<IEmbeddingProvider> m_embedding;
    std::unique_ptr<SearchManager> m_search;
    SessionStore m_sessions;
    std::string m_sessionKey;  // 当前会话键
};
```

#### 7.2 各方法实现
- `initialize(dataDir)` → `m_store.open(dataDir + "/clawlite.db")` + 创建 SearchManager
- `indexFile(filePath)` → 读文件 → `Chunker::chunkMarkdown()` → `m_embedding->embedBatch()` → `m_store.upsertFile()` + `m_store.upsertChunk()`（逐块）
- `removeFile(filePath)` → `m_store.removeFile()` + `m_store.removeChunksByFile()`
- `needsReindex(filePath)` → 对比 mtime/hash 与 `m_store.getFile()`
- `ingest(msg)` → `m_sessions.appendMessage(m_sessionKey, msg)`
- `ingestBatch(messages)` → 循环调用 `ingest()`
- `assemble(tokenBudget)` → 取最近对话 + 搜索相关记忆 + 合并 + 裁剪到预算
- `compact(targetTokens)` → `Compactor::compact()`
- `search(query, topK)` → `m_search->search(query)`
- `createSession(key)` → `m_sessions.createSession(key)`
- `getSessionHistory(key, maxTurns)` → `m_sessions.getHistory(key, maxTurns)`

#### 7.3 assemble() 的详细逻辑（核心方法）
```
1. history = m_sessions.getHistory(m_sessionKey, 20)
2. historyTokens = countTotalTokens(history)
3. remainingBudget = tokenBudget - historyTokens
4. if (remainingBudget > 200):
       query = 从最近用户消息提取关键词
       memResults = m_search->search(query, 5)
       把 memResults.snippet 转为 system 消息
       合并到 history 前面
5. 裁剪到 tokenBudget
```

**预估时间**：3-4h

---

## 三、实现时间线总览

| 步骤 | 文件 | 内容 | 预估 |
|------|------|------|------|
| 1 | embedding.cpp | 无需改动 | 0.5h |
| 2 | session_store.cpp | 补完 parseSessionKey | 0.5h |
| 3 | memory_store.cpp | SQLite 全部实现 | 4-6h |
| 4 | chunker.cpp | 滑动窗口算法 | 2-3h |
| 5 | search_manager.cpp | 混合检索 | 3-4h |
| 6 | compaction.cpp | 验证+补测 | 0.5h |
| 7 | context_engine.cpp | 组装层 | 3-4h |
| **合计** | | | **14-19h** |

**并行策略**：步骤 3（memory_store）和步骤 4（chunker）可以并行，因为 chunker 不依赖 SQLite。

---

## 四、测试验收标准

### 4.1 单元测试（已有测试框架，补完用例）

#### memory_store 测试
| 测试用例 | 验证点 |
|----------|--------|
| `testOpenClose` | 打开内存数据库并关闭，不崩溃 |
| `testFileCrud` | 文件增删改查，fileCount 正确 |
| `testChunkCrud` | 块增删改查，chunkCount 正确 |
| `testFtsSearch` | 插入文本块后 FTS5 搜索能返回结果，BM25 分数排序正确 |
| `testEmbeddingCache` | 缓存写入后能读取，向量值一致 |
| `testTransaction` | begin → 插入 → rollback，数据不持久化 |
| `testBatchInsert` | 事务内批量插入 1000 块，性能 < 1s |
| `testEmbeddingJson` | embedding JSON 序列化/反序列化精度无损 |
| `testFtsSpecialChars` | 查询含 FTS5 特殊字符（引号、星号）不崩溃 |

#### chunker 测试
| 测试用例 | 验证点 |
|----------|--------|
| `testBasicChunking` | 100 行文本产生合理数量的块 |
| `testEmptyContent` | 空字符串返回 0 块 |
| `testSmallContent` | 短文本返回 1 块，内容正确 |
| `testOverlap` | 相邻块有重叠行，重叠行数符合配置 |
| `testChunkContinuity` | startLine 单调递增，无遗漏行 |
| `testCJKContent` | 中文文本分块不乱码，不切在字符中间 |
| `testSingleLongLine` | 单行超长文本（> maxChars）能正确切割 |
| `testHashConsistency` | 相同文本 hash 相同，不同文本 hash 不同 |

#### search_manager 测试
| 测试用例 | 验证点 |
|----------|--------|
| `testCosineSimilarity` | 相同向量 ≈ 1.0，正交向量 ≈ 0.0 |
| `testVectorSearch` | 插入数据后向量搜索返回结果 |
| `testFtsSearch` | 插入数据后全文搜索返回结果 |
| `testHybridSearch` | 混合搜索结果按加权分数排序 |
| `testMergeDedup` | 同一 chunk 在两个列表出现时合并而非重复 |
| `testNormalizeScores` | 归一化后最高分 = 1.0 |
| `testRelevanceOrder` | 搜索 "fox" 时包含 "fox" 的 chunk 排在前面 |

#### session_store 测试
| 测试用例 | 验证点 |
|----------|--------|
| `testSessionCrud` | 会话创建、消息追加、历史查询 |
| `testParseSessionKey` | 正确解析各字段 |
| `testBuildSessionKey` | 构建的 key 格式正确 |
| `testListByAgent` | 按 agentId 过滤会话 |
| `testGetHistoryMaxTurns` | maxTurns 参数限制返回轮数 |
| `testAutoCreateSession` | appendMessage 自动创建不存在的会话 |
| `testParseWithThread` | 带 thread: 的 key 正确解析 threadId |

#### compaction 测试
| 测试用例 | 验证点 |
|----------|--------|
| `testNoCompaction` | 消息未超限时不压缩 |
| `testCompaction` | 超限后消息数减少，保留最近 N 轮 |
| `testSummaryGenerated` | 压缩后产生非空摘要 |
| `testTokenReduction` | tokensAfter < tokensBefore |
| `testEmptyMessages` | 空消息列表不崩溃 |
| `testAllSystemMessages` | 全是 system 消息时不压缩 |

#### context_engine 集成测试
| 测试用例 | 验证点 |
|----------|--------|
| `testInitialize` | 初始化后数据库文件存在 |
| `testIndexFile` | 索引文件后 fileCount、chunkCount > 0 |
| `testSearchAfterIndex` | 索引后搜索能返回结果 |
| `testIngestAndAssemble` | 写入消息后 assemble 返回合理上下文 |
| `testCompactIntegration` | 写入大量消息后 compact 能减少 token |
| `testReindexDetection` | 修改文件后 needsReindex 返回 true |

### 4.2 集成验收标准

1. **编译通过**：`cmake --build .` 无错误无警告
2. **测试全绿**：`./clawlite_tests` 全部 PASS，0 failures
3. **端到端流程**：索引一个 Markdown 文件 → 搜索关键词 → 返回相关块
4. **会话流程**：创建会话 → 追加对话 → 获取历史 → 压缩 → 历史被摘要替代

### 4.3 答辩演示重点（数据结构课程）

答辩时重点展示以下数据结构/算法的运行过程：

1. **滑动窗口分块**（chunker）—— 用动画或日志展示窗口滑动过程
2. **余弦相似度计算**（embedding）—— 展示向量点积和归一化
3. **FTS5 倒排索引**（memory_store）—— 展示 BM25 排序结果
4. **混合检索合并**（search_manager）—— 展示两种搜索结果的加权合并
5. **优先队列/top-k 排序**（search_manager）—— 展示排序过程

---

## 五、已知风险与注意事项

### 5.1 SQLite FTS5 编译问题
- CMakeLists.txt 已配置 `SQLITE_ENABLE_FTS5`，确保 `sqlite3.c` 编译时生效
- 如果 FTS5 不可用，需要 graceful degrade（参考 openclaw 的 try/catch 做法）
- 验证方法：编译后运行 `SELECT fts5()` 不报错即表示 FTS5 已启用

### 5.2 embedding JSON 序列化精度
- `vector<double>` → JSON 字符串 → `vector<double>` 的往返过程可能有精度损失
- 建议保留 6 位小数（`%.6f`），对余弦相似度计算影响可忽略
- 参考 openclaw 的 `parseEmbedding()` 做容错处理

### 5.3 CJK 文本分块
- 中文文本没有空格分隔，按字符数估算 token 更准确
- 切割时必须检查 UTF-8 字符边界（不能切在多字节字符中间）
- 参考 openclaw 的两阶段分割算法

### 5.4 线程安全
- 当前设计是单线程，不需要考虑并发
- 如果后续需要多线程访问 MemoryStore，需要加互斥锁（SQLite 的 WAL 模式支持并发读）

### 5.5 向量搜索性能
- 当前实现是暴力遍历（O(n)），适合课程作业规模
- 如果数据量大（>10000 块），需要考虑 ANN 索引（见优化方向）

---

## 六、后续优化方向

### 6.1 接入真实嵌入模型（优先级最高）

当前 `LocalMockEmbedding` 是基于字符频率的模拟，无法做真正的语义搜索。接入 llama.cpp：

```cpp
class LlamaCppEmbedding : public IEmbeddingProvider {
public:
    LlamaCppEmbedding(const std::string& baseUrl, const std::string& model);

    std::vector<double> embedQuery(const std::string& text) override {
        // POST /embedding → 解析 JSON 响应 → 返回 vector<double>
        // 使用 cpp-httplib（已在 third_party 中）
    }
};
```

llama.cpp 启动命令示例：
```bash
llama-server -m nomic-embed-text-v1.5.Q8_0.gguf --embedding --port 8080
```

**注意事项**：
- 切换嵌入模型后需要清空 `embedding_cache` 和 `chunks.embedding` 列，重新索引
- 不同模型的维度不同（如 nomic-embed 是 768 维，all-MiniLM 是 384 维）
- embedding 批量请求时注意并发限制和超时

### 6.2 ANN 近似最近邻搜索

当前向量搜索是暴力遍历所有 chunk，时间复杂度 O(n*d)。优化方案：

- **sqlite-vec 扩展**：SQLite 的向量搜索扩展，直接在 SQL 层做 ANN 查询
- **HNSW 索引**：内存中的近似最近邻索引，查询时间 O(log n)
- **简单优化**：先用 FTS5 粗筛 top-100，再对这 100 个做向量排序（两阶段检索）

### 6.3 chunker 改进

- **Markdown 感知分块**：按标题（`#`、`##`）切分，保证语义完整性
- **代码块保护**：不在代码块（` ``` `）中间切割
- **段落合并**：短段落合并到一个 chunk，避免碎片化

### 6.4 compaction 改进

当前是简单的截断 + 取前 N 字符作为摘要。改进方向：

- **LLM 摘要**：调用本地模型生成真正的摘要（参考 openclaw 的 compact.ts）
- **滑动摘要**：被移除的消息分批摘要，保留更多信息
- **重要性排序**：根据消息长度、角色、时间戳等因素加权，保留重要消息

### 6.5 持久化会话

当前 `SessionStore` 用 `unordered_map` 存在内存中，程序退出就丢失。改进：

- **JSONL 文件持久化**：参考 openclaw 的 session 文件格式
- **SQLite 持久化**：复用 `MemoryStore`，新增 `sessions` 和 `messages` 表
- **增量追加**：每条消息写入后立即持久化，避免崩溃丢失

### 6.6 文件监听与自动索引

- **文件系统监听**：用 `std::filesystem::last_write_time` 检测文件变化
- **增量索引**：只重新索引变化的文件（通过 `needsReindex()` 检查 mtime/hash）
- **事件通知**：索引完成后通过 `EventBus`（已有实现）通知其他模块

### 6.7 搜索结果缓存

- **LRU 缓存**：对频繁查询的结果做缓存
- **查询归一化**：相同语义的查询（如 "fox" 和 "FOX"）复用缓存

---

## 七、与 openclaw 的差异说明

| 特性 | openclaw | ClawLite（当前） | 说明 |
|------|----------|-----------------|------|
| 异步模型 | 全 async/await | 同步 | C++ 简化，单线程即可 |
| 嵌入后端 | node-llama-cpp / 远程 API | LocalMockEmbedding | 后续接入 llama.cpp |
| 向量存储 | sqlite-vec 扩展 | JSON 字符串列 | 性能差但实现简单 |
| 会话持久化 | JSONL 文件 | 内存 HashMap | 后续可加文件持久化 |
| compaction | LLM 摘要 + checkpoint | 截断 + 取前 N 字符 | 后续可接入 LLM |
| 并发控制 | write lock + timeout | 无 | 单线程不需要 |
| 错误恢复 | checkpoint + rollback | 无 | 简化实现 |
| 多模态 | 图片 base64 嵌入 | 仅文本 | 课程作业不需要 |
| schema 迁移 | ALTER TABLE 检测 | IF NOT EXISTS | 简化实现 |
