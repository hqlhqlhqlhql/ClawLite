# ClawLite — 轻量级 Agent Runtime

数据结构大作业 | 基于 C++ 实现 | 三人协作

---

## 一、项目概述

ClawLite 是一个轻量级 Agent Runtime，参考 [openclaw](https://github.com/nicepkg/openclaw) 的架构设计，用 C++ 从零实现核心功能。重点突出**数据结构的应用**和**工程工作量**，不涉及 AI 算法本身。

### 核心功能

1. **Skill Runtime & Parser** — 技能定义解析、加载、过滤、提示词生成
2. **Context Memory System** — 上下文记忆管理、分块、检索、压缩
3. **LLM Runtime & Prompt Engine** — LLM 调用、提示词构建、Agent 主循环、工具执行

### 技术栈

- C++17
- CMake 3.16+
- SQLite3（嵌入式数据库）
- yaml-cpp（YAML 解析）
- cpp-httplib（HTTP 客户端，header-only）

---

## 二、项目结构

```
ClawLite/
├── CMakeLists.txt                 # 根构建配置
├── README.md                      # 本文档
│
├── include/                       # 头文件（接口定义）
│   ├── core/                      # 公共模块
│   │   ├── types.h                # 公共类型：Message, Tool, SearchResult 等
│   │   └── event_bus.h            # 观察者模式事件总线
│   │
│   ├── skill/                     # 模块 A：Skill Runtime & Parser
│   │   ├── skill_parser.h         # SKILL.md frontmatter 解析器
│   │   ├── skill_loader.h         # 多目录技能发现与加载
│   │   ├── skill_registry.h       # 技能注册表（HashMap 去重合并）
│   │   └── skill_filter.h         # 资格过滤（OS / bin / env 检查）
│   │
│   ├── memory/                    # 模块 B：Context Memory System
│   │   ├── context_engine.h       # 上下文引擎接口（策略模式）
│   │   ├── memory_store.h         # SQLite 存储层（4 张表 + FTS5）
│   │   ├── chunker.h              # Markdown 滑动窗口分块算法
│   │   ├── embedding.h            # 向量嵌入接口（本地/远程）
│   │   ├── search_manager.h       # 混合检索（向量余弦 + FTS5 全文）
│   │   ├── session_store.h        # 会话持久化（JSONL 转录）
│   │   └── compaction.h           # 上下文压缩（LLM 摘要）
│   │
│   └── llm/                       # 模块 C：LLM Runtime & Prompt Engine
│       ├── llm_client.h           # LLM HTTP 客户端（SSE 流式解析）
│       ├── prompt_builder.h       # 系统提示词多段拼接构建器
│       ├── runtime_plan.h         # 运行时计划（策略组合）
│       ├── harness.h              # Agent 主循环（状态机）
│       └── tool_executor.h        # 工具注册与执行
│
├── src/                           # 源文件（实现）
│   ├── core/
│   │   ├── types.cpp
│   │   └── event_bus.cpp
│   ├── skill/                     # A 的实现
│   │   ├── skill_parser.cpp
│   │   ├── skill_loader.cpp
│   │   ├── skill_registry.cpp
│   │   └── skill_filter.cpp
│   ├── memory/                    # B 的实现
│   │   ├── context_engine.cpp
│   │   ├── memory_store.cpp
│   │   ├── chunker.cpp
│   │   ├── embedding.cpp
│   │   ├── search_manager.cpp
│   │   ├── session_store.cpp
│   │   └── compaction.cpp
│   ├── llm/                       # C 的实现
│   │   ├── llm_client.cpp
│   │   ├── prompt_builder.cpp
│   │   ├── runtime_plan.cpp
│   │   ├── harness.cpp
│   │   └── tool_executor.cpp
│   └── main.cpp                   # 程序入口（REPL 交互循环）
│
├── tests/                         # 单元测试
│   ├── test_skill_parser.cpp      # A 的测试
│   ├── test_skill_registry.cpp
│   ├── test_memory_store.cpp      # B 的测试
│   ├── test_chunker.cpp
│   ├── test_search.cpp
│   ├── test_session_store.cpp
│   └── test_prompt_builder.cpp    # C 的测试
│
├── skills/                        # 内置技能定义文件
│   ├── hello/SKILL.md
│   ├── calculator/SKILL.md
│   └── file-reader/SKILL.md
│
└── third_party/                   # 第三方库（手动放置或 git submodule）
    ├── sqlite3/                   # sqlite3.c + sqlite3.h
    ├── yaml-cpp/
    └── httplib/                   # httplib.h (header-only)
```

---

## 三、模块详解与数据结构对照

### 模块 A：Skill Runtime & Parser（负责人：___）

**职责**：解析 SKILL.md 文件，发现/加载/过滤技能，生成技能提示词。

#### 核心数据结构

| 数据结构 | 用途 | 代码位置 |
|---------|------|---------|
| `std::unordered_map<string, SkillEntry>` | 技能注册表，按 name 去重合并 | `skill_registry.h` |
| `std::unordered_map<string, string>` | frontmatter KV 解析结果 | `skill_parser.h` |
| `std::set<string>` | 已发现技能名去重、环境变量集合 | `skill_loader.h`, `skill_filter.h` |
| 排序 `vector` + 二分查找 | 技能提示词字符预算裁剪 | `skill_registry.h:buildSkillPrompt()` |
| 观察者模式 `set<callback>` | 技能变更通知 | `event_bus.h` |

#### 关键算法

1. **Frontmatter 解析**（参考 `openclaw-main/src/markdown/frontmatter.ts`）
   - 读取 SKILL.md，找到 `---` 分隔的 YAML 块
   - 用 yaml-cpp 解析为 KV 对
   - `metadata` 字段内嵌 JSON，需要二次解析

2. **多源加载与优先级合并**（参考 `openclaw-main/src/agents/skills/workspace.ts:loadSkillEntries`）
   - 扫描多个目录：`skills/`（内置）、`~/.clawlite/skills/`（用户）、`./skills/`（项目）
   - 用 Map 按 name 合并，后加载的覆盖先加载的（优先级：项目 > 用户 > 内置）

3. **二分搜索裁剪**（参考 `openclaw-main/src/agents/skills/workspace.ts:applySkillsPromptLimits`）
   - 技能按 name 排序后，用二分查找找到不超过字符预算的最大前缀
   - 这是**数据结构课程重点**，务必实现

#### 文件清单

```
include/skill/skill_parser.h      ← 解析 SKILL.md 的 frontmatter
include/skill/skill_loader.h      ← 多目录扫描，返回 vector<SkillEntry>
include/skill/skill_registry.h    ← Map 去重合并 + buildSkillPrompt()
include/skill/skill_filter.h      ← OS/bin/env 资格检查
src/skill/skill_parser.cpp
src/skill/skill_loader.cpp
src/skill/skill_registry.cpp
src/skill/skill_filter.cpp
tests/test_skill_parser.cpp
tests/test_skill_registry.cpp
```

---

### 模块 B：Context Memory System（负责人：___）

**职责**：管理 Agent 的上下文记忆，包括文件索引、分块、向量存储、全文检索、会话管理、上下文压缩。

#### 核心数据结构

| 数据结构 | 用途 | 代码位置 |
|---------|------|---------|
| SQLite B+Tree（files 表） | 跟踪已索引文件的 path/hash/mtime | `memory_store.h` |
| SQLite B+Tree（chunks 表） | 存储文本块 + 向量嵌入（JSON） | `memory_store.h` |
| SQLite FTS5 倒排索引 | 全文搜索 | `memory_store.h` |
| `vector<MemoryChunk>` | 分块结果，滑动窗口产出 | `chunker.h` |
| `vector<double>` | 向量嵌入，余弦相似度计算 | `embedding.h`, `search_manager.h` |
| `unordered_map<string, vector<Message>>` | 会话存储，按 session key 索引 | `session_store.h` |
| 层级字符串键解析 | 会话键 `agent:id:channel:peer` | `session_store.h` |
| 优先级队列 / 排序向量 | 搜索结果按分数排序合并 | `search_manager.h` |
| `unordered_map<string, vector<double>>` | embedding 缓存 | `memory_store.h` |

#### 关键算法

1. **滑动窗口分块**（参考 `openclaw-main/packages/memory-host-sdk/host/internal.ts:chunkMarkdown`）
   - 按行遍历 Markdown，维护一个滑动窗口
   - 窗口大小 = `chunkTokens`（如 200 token），步长 = `chunkTokens - overlapTokens`（如 50）
   - CJK 文本特殊处理：按字符而非空格分割
   - **这是数据结构课程重点**，要展示窗口滑动过程

2. **SQLite 存储层**（参考 `openclaw-main/packages/memory-host-sdk/host/memory-schema.ts`）
   - 4 张表设计：`meta`（KV 元数据）、`files`（文件索引）、`chunks`（文本块+向量）、`embedding_cache`
   - FTS5 虚拟表做全文搜索
   - 索引设计：`idx_chunks_path`、`idx_chunks_source`
   - **这是工作量重点**，SQLite C API 调用较多

3. **混合搜索**（参考 `openclaw-main/packages/memory-host-sdk/host/types.ts:MemorySearchManager`）
   - 向量搜索：遍历 chunks 表，计算余弦相似度，取 top-k
   - 全文搜索：FTS5 的 `MATCH` 查询，BM25 排序
   - 分数合并：`finalScore = alpha * vectorScore + (1-alpha) * textScore`
   - **这是数据结构课程重点**，要展示排序合并过程

4. **上下文压缩**（参考 `openclaw-main/src/agents/pi-embedded-runner/compact.ts`）
   - 当对话 token 数超过 budget 时触发
   - 将旧对话发给 LLM 生成摘要
   - 保留最近 N 轮 + 摘要替代旧轮次
   - 这个可以简化实现（直接截断 + 记录截断点）

5. **会话键解析**（参考 `openclaw-main/src/sessions/session-key-utils.ts`）
   - 解析 `agent:main:cli:user:default` 这种层级键
   - 支持按 agent/channel/peer 维度查询

#### 文件清单

```
include/memory/context_engine.h    ← 虚接口：ingest/assemble/compact/search
include/memory/memory_store.h      ← SQLite 封装：4 张表 + FTS5
include/memory/chunker.h           ← 滑动窗口分块算法
include/memory/embedding.h         ← 向量嵌入接口（本地模拟/远程 API）
include/memory/search_manager.h    ← 混合检索：向量 + FTS5
include/memory/session_store.h     ← 会话持久化 + 键解析
include/memory/compaction.h        ← 上下文压缩策略
src/memory/context_engine.cpp
src/memory/memory_store.cpp
src/memory/chunker.cpp
src/memory/embedding.cpp
src/memory/search_manager.cpp
src/memory/session_store.cpp
src/memory/compaction.cpp
tests/test_memory_store.cpp
tests/test_chunker.cpp
tests/test_search.cpp
tests/test_session_store.cpp
```

---

### 模块 C：LLM Runtime & Prompt Engine（负责人：___）

**职责**：调用 LLM API，构建系统提示词，实现 Agent 主循环（工具调用循环），管理运行时计划。

#### 核心数据结构

| 数据结构 | 用途 | 代码位置 |
|---------|------|---------|
| 状态机（enum + switch） | Agent 主循环：IDLE → THINKING → TOOL_CALL → RESPONDING | `harness.h` |
| `vector<ChatMessage>` | 对话历史，传给 LLM | `llm_client.h` |
| `vector<Tool>` | 工具定义列表，传给 LLM function calling | `tool_executor.h` |
| `unordered_map<string, ToolHandler>` | 工具注册表，name → handler | `tool_executor.h` |
| `vector<string>` | 系统提示词多段拼接 | `prompt_builder.h` |
| 结构体组合 | RuntimePlan 聚合所有运行时决策 | `runtime_plan.h` |
| SSE 解析状态机 | 流式响应逐行解析 | `llm_client.h` |
| 递归/循环 | 工具调用结果回传后再请求 | `harness.h` |

#### 关键算法

1. **SSE 流式解析**（参考 `openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts`）
   - 逐行读取 `data: {"choices":[{"delta":{"content":"..."}}]}` 
   - 状态机：读到 `data: ` 开头则解析 JSON，读到空行则结束
   - 支持 `data: [DONE]` 终止信号

2. **Agent 主循环状态机**（参考 `openclaw-main/src/agents/pi-embedded-runner/run/attempt.ts`）
   ```
   IDLE → 构建消息 → 调用 LLM → 收到响应
                                    ├── 有 tool_call → 执行工具 → 结果加入消息 → 再调用 LLM
                                    └── 纯文本 → 返回结果
   ```
   - 最大循环次数限制（防止死循环）
   - 工具调用结果格式化后回传

3. **系统提示词构建**（参考 `openclaw-main/src/agents/pi-embedded-runner/system-prompt.ts`）
   - 多来源拼接：基础 prompt + 技能列表 + 工具列表 + 内存上下文 + 运行时信息
   - 每个来源是一个独立段落，最后 join

4. **工具注册与执行**（参考 `openclaw-main/src/agents/tools/common.ts`）
   - `Tool` 结构体：name + description + parameters(JSON Schema) + handler 函数
   - 注册表用 `unordered_map<string, Tool>`
   - 执行时从 LLM 响应中提取 tool_call name 和 arguments，查表调用

#### 文件清单

```
include/llm/llm_client.h          ← HTTP 客户端 + SSE 流式解析
include/llm/prompt_builder.h      ← 系统提示词多段拼接
include/llm/runtime_plan.h        ← 运行时计划结构体
include/llm/harness.h             ← Agent 主循环状态机
include/llm/tool_executor.h       ← 工具注册表 + 执行器
src/llm/llm_client.cpp
src/llm/prompt_builder.cpp
src/llm/runtime_plan.cpp
src/llm/harness.cpp
src/llm/tool_executor.cpp
tests/test_prompt_builder.cpp
```

---

## 四、模块间接口约定

三个模块通过以下接口解耦，各人独立开发测试，最后集成：

```
┌──────────────┐      ┌──────────────────┐      ┌──────────────┐
│  A: Skill    │─────▶│  B: Memory       │─────▶│  C: LLM      │
│  Registry    │      │  Context Engine   │      │  Harness     │
└──────────────┘      └──────────────────┘      └──────────────┘
       │                      │                        │
       │ getActiveSkills()    │ assemble(budget)       │ runTurn()
       │ buildSkillPrompt()   │ search(query, k)       │
       ▼                      ▼                        ▼
  技能列表 + 提示词      组装好的上下文消息          LLM 回复
```

### A → B/C 提供

```cpp
// 技能注册表接口
class SkillRegistry {
public:
    // 获取当前所有活跃技能
    std::vector<SkillEntry> getActiveSkills() const;
    
    // 构建技能提示词（带字符预算裁剪）
    std::string buildSkillPrompt(int charBudget = 18000) const;
    
    // 刷新技能（重新扫描目录）
    void refresh();
};
```

### B → C 提供

```cpp
// 上下文引擎接口
class ContextEngine {
public:
    // 写入一条消息到记忆
    void ingest(const Message& msg);
    
    // 组装上下文（在 token 预算内）
    std::vector<Message> assemble(int tokenBudget);
    
    // 压缩上下文（超限时调用）
    CompactResult compact(int targetTokens);
    
    // 语义搜索
    std::vector<SearchResult> search(const std::string& query, int topK = 5);
    
    // 会话管理
    void createSession(const std::string& sessionKey);
    std::vector<Message> getSessionHistory(const std::string& sessionKey, int maxTurns = 20);
};
```

### C 提供（主程序调用）

```cpp
// Agent Harness 接口
class AgentHarness {
public:
    // 执行一轮对话
    RunResult runTurn(
        const std::vector<Message>& context,
        const std::vector<Tool>& tools,
        const std::string& systemPrompt
    );
};
```

---

## 五、openclaw 参考代码索引

以下是你们各自需要重点阅读的 openclaw 源码：

### A 需要读的

| 文件 | 内容 | 阅读重点 |
|------|------|---------|
| `skills/weather/SKILL.md` | 最简单的技能定义示例 | 理解 frontmatter 格式 |
| `skills/taskflow/SKILL.md` | 复杂技能示例 | 理解 metadata.openclaw 结构 |
| `src/agents/skills/workspace.ts` | 技能加载主逻辑 | `loadSkillEntries()` 多源合并 |
| `src/agents/skills/local-loader.ts` | 目录扫描与文件读取 | `loadSkillsFromDirSafe()` |
| `src/agents/skills/types.ts` | 核心类型定义 | `SkillEntry`, `SkillSnapshot` |
| `src/agents/skills/config.ts` | 资格过滤 | `shouldIncludeSkill()` |
| `src/markdown/frontmatter.ts` | Frontmatter 解析 | `parseFrontmatterBlock()` 双策略 |

### B 需要读的（你的部分）

| 文件 | 内容 | 阅读重点 |
|------|------|---------|
| `src/context-engine/types.ts` | 上下文引擎接口定义 | `ContextEngine` 接口的所有方法 |
| `src/context-engine/registry.ts` | 引擎注册表 | `Map<string, {factory, owner}>` 注册模式 |
| `packages/memory-host-sdk/host/memory-schema.ts` | SQLite 表结构 | 4 张表 + FTS5 + 索引 |
| `packages/memory-host-sdk/host/internal.ts` | 分块 + 余弦相似度 | `chunkMarkdown()` 滑动窗口 |
| `packages/memory-host-sdk/host/types.ts` | 搜索管理器接口 | `MemorySearchManager` |
| `packages/memory-host-sdk/host/query-expansion.ts` | 查询扩展 | 停用词 + 分词 |
| `packages/memory-host-sdk/host/session-files.ts` | 会话转录处理 | `SessionFileEntry` |
| `src/sessions/session-key-utils.ts` | 会话键解析 | 层级键结构 |
| `src/agents/pi-embedded-runner/compact.ts` | 上下文压缩 | 压缩流程 |

### C 需要读的

| 文件 | 内容 | 阅读重点 |
|------|------|---------|
| `src/agents/pi-embedded-runner/run/attempt.ts` | 核心 LLM 调用循环 | 900+ 行，Agent 主循环 |
| `src/agents/pi-embedded-runner/system-prompt.ts` | 系统提示词构建 | `buildEmbeddedSystemPrompt()` |
| `src/agents/runtime-plan/types.ts` | 运行时计划 | `AgentRuntimePlan` 结构体 |
| `src/agents/tools/common.ts` | 工具接口 | `AnyAgentTool`, result 类型 |
| `src/agents/harness/types.ts` | Harness 接口 | `AgentHarness` 生命周期 |
| `src/agents/harness/v2.ts` | Harness V2 生命周期 | prepare→start→send→resolve→cleanup |
| `src/auto-reply/dispatch.ts` | 消息分发入口 | 完整调用链 |

---

## 六、构建与运行

### 依赖安装

```bash
# Windows (vcpkg)
vcpkg install sqlite3 yaml-cpp cpp-httplib

# 或手动下载
# SQLite3: https://sqlite.org/download.html (sqlite-amalgamation)
# yaml-cpp: https://github.com/jbeder/yaml-cpp
# cpp-httplib: https://github.com/yhirose/cpp-httplib (单头文件)
```

### 构建

```bash
cd ClawLite
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

### 运行

```bash
# 启动交互式 REPL
./clawlite

# 运行测试
./clawlite_tests
```

### REPL 用法

```
ClawLite Agent Runtime v0.1
Skills: 3 loaded | Memory: 0 chunks indexed
> 你好，请帮我计算 3+5
[Agent] 正在思考...
[Tool] calculator({"expr": "3+5"})
[Agent] 计算结果是 8。
> /skills                # 列出已加载技能
> /memory search 测试     # 搜索记忆
> /memory load README.md # 加载文件到记忆
> /quit                  # 退出
```

---

## 七、评分亮点清单

### 数据结构应用（务必在答辩中展示）

- [ ] **HashMap** — 技能注册表去重合并（A）、会话存储（B）、工具注册表（C）
- [ ] **SQLite B+Tree** — 文件/块/嵌入持久化（B）
- [ ] **FTS5 倒排索引** — 全文搜索（B）
- [ ] **滑动窗口算法** — Markdown 分块（B）
- [ ] **余弦相似度 + 排序** — 向量检索 top-k（B）
- [ ] **二分搜索** — 技能提示词字符预算裁剪（A）
- [ ] **状态机** — Agent 主循环（C）、SSE 解析（C）
- [ ] **观察者模式** — 事件总线（core）
- [ ] **策略模式** — 上下文引擎可插拔（B）
- [ ] **工厂模式** — 工具注册与创建（C）
- [ ] **优先级队列 / 归并排序** — 搜索结果合并（B）

### 工作量展示

- [ ] SQLite C API 调用（建表、索引、FTS5、事务）— B
- [ ] YAML 解析 + JSON 二次解析 — A
- [ ] HTTP 客户端 + SSE 流式解析 — C
- [ ] 多目录扫描 + 优先级合并 — A
- [ ] 滑动窗口分块 + CJK 处理 — B
- [ ] Agent 工具调用循环（最多 N 轮）— C
- [ ] 单元测试覆盖各模块核心算法 — 全员

---

## 八、开发建议

1. **先定义接口，再实现** — 头文件已经定义好接口，先编译通过（空实现），再逐个填充
2. **每人一个分支** — `feature/skill`、`feature/memory`、`feature/llm`
3. **测试先行** — 每实现一个算法就写对应的测试用例
4. **SQLite 用 amalgamation** — 下载 `sqlite3.c` + `sqlite3.h` 放到 `third_party/sqlite3/`，CMake 直接编译
5. **yaml-cpp 和 httplib** — 可以用 vcpkg，也可以手动放到 `third_party/`
6. **不要用 C++20** — 保证编译器兼容性（课程机器可能只有 C++17）

---

## 九、参考资源

- openclaw 源码：`openclaw-main/` 目录（已 clone）
- SQLite 文档：https://www.sqlite.org/docs.html
- yaml-cpp 文档：https://github.com/jbeder/yaml-cpp
- cpp-httplib：https://github.com/yhirose/cpp-httplib
- OpenAI API 格式：https://platform.openai.com/docs/api-reference/chat
