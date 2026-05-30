# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

**环境：Windows，使用 PowerShell。禁止使用类 Unix 指令（如 `./`、`ls`、`rm` 等），一律使用 PowerShell 等价命令。**

```powershell
# Configure (MinGW on Windows)
cmake -B build -G "MinGW Makefiles"

# Build
cmake --build build

# Run all tests
.\build\clawlite_tests.exe

# Run tests via ctest (VSC integrated)
ctest --test-dir build

# Run B module benchmark (outputs JSON with 3 charts)
.\build\bench_b_module.exe
```

Tests use `TEST_ASSERT` macro (defined in `tests/test_helpers.h`) instead of `assert()`. Failed assertions print to stderr but don't terminate the program. Each test suite reports its failure count.

## Architecture

C++17 project. Three modules compiled as static libraries, linked into one executable:

```
clawlite_core  ← types, event_bus
clawlite_skill ← skill_parser, loader, registry, filter
clawlite_memory ← memory_store, chunker, search_manager, session_store, compaction, context_engine, file_state_cache
clawlite_llm   ← llm_client, prompt_builder, runtime_plan, harness, tool_executor
```

Header files in `include/`, source in `src/`, tests in `tests/`.

### Module B: Memory System (key components)

- **MemoryStore** — SQLite wrapper (4 tables + FTS5 virtual table). All SQL uses prepared statements via `sqlite3_prepare_v2`/`sqlite3_step`/`sqlite3_finalize`. Forward-declares `struct sqlite3` outside `namespace clawlite` in the header. Note: `embedding_cache` table and `embedding` column in `chunks` still exist in the schema but are unused dead code pending cleanup.
- **Chunker** — Sliding window algorithm for Markdown. `chunkMarkdown()` splits text into overlapping chunks by character count.
- **FileStateCache** — LRU cache (doubly-linked list + HashMap, O(1) get/put). Stores file content keyed by path. Used by `ContextEngine` to re-inject recent file state after compaction. Capacity 32 by default.
- **SearchManager** — FTS5-only full-text search (BM25 ranking). Vector search removed. Triggered only by `/memory search` command, never auto-called in the main loop.
- **SessionStore** — In-memory `unordered_map<string, SessionEntry>`. Session keys are hierarchical: `agent:<id>:<channel>:<peer>[:thread:<id>]`. Persists to JSONL via `save()`/`load()`.
- **Compactor** — Layered heuristic: (1) `ensureToolPairing()` removes orphan tool_results, (2) truncate oversized tool outputs, (3) drop oldest turns, (4) `LLMSummary` strategy with injected summarizer function. Three strategies: `TruncateOnly` / `GreedySummary` / `LLMSummary`.
- **ContextEngine** — Assembly layer. `createContextEngine()` factory function returns `unique_ptr<IContextEngine>`. Concrete `DefaultContextEngine` is defined in the .cpp file, not the header. Owns `FileStateCache` and re-injects up to 3 recent files after compaction.

### Data flow

```
File → Chunker → MemoryStore (SQLite, FTS5)
                 FileStateCache (LRU, for post-compaction re-injection)
User query → SearchManager (FTS5 only, command-triggered) → results
Messages → SessionStore (JSONL persist) → Compactor → ContextEngine.assemble()
```

## Key patterns

- **SQLite**: `MemoryStore` uses `void*` pattern-free approach. The `sqlite3*` forward declaration is at file scope (outside `namespace clawlite`) to avoid type conflicts with the real `sqlite3.h`.
- **FTS5 sync**: `upsertChunk()` writes to both `chunks` table and `chunks_fts` virtual table. `removeChunksByFile()` deletes from both.
- **LRU cache**: `FileStateCache` uses `std::list<string>` (order) + `unordered_map<string, CacheEntry>` (lookup). `touch()` moves an entry to list front in O(1). `evict()` removes from list back.
- **Compaction pipeline**: `Compactor::compact()` runs fixed heuristics in order — tool pairing → truncate long outputs → drop old turns → LLM summary. `LLMSummary` strategy requires an injected `std::function<string(vector<Message>)>` summarizer; falls back to `GreedySummary` if not set.
- **Search is command-only**: `SearchManager.search()` is never called automatically. The main loop does not inject search results into the system prompt. Only `/memory search` triggers it.
- **Test safety**: Always guard array/optional access with bounds checks before dereferencing (e.g., `if (!chunks.empty()) TEST_ASSERT(chunks[0]...)`).

## Dependencies (in third_party/)

- `sqlite3/sqlite3.c` + `sqlite3.h` — Compiled as static library with `SQLITE_ENABLE_FTS5`
- `httplib/httplib.h` — Header-only HTTP client (present but no longer used; ws2_32 link kept for potential future use)

## Windows notes

- CMake uses `ws2_32` link for Winsock
- `_WIN32_WINNT=0x0A00` defined for httplib compatibility
- UTF-8 source encoding enforced via `-finput-charset=UTF-8 -fexec-charset=UTF-8`
