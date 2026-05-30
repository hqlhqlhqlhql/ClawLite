#pragma once
// ClawLite — 文件状态 LRU 缓存
// 数据结构：双向链表 + HashMap，O(1) get/put
// 用途：Agent 多次读同一文件时，第二次用 stub 替换，省 token
//
// 参考：claude-code 的 readFileState 缓存

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

namespace clawlite {

class FileStateCache {
public:
    explicit FileStateCache(int capacity = 32);
    ~FileStateCache() = default;

    // 查询文件内容，命中则移到队首（标记为最近使用）
    std::optional<std::string> get(const std::string& path);

    // 存入文件内容，满了则淘汰队尾（最久未使用）
    void put(const std::string& path, std::string content, int64_t mtimeMs);

    // 文件被外部修改时主动失效
    void invalidate(const std::string& path);

    // 统计信息（用于 benchmark）
    int hitCount() const { return m_hits; }
    int missCount() const { return m_misses; }
    size_t cacheSize() const { return m_cache.size(); }

private:
    struct Entry {
        std::string content;
        int64_t mtimeMs;
    };

    int m_capacity;
    int m_hits = 0;
    int m_misses = 0;

    // LRU 数据结构：
    // 1. 双向链表维护访问顺序（front=最近, back=最久）
    // 2. HashMap 实现 O(1) 查找
    using OrderList = std::list<std::string>;  // 存 path
    struct CacheEntry {
        Entry data;
        OrderList::iterator orderIt;
    };
    using CacheMap = std::unordered_map<std::string, CacheEntry>;

    OrderList m_order;
    CacheMap m_cache;

    void touch(const CacheMap::iterator& it);
    void evict();
};

} // namespace clawlite
