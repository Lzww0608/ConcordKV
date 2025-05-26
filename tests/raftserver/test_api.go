// 注意：此程序需要在raftserver目录下运行
// 使用方法：cd ../../raftserver && go run ../tests/raftserver/test_api.go
package main

import (
	"encoding/json"
	"log"
	"net/http"
	"time"

	"raftserver/raft"
	"raftserver/server"
)

func main() {
	log.Printf("启动简化的API测试服务器...")

	// 创建服务器配置
	config := &server.ServerConfig{
		NodeID:            raft.NodeID("test-node"),
		ListenAddr:        "127.0.0.1:24080",
		APIAddr:           "127.0.0.1:24081",
		ElectionTimeout:   5 * time.Second,
		HeartbeatInterval: 1 * time.Second,
		MaxLogEntries:     100,
		SnapshotThreshold: 1000,
		Peers:             make(map[raft.NodeID]string),
	}

	// 添加自己到peers列表
	config.Peers[config.NodeID] = config.ListenAddr

	// 创建服务器
	srv, err := server.NewServerWithConfig(config)
	if err != nil {
		log.Fatalf("创建服务器失败: %v", err)
	}

	// 启动服务器
	if err := srv.Start(); err != nil {
		log.Fatalf("启动服务器失败: %v", err)
	}

	log.Printf("服务器已启动，等待选举完成...")
	time.Sleep(10 * time.Second)

	// 测试API
	testAPI()

	// 停止服务器
	if err := srv.Stop(); err != nil {
		log.Printf("停止服务器失败: %v", err)
	}

	log.Printf("测试完成")
}

func testAPI() {
	baseURL := "http://127.0.0.1:24081"

	// 测试状态API
	log.Printf("测试状态API...")
	resp, err := http.Get(baseURL + "/api/status")
	if err != nil {
		log.Printf("状态API请求失败: %v", err)
		return
	}
	defer resp.Body.Close()

	if resp.StatusCode == 200 {
		var result map[string]interface{}
		if err := json.NewDecoder(resp.Body).Decode(&result); err != nil {
			log.Printf("解析状态API响应失败: %v", err)
		} else {
			log.Printf("状态API响应: %+v", result)
		}
	} else {
		log.Printf("状态API返回错误状态码: %d", resp.StatusCode)
	}

	// 测试键值API
	log.Printf("测试键值API...")
	resp2, err := http.Get(baseURL + "/api/keys")
	if err != nil {
		log.Printf("键值API请求失败: %v", err)
		return
	}
	defer resp2.Body.Close()

	if resp2.StatusCode == 200 {
		var result map[string]interface{}
		if err := json.NewDecoder(resp2.Body).Decode(&result); err != nil {
			log.Printf("解析键值API响应失败: %v", err)
		} else {
			log.Printf("键值API响应: %+v", result)
		}
	} else {
		log.Printf("键值API返回错误状态码: %d", resp2.StatusCode)
	}
}
