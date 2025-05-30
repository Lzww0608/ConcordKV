/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Raft consensus server - config.go
 */
package config

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"strconv"
	"strings"
	"sync"

	"gopkg.in/yaml.v3"
)

// Config 表示配置管理器
type Config struct {
	data     map[string]interface{}
	filePath string
	mutex    sync.RWMutex
	watchers map[string][]WatchCallback
}

// WatchCallback 是配置变更的回调函数类型
type WatchCallback func(oldValue, newValue interface{})

// Load 从文件加载配置
func Load(filePath string) (*Config, error) {
	cfg := &Config{
		data:     make(map[string]interface{}),
		filePath: filePath,
		watchers: make(map[string][]WatchCallback),
	}

	if _, err := os.Stat(filePath); os.IsNotExist(err) {
		return cfg, nil
	}

	data, err := ioutil.ReadFile(filePath)
	if err != nil {
		return nil, fmt.Errorf("读取配置文件失败: %w", err)
	}

	var fileData map[string]interface{}

	// 根据文件扩展名决定解析方式
	if strings.HasSuffix(filePath, ".yaml") || strings.HasSuffix(filePath, ".yml") {
		if err := yaml.Unmarshal(data, &fileData); err != nil {
			return nil, fmt.Errorf("解析YAML配置失败: %w", err)
		}
	} else if strings.HasSuffix(filePath, ".json") {
		if err := json.Unmarshal(data, &fileData); err != nil {
			return nil, fmt.Errorf("解析JSON配置失败: %w", err)
		}
	} else {
		return nil, fmt.Errorf("不支持的配置文件格式: %s", filePath)
	}

	cfg.data = fileData
	return cfg, nil
}

// Save 保存配置到文件
func (c *Config) Save() error {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	var data []byte
	var err error

	// 根据文件扩展名决定序列化方式
	if strings.HasSuffix(c.filePath, ".yaml") || strings.HasSuffix(c.filePath, ".yml") {
		data, err = yaml.Marshal(c.data)
		if err != nil {
			return fmt.Errorf("序列化YAML配置失败: %w", err)
		}
	} else if strings.HasSuffix(c.filePath, ".json") {
		data, err = json.MarshalIndent(c.data, "", "  ")
		if err != nil {
			return fmt.Errorf("序列化JSON配置失败: %w", err)
		}
	} else {
		return fmt.Errorf("不支持的配置文件格式: %s", c.filePath)
	}

	if err := ioutil.WriteFile(c.filePath, data, 0644); err != nil {
		return fmt.Errorf("写入配置文件失败: %w", err)
	}

	return nil
}

// get 获取指定路径的配置值
func (c *Config) get(path string) (interface{}, bool) {
	c.mutex.RLock()
	defer c.mutex.RUnlock()

	parts := strings.Split(path, ".")
	current := c.data

	for i, part := range parts {
		if i == len(parts)-1 {
			val, ok := current[part]
			return val, ok
		}

		next, ok := current[part]
		if !ok {
			return nil, false
		}

		nextMap, ok := next.(map[string]interface{})
		if !ok {
			// 尝试将JSON/YAML解析的map[interface{}]interface{}转为map[string]interface{}
			if m, ok := next.(map[interface{}]interface{}); ok {
				nextMap = make(map[string]interface{})
				for k, v := range m {
					if ks, ok := k.(string); ok {
						nextMap[ks] = v
					}
				}
			} else {
				return nil, false
			}
		}

		current = nextMap
	}

	return nil, false
}

// set 设置指定路径的配置值
func (c *Config) set(path string, value interface{}) error {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	parts := strings.Split(path, ".")
	current := c.data

	for i, part := range parts[:len(parts)-1] {
		next, ok := current[part]
		if !ok {
			// 创建中间节点
			next = make(map[string]interface{})
			current[part] = next
		}

		nextMap, ok := next.(map[string]interface{})
		if !ok {
			// 尝试将JSON/YAML解析的map[interface{}]interface{}转为map[string]interface{}
			if m, ok := next.(map[interface{}]interface{}); ok {
				nextMap = make(map[string]interface{})
				for k, v := range m {
					if ks, ok := k.(string); ok {
						nextMap[ks] = v
					}
				}
				current[part] = nextMap
			} else {
				return fmt.Errorf("路径 %s 的部分 %s 不是对象", path, strings.Join(parts[:i+1], "."))
			}
		}

		current = nextMap
	}

	lastPart := parts[len(parts)-1]
	oldValue, exists := current[lastPart]
	current[lastPart] = value

	// 触发监听回调
	if callbacks, ok := c.watchers[path]; ok && (!exists || oldValue != value) {
		for _, callback := range callbacks {
			go callback(oldValue, value)
		}
	}

	return nil
}

// GetInt 获取整数配置值
func (c *Config) GetInt(path string, defaultValue int) int {
	val, ok := c.get(path)
	if !ok {
		return defaultValue
	}

	switch v := val.(type) {
	case int:
		return v
	case int64:
		return int(v)
	case float64:
		return int(v)
	case string:
		if i, err := strconv.Atoi(v); err == nil {
			return i
		}
	}

	return defaultValue
}

