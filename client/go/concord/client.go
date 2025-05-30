/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client main implementation
 */

package concord

import (
	"errors"
	"time"
)

// ErrKeyNotFound 表示键不存在错误
var ErrKeyNotFound = errors.New("key not found")

// Config 客户端配置
type Config struct {
	Endpoints   []string      // 服务器节点地址
	Timeout     time.Duration // 超时时间
	RetryCount  int           // 重试次数
	EnableCache bool          // 是否启用缓存
}

// Client 表示ConcordKV客户端
type Client struct {
	config Config
}

// NewClient 创建新的客户端
func NewClient(config Config) (*Client, error) {
	return &Client{
		config: config,
	}, nil
}

// Get 获取键值
func (c *Client) Get(key string) (string, error) {
	// 这只是一个空实现，用于测试
	return "", nil
}

// Set 设置键值
func (c *Client) Set(key, value string) error {
	// 这只是一个空实现，用于测试
	return nil
}

// Delete 删除键
func (c *Client) Delete(key string) error {
	// 这只是一个空实现，用于测试
	return nil
}

// Close 关闭客户端连接
func (c *Client) Close() error {
	// 这只是一个空实现，用于测试
	return nil
}

// NewTransaction 创建新的事务
func (c *Client) NewTransaction() *Transaction {
	// 返回带有默认选项的事务
	return &Transaction{
		options: TransactionOptions{
			IsolationLevel: IsolationRepeatableRead,
			ReadOnly:       false,
			Timeout:        30,
			RetryCount:     3,
		},
		status: "active",
	}
}
