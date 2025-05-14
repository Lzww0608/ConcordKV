package concord

import (
	"sync"
	"time"
)

// CacheEntry 表示缓存中的单个条目
type CacheEntry struct {
	Value     string
	Timestamp time.Time
	TTL       time.Duration
}

// Cache 客户端缓存
type Cache struct {
	mu       sync.RWMutex
	entries  map[string]CacheEntry
	capacity int
}

// NewCache 创建新的客户端缓存
func NewCache(capacity int) *Cache {
	if capacity <= 0 {
		capacity = 1000 // 默认缓存大小
	}

	return &Cache{
		entries:  make(map[string]CacheEntry),
		capacity: capacity,
	}
}

// Get 从缓存获取值
func (c *Cache) Get(key string) (string, bool) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	entry, exists := c.entries[key]
	if !exists {
		return "", false
	}

	// 检查条目是否过期
	if entry.TTL > 0 && time.Since(entry.Timestamp) > entry.TTL {
		return "", false
	}

	return entry.Value, true
}

// Set 设置缓存值
func (c *Cache) Set(key, value string, ttl time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()

	// 检查容量限制
	if len(c.entries) >= c.capacity && c.entries[key].Value == "" {
		// 如果达到容量限制，执行简单的LRU清理策略
		c.evictOldest()
	}

	c.entries[key] = CacheEntry{
		Value:     value,
		Timestamp: time.Now(),
		TTL:       ttl,
	}
}

// Delete 从缓存删除值
func (c *Cache) Delete(key string) {
	c.mu.Lock()
	defer c.mu.Unlock()

	delete(c.entries, key)
}

// Clear 清空缓存
func (c *Cache) Clear() {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.entries = make(map[string]CacheEntry)
}

// Size 返回缓存当前大小
func (c *Cache) Size() int {
	c.mu.RLock()
	defer c.mu.RUnlock()

	return len(c.entries)
}

// 清除最旧的缓存条目（简单LRU实现）
func (c *Cache) evictOldest() {
	var oldestKey string
	var oldestTime time.Time

	// 初始化为最新时间，确保第一次比较会更新
	first := true

	for key, entry := range c.entries {
		if first || entry.Timestamp.Before(oldestTime) {
			oldestKey = key
			oldestTime = entry.Timestamp
			first = false
		}
	}

	if oldestKey != "" {
		delete(c.entries, oldestKey)
	}
}
