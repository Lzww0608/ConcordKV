package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"time"
)

func main() {
	log.Printf("ConcordKV Raft 客户端测试程序")

	// 测试服务器地址
	serverAddr := "http://localhost:8081"

	// 等待服务器启动
	log.Printf("等待服务器启动...")
	time.Sleep(time.Second * 2)

	// 测试服务器状态
	log.Printf("检查服务器状态...")
	if err := checkStatus(serverAddr); err != nil {
		log.Fatalf("检查状态失败: %v", err)
	}

	// 测试键值操作
	log.Printf("测试键值操作...")
	if err := testKVOperations(serverAddr); err != nil {
		log.Fatalf("测试键值操作失败: %v", err)
	}

	log.Printf("测试完成!")
}

// checkStatus 检查服务器状态
func checkStatus(serverAddr string) error {
	resp, err := http.Get(serverAddr + "/api/status")
	if err != nil {
		return fmt.Errorf("请求状态失败: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %w", err)
	}

	var status map[string]interface{}
	if err := json.Unmarshal(body, &status); err != nil {
		return fmt.Errorf("解析状态响应失败: %w", err)
	}

	log.Printf("服务器状态: %+v", status)
	return nil
}

// testKVOperations 测试键值操作
func testKVOperations(serverAddr string) error {
	// 测试SET操作
	log.Printf("测试SET操作...")
	if err := testSet(serverAddr, "test_key", "test_value"); err != nil {
		return fmt.Errorf("SET操作失败: %w", err)
	}

	// 等待一下让命令被应用
	time.Sleep(time.Millisecond * 100)

	// 测试GET操作
	log.Printf("测试GET操作...")
	value, exists, err := testGet(serverAddr, "test_key")
	if err != nil {
		return fmt.Errorf("GET操作失败: %w", err)
	}

	if !exists {
		return fmt.Errorf("键不存在")
	}

	if value != "test_value" {
		return fmt.Errorf("值不匹配，期望: test_value, 实际: %v", value)
	}

	log.Printf("GET操作成功，值: %v", value)

	// 测试多个键值
	log.Printf("测试多个键值...")
	testData := map[string]interface{}{
		"key1": "value1",
		"key2": 42,
		"key3": true,
		"key4": map[string]interface{}{"nested": "object"},
	}

	for key, val := range testData {
		if err := testSet(serverAddr, key, val); err != nil {
			return fmt.Errorf("设置键 %s 失败: %w", key, err)
		}
	}

	// 获取所有键
	log.Printf("获取所有键...")
	keys, err := testKeys(serverAddr)
	if err != nil {
		return fmt.Errorf("获取键列表失败: %w", err)
	}

	log.Printf("所有键: %v", keys)

	// 测试DELETE操作
	log.Printf("测试DELETE操作...")
	if err := testDelete(serverAddr, "key1"); err != nil {
		return fmt.Errorf("DELETE操作失败: %w", err)
	}

	// 验证删除成功
	time.Sleep(time.Millisecond * 100)
	_, exists, err = testGet(serverAddr, "key1")
	if err != nil {
		return fmt.Errorf("验证删除失败: %w", err)
	}

	if exists {
		return fmt.Errorf("键删除失败，键仍然存在")
	}

	log.Printf("DELETE操作成功")

	return nil
}

// testSet 测试SET操作
func testSet(serverAddr, key string, value interface{}) error {
	reqData := map[string]interface{}{
		"key":   key,
		"value": value,
	}

	reqJSON, err := json.Marshal(reqData)
	if err != nil {
		return fmt.Errorf("序列化请求失败: %w", err)
	}

	resp, err := http.Post(serverAddr+"/api/set", "application/json", bytes.NewBuffer(reqJSON))
	if err != nil {
		return fmt.Errorf("发送SET请求失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("SET请求失败，状态码: %d, 响应: %s", resp.StatusCode, string(body))
	}

	var result map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return fmt.Errorf("解析SET响应失败: %w", err)
	}

	if success, ok := result["success"].(bool); !ok || !success {
		return fmt.Errorf("SET操作失败: %+v", result)
	}

	return nil
}

// testGet 测试GET操作
func testGet(serverAddr, key string) (interface{}, bool, error) {
	resp, err := http.Get(fmt.Sprintf("%s/api/get?key=%s", serverAddr, key))
	if err != nil {
		return nil, false, fmt.Errorf("发送GET请求失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, false, fmt.Errorf("GET请求失败，状态码: %d, 响应: %s", resp.StatusCode, string(body))
	}

	var result map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, false, fmt.Errorf("解析GET响应失败: %w", err)
	}

	exists, ok := result["exists"].(bool)
	if !ok {
		return nil, false, fmt.Errorf("无效的响应格式")
	}

	if exists {
		return result["value"], true, nil
	}

	return nil, false, nil
}

// testDelete 测试DELETE操作
func testDelete(serverAddr, key string) error {
	req, err := http.NewRequest("DELETE", fmt.Sprintf("%s/api/delete?key=%s", serverAddr, key), nil)
	if err != nil {
		return fmt.Errorf("创建DELETE请求失败: %w", err)
	}

	client := &http.Client{}
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("发送DELETE请求失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("DELETE请求失败，状态码: %d, 响应: %s", resp.StatusCode, string(body))
	}

	var result map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return fmt.Errorf("解析DELETE响应失败: %w", err)
	}

	if success, ok := result["success"].(bool); !ok || !success {
		return fmt.Errorf("DELETE操作失败: %+v", result)
	}

	return nil
}

// testKeys 测试获取所有键
func testKeys(serverAddr string) ([]interface{}, error) {
	resp, err := http.Get(serverAddr + "/api/keys")
	if err != nil {
		return nil, fmt.Errorf("发送KEYS请求失败: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("KEYS请求失败，状态码: %d, 响应: %s", resp.StatusCode, string(body))
	}

	var result map[string]interface{}
	if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
		return nil, fmt.Errorf("解析KEYS响应失败: %w", err)
	}

	keys, ok := result["keys"].([]interface{})
	if !ok {
		return nil, fmt.Errorf("无效的响应格式")
	}

	return keys, nil
}
