/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client - mock_client.go
 */
package concord

import (
	"errors"
	"sync"
	"time"
)

// 模拟错误
var (
	ErrKeyNotFound  = errors.New("key not found")
	ErrMockInternal = errors.New("内部错误")
)

// Config 客户端配置
type Config struct {
	Endpoints   []string      // 服务器节点地址
	Timeout     time.Duration // 超时时间
	RetryCount  int           // 重试次数
	EnableCache bool          // 是否启用缓存
	CacheSize   int           // 缓存大小
	CacheTTL    time.Duration // 缓存过期时间
}

// Client 表示ConcordKV客户端
type Client struct {
	config Config
	store  map[string]string
	mu     sync.RWMutex
}

// NewClient 创建新的客户端
func NewClient(config Config) (*Client, error) {
	return &Client{
		config: config,
		store:  make(map[string]string),
	}, nil
}

// Close 关闭客户端连接
func (c *Client) Close() error {
	return nil
}

// Get 获取键值
func (c *Client) Get(key string) (string, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if val, ok := c.store[key]; ok {
		return val, nil
	}

	return "", ErrKeyNotFound
}

// Set 设置键值
func (c *Client) Set(key, value string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.store[key] = value
	return nil
}

// Delete 删除键
func (c *Client) Delete(key string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	delete(c.store, key)
	return nil
}

// EnableMonitoring 启用监控
func (c *Client) EnableMonitoring(interval time.Duration) error {
	// 模拟实现
	return nil
}