// GetFloat 获取浮点数配置值
func (c *Config) GetFloat(path string, defaultValue float64) float64 {
	val, ok := c.get(path)
	if !ok {
		return defaultValue
	}

	switch v := val.(type) {
	case float64:
		return v
	case int:
		return float64(v)
	case int64:
		return float64(v)
	case string:
		if f, err := strconv.ParseFloat(v, 64); err == nil {
			return f
		}
	}

	return defaultValue
}

// GetBool 获取布尔配置值
func (c *Config) GetBool(path string, defaultValue bool) bool {
	val, ok := c.get(path)
	if !ok {
		return defaultValue
	}

	switch v := val.(type) {
	case bool:
		return v
	case string:
		if b, err := strconv.ParseBool(v); err == nil {
			return b
		}
	case int:
		return v != 0
	}

	return defaultValue
}

// GetString 获取字符串配置值
func (c *Config) GetString(path string, defaultValue string) string {
	val, ok := c.get(path)
	if !ok {
		return defaultValue
	}

	switch v := val.(type) {
	case string:
		return v
	case int, int64, float64, bool:
		return fmt.Sprintf("%v", v)
	}

	return defaultValue
}

// GetStringSlice 获取字符串数组配置值
func (c *Config) GetStringSlice(path string, defaultValue []string) []string {
	val, ok := c.get(path)
	if !ok {
		return defaultValue
	}

	switch v := val.(type) {
	case []interface{}:
		result := make([]string, 0, len(v))
		for _, item := range v {
			if s, ok := item.(string); ok {
				result = append(result, s)
			} else {
				result = append(result, fmt.Sprintf("%v", item))
			}
		}
		return result
	case []string:
		return v
	}

	return defaultValue
}

// SetInt 设置整数配置值
func (c *Config) SetInt(path string, value int) error {
	return c.set(path, value)
}

// SetFloat 设置浮点数配置值
func (c *Config) SetFloat(path string, value float64) error {
	return c.set(path, value)
}

// SetBool 设置布尔配置值
func (c *Config) SetBool(path string, value bool) error {
	return c.set(path, value)
}

// SetString 设置字符串配置值
func (c *Config) SetString(path string, value string) error {
	return c.set(path, value)
}

// SetStringSlice 设置字符串数组配置值
func (c *Config) SetStringSlice(path string, value []string) error {
	return c.set(path, value)
}

// Exists 检查配置路径是否存在
func (c *Config) Exists(path string) bool {
	_, ok := c.get(path)
	return ok
}

// Watch 监听配置变更
func (c *Config) Watch(path string, callback WatchCallback) {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	if _, ok := c.watchers[path]; !ok {
		c.watchers[path] = make([]WatchCallback, 0)
	}
	c.watchers[path] = append(c.watchers[path], callback)
}

// LoadFromEnv 从环境变量加载配置
// 环境变量格式为 PREFIX_KEY=value，会转换为 key: value
func (c *Config) LoadFromEnv(prefix string) {
	prefix = strings.ToUpper(prefix) + "_"

	for _, env := range os.Environ() {
		if !strings.HasPrefix(env, prefix) {
			continue
		}

		parts := strings.SplitN(env, "=", 2)
		if len(parts) != 2 {
			continue
		}

		key := strings.ToLower(strings.TrimPrefix(parts[0], prefix))
		key = strings.ReplaceAll(key, "_", ".")
		value := parts[1]

		// 尝试解析值
		if value == "true" || value == "false" {
			c.SetBool(key, value == "true")
		} else if i, err := strconv.Atoi(value); err == nil {
			c.SetInt(key, i)
		} else if f, err := strconv.ParseFloat(value, 64); err == nil {
			c.SetFloat(key, f)
		} else {
			c.SetString(key, value)
		}
	}
}

// Merge 合并其他配置
func (c *Config) Merge(other *Config) error {
	c.mutex.Lock()
	defer c.mutex.Unlock()

	other.mutex.RLock()
	defer other.mutex.RUnlock()

	// 递归合并函数
	var mergeMap func(target, source map[string]interface{}) error
	mergeMap = func(target, source map[string]interface{}) error {
		for k, v := range source {
			if sourceMap, ok := v.(map[string]interface{}); ok {
				// 如果源是map，递归合并
				if targetVal, exists := target[k]; exists {
					if targetMap, ok := targetVal.(map[string]interface{}); ok {
						if err := mergeMap(targetMap, sourceMap); err != nil {
							return err
						}
						continue
					}
				}
				// 目标不存在或不是map，直接替换
				newMap := make(map[string]interface{})
				if err := mergeMap(newMap, sourceMap); err != nil {
					return err
				}
				target[k] = newMap
			} else {
				// 非map值直接替换
				target[k] = v
			}
		}
		return nil
	}

	return mergeMap(c.data, other.data)
}
