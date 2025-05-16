package concord

import (
	"errors"
	"sync"
	"time"
)

// 错误定义
var (
	ErrNoEndpoints      = errors.New("没有可用的节点端点")
	ErrConnectionFailed = errors.New("连接失败")
	ErrTimeout          = errors.New("请求超时")
	ErrKeyNotFound      = errors.New("键不存在")
	ErrInvalidArgument  = errors.New("无效参数")
)

// Config 客户端配置
type Config struct {
	// 集群节点列表
	Endpoints []string
	// 连接超时时间
	Timeout time.Duration
	// 重试次数
	RetryCount int
	// 重试间隔
	RetryInterval time.Duration
	// 客户端缓存大小
	CacheSize int
	// 缓存TTL
	CacheTTL time.Duration
	// 是否启用缓存
	EnableCache bool
}

// Client ConcordKV客户端
type Client struct {
	config     Config
	mu         sync.RWMutex
	conns      map[string]*connection
	cache      *Cache
	closed     bool
	discovery  *Discovery  // 节点发现服务
	monitoring *Monitoring // 监控服务
}

// 内部连接结构定义已移至connection.go文件中

// NewClient 创建新的客户端实例
func NewClient(config Config) (*Client, error) {
	if len(config.Endpoints) == 0 {
		return nil, ErrNoEndpoints
	}

	if config.Timeout == 0 {
		config.Timeout = 3 * time.Second
	}

	if config.RetryCount == 0 {
		config.RetryCount = 3
	}

	if config.RetryInterval == 0 {
		config.RetryInterval = 500 * time.Millisecond
	}

	client := &Client{
		config: config,
		conns:  make(map[string]*connection),
	}

	// 初始化缓存（如果启用）
	if config.EnableCache {
		client.cache = NewCache(config.CacheSize)
	}

	// 初始化连接
	if err := client.initConnections(); err != nil {
		return nil, err
	}

	return client, nil
}

// 初始化所有连接
func (c *Client) initConnections() error {
	for _, endpoint := range c.config.Endpoints {
		conn := newConnection(endpoint)
		// 尝试连接
		if err := conn.connect(); err != nil {
			return ErrConnectionFailed
		}
		c.conns[endpoint] = conn
	}
	return nil
}

// 初始化新发现的连接
func (c *Client) initNewConnections(endpoints []string) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	for _, endpoint := range endpoints {
		if _, exists := c.conns[endpoint]; !exists {
			conn := newConnection(endpoint)
			// 尝试连接
			if err := conn.connect(); err != nil {
				// 记录错误但继续处理其他连接
				continue
			}
			c.conns[endpoint] = conn
		}
	}
	return nil
}

// Close 关闭客户端及其所有连接
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.closed {
		return nil
	}

	c.closed = true

	// 停止发现服务（如果启用）
	if c.discovery != nil {
		c.discovery.Stop()
	}

	// 停止监控服务（如果启用）
	if c.monitoring != nil {
		c.monitoring.Stop()
	}

	// 关闭所有连接
	for _, conn := range c.conns {
		if err := conn.disconnect(); err != nil {
			// 记录错误但继续关闭其他连接
			// 实际项目中可能需要日志记录
		}
	}

	return nil
}

// Get 获取键对应的值
func (c *Client) Get(key string) (string, error) {
	if key == "" {
		return "", ErrInvalidArgument
	}

	// 如果启用了缓存，尝试从缓存获取
	if c.cache != nil {
		if value, ok := c.cache.Get(key); ok {
			return value, nil
		}
	}

	// 构建请求
	req := request{
		Type: "GET",
		Key:  key,
	}

	// 发送请求到一个可用连接
	resp, err := c.doRequestWithRetry(req)
	if err != nil {
		return "", err
	}

	if !resp.Success {
		if resp.Error == "key not found" {
			return "", ErrKeyNotFound
		}
		return "", errors.New(resp.Error)
	}

	// 如果启用了缓存，更新缓存
	if c.cache != nil {
		c.cache.Set(key, resp.Value, c.config.CacheTTL)
	}

	return resp.Value, nil
}

// Set 设置键值对
func (c *Client) Set(key, value string) error {
	if key == "" {
		return ErrInvalidArgument
	}

	// 构建请求
	req := request{
		Type:  "SET",
		Key:   key,
		Value: value,
	}

	// 发送请求到一个可用连接
	resp, err := c.doRequestWithRetry(req)
	if err != nil {
		return err
	}

	if !resp.Success {
		return errors.New(resp.Error)
	}

	// 如果启用了缓存，更新缓存
	if c.cache != nil {
		c.cache.Set(key, value, c.config.CacheTTL)
	}

	return nil
}

// Delete 删除键值对
func (c *Client) Delete(key string) error {
	if key == "" {
		return ErrInvalidArgument
	}

	// 构建请求
	req := request{
		Type: "DELETE",
		Key:  key,
	}

	// 发送请求到一个可用连接
	resp, err := c.doRequestWithRetry(req)
	if err != nil {
		return err
	}

	if !resp.Success {
		return errors.New(resp.Error)
	}

	// 如果启用了缓存，从缓存中删除
	if c.cache != nil {
		c.cache.Delete(key)
	}

	return nil
}

// 发送请求到一个可用连接并支持重试
func (c *Client) doRequestWithRetry(req request) (*response, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if c.closed {
		return nil, errors.New("客户端已关闭")
	}

	var lastErr error
	for i := 0; i <= c.config.RetryCount; i++ {
		// 选择一个可用连接
		conn, err := c.getAvailableConnection()
		if err != nil {
			lastErr = err
			time.Sleep(c.config.RetryInterval)
			continue
		}

		// 发送请求
		resp, err := conn.doRequest(req)
		if err != nil {
			lastErr = err
			time.Sleep(c.config.RetryInterval)
			continue
		}

		return resp, nil
	}

	if lastErr != nil {
		return nil, lastErr
	}
	return nil, ErrTimeout
}

// 获取一个可用的连接
func (c *Client) getAvailableConnection() (*connection, error) {
	if len(c.conns) == 0 {
		return nil, ErrNoEndpoints
	}

	// 如果启用了节点发现和负载均衡，使用负载均衡策略选择节点
	if c.discovery != nil {
		endpoint, err := c.discovery.GetEndpoint()
		if err != nil {
			return nil, err
		}

		if conn, ok := c.conns[endpoint]; ok && conn.isHealthy() {
			return conn, nil
		}
	}

	// 简单的轮询策略，在实际项目中可以改进为更复杂的负载均衡策略
	for _, conn := range c.conns {
		if conn.isHealthy() {
			return conn, nil
		}
	}

	return nil, ErrConnectionFailed
}

// 基本请求结构
type request struct {
	Type  string
	Key   string
	Value string
}

// 基本响应结构
type response struct {
	Success bool
	Value   string
	Error   string
}
