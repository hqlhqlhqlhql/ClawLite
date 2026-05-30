// ClawLite — 文件状态 LRU 缓存实现
// 数据结构：双向链表 + HashMap，O(1) get/put

#include "memory/file_state_cache.h"

namespace clawlite {

FileStateCache::FileStateCache(int capacity) : m_capacity(capacity) {}

std::optional<std::string> FileStateCache::get(const std::string& path) {
    auto it = m_cache.find(path);
    if (it == m_cache.end()) {
        m_misses++;
        return std::nullopt;
    }
    m_hits++;
    touch(it);
    return it->second.data.content;
}

void FileStateCache::put(const std::string& path, std::string content, int64_t mtimeMs) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        // 更新已存在的条目
        it->second.data.content = std::move(content);
        it->second.data.mtimeMs = mtimeMs;
        touch(it);
        return;
    }

    // 新条目
    if ((int)m_cache.size() >= m_capacity) {
        evict();
    }

    m_order.push_front(path);
    CacheEntry ce;
    ce.data.content = std::move(content);
    ce.data.mtimeMs = mtimeMs;
    ce.orderIt = m_order.begin();
    m_cache[path] = ce;
}

void FileStateCache::invalidate(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) {
        m_order.erase(it->second.orderIt);
        m_cache.erase(it);
    }
}

void FileStateCache::touch(const CacheMap::iterator& it) {
    m_order.erase(it->second.orderIt);
    m_order.push_front(it->first);
    it->second.orderIt = m_order.begin();
}

void FileStateCache::evict() {
    if (m_order.empty()) return;
    std::string path = m_order.back();
    m_order.pop_back();
    m_cache.erase(path);
}

} // namespace clawlite
