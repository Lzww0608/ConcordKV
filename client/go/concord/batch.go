/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Go client batch operations implementation
 */

package concord

import (
	"errors"
	"sync"
)

// KeyValue 定义键值对
type KeyValue struct {
	Key   string
	Value string
}

// BatchResult 表示批量操作的结果
type BatchResult struct {
	// 批量获取操作的结果
	Values map[string]string
	// 每个键的操作是否成功
	Succeeded map[string]bool
	// 每个键的错误信息（如果有）
	Errors map[string]error
}

// NewBatchResult 创建一个新的批量操作结果
func NewBatchResult() *BatchResult {
	return &BatchResult{
		Values:    make(map[string]string),
		Succeeded: make(map[string]bool),
		Errors:    make(map[string]error),
	}
}

// BatchGet 批量获取多个键的值
// 返回一个包含所有键值的map和可能的错误
func (c *Client) BatchGet(keys []string) (*BatchResult, error) {
	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()
	var wg sync.WaitGroup
	var mutex sync.Mutex

	// 并行处理所有键
	for _, key := range keys {
		wg.Add(1)
		go func(k string) {
			defer wg.Done()

			// 获取单个键的值
			value, err := c.Get(k)

			// 使用互斥锁保护结果映射的并发访问
			mutex.Lock()
			defer mutex.Unlock()

			if err != nil {
				result.Succeeded[k] = false
				result.Errors[k] = err
			} else {
				result.Values[k] = value
				result.Succeeded[k] = true
			}
		}(key)
	}

	// 等待所有获取操作完成
	wg.Wait()
	return result, nil
}

// BatchSet 批量设置多个键值对
// 接受KeyValue结构体的切片
func (c *Client) BatchSet(pairs []KeyValue) (*BatchResult, error) {
	if len(pairs) == 0 {
		return nil, errors.New("键值对列表不能为空")
	}

	result := NewBatchResult()
	var wg sync.WaitGroup
	var mutex sync.Mutex

	// 并行处理所有键值对
	for _, pair := range pairs {
		wg.Add(1)
		go func(kv KeyValue) {
			defer wg.Done()

			// 设置单个键值对
			err := c.Set(kv.Key, kv.Value)

			// 使用互斥锁保护结果映射的并发访问
			mutex.Lock()
			defer mutex.Unlock()

			if err != nil {
				result.Succeeded[kv.Key] = false
				result.Errors[kv.Key] = err
			} else {
				result.Succeeded[kv.Key] = true
			}
		}(pair)
	}

	// 等待所有设置操作完成
	wg.Wait()
	return result, nil
}

// BatchDelete 批量删除多个键
func (c *Client) BatchDelete(keys []string) (*BatchResult, error) {
	if len(keys) == 0 {
		return nil, errors.New("键列表不能为空")
	}

	result := NewBatchResult()
	var wg sync.WaitGroup
	var mutex sync.Mutex

	// 并行处理所有键
	for _, key := range keys {
		wg.Add(1)
		go func(k string) {
			defer wg.Done()

			// 删除单个键
			err := c.Delete(k)

			// 使用互斥锁保护结果映射的并发访问
			mutex.Lock()
			defer mutex.Unlock()

			if err != nil {
				result.Succeeded[k] = false
				result.Errors[k] = err
			} else {
				result.Succeeded[k] = true
			}
		}(key)
	}

	// 等待所有删除操作完成
	wg.Wait()
	return result, nil
}
