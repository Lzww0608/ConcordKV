/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Raft consensus server - cluster_manager.go
 */
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"
)

// ClusterManager 集群管理工具
type ClusterManager struct {
	client  *http.Client
	baseURL string
}

// ServerInfo 服务器信息
type ServerInfo struct {
	ID      string `json:"id"`
	Address string `json:"address"`
}

// ConfigurationResponse 配置响应
type ConfigurationResponse struct {
	Configuration struct {
		Servers []ServerInfo `json:"servers"`
	} `json:"configuration"`
	Changing bool `json:"changing"`
}

// APIResponse 通用API响应
type APIResponse struct {
	Success bool        `json:"success"`
	Message string      `json:"message"`
	Error   string      `json:"error"`
	Leader  string      `json:"leader"`
	Data    interface{} `json:"data"`
}

// NewClusterManager 创建新的集群管理器
func NewClusterManager(baseURL string) *ClusterManager {
	return &ClusterManager{
		client: &http.Client{
			Timeout: 30 * time.Second,
		},
		baseURL: strings.TrimSuffix(baseURL, "/"),
	}
}

// AddServer 添加服务器到集群
func (cm *ClusterManager) AddServer(ctx context.Context, serverID, address string) error {
	reqBody := map[string]string{
		"id":      serverID,
		"address": address,
	}

	data, err := json.Marshal(reqBody)
	if err != nil {
		return fmt.Errorf("序列化请求失败: %w", err)
	}

	url := fmt.Sprintf("%s/api/cluster/add", cm.baseURL)
	req, err := http.NewRequestWithContext(ctx, "POST", url, bytes.NewReader(data))
	if err != nil {
		return fmt.Errorf("创建请求失败: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")

	resp, err := cm.client.Do(req)
	if err != nil {
		return fmt.Errorf("发送请求失败: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %w", err)
	}

	var apiResp APIResponse
	if err := json.Unmarshal(body, &apiResp); err != nil {
		return fmt.Errorf("解析响应失败: %w", err)
	}

	if !apiResp.Success {
		if apiResp.Error == "不是领导者" && apiResp.Leader != "" {
			return fmt.Errorf("当前节点不是领导者，请联系领导者: %s", apiResp.Leader)
		}
		return fmt.Errorf("添加服务器失败: %s", apiResp.Error)
	}

	fmt.Printf("✓ 成功添加服务器: %s (%s)\n", serverID, address)
	return nil
}

// RemoveServer 从集群中移除服务器
func (cm *ClusterManager) RemoveServer(ctx context.Context, serverID string) error {
	reqBody := map[string]string{
		"id": serverID,
	}

	data, err := json.Marshal(reqBody)
	if err != nil {
		return fmt.Errorf("序列化请求失败: %w", err)
	}

	url := fmt.Sprintf("%s/api/cluster/remove", cm.baseURL)
	req, err := http.NewRequestWithContext(ctx, "POST", url, bytes.NewReader(data))
	if err != nil {
		return fmt.Errorf("创建请求失败: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")

	resp, err := cm.client.Do(req)
	if err != nil {
		return fmt.Errorf("发送请求失败: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %w", err)
	}

	var apiResp APIResponse
	if err := json.Unmarshal(body, &apiResp); err != nil {
		return fmt.Errorf("解析响应失败: %w", err)
	}

	if !apiResp.Success {
		if apiResp.Error == "不是领导者" && apiResp.Leader != "" {
			return fmt.Errorf("当前节点不是领导者，请联系领导者: %s", apiResp.Leader)
		}
		return fmt.Errorf("移除服务器失败: %s", apiResp.Error)
	}

	fmt.Printf("✓ 成功移除服务器: %s\n", serverID)
	return nil
}

// GetConfiguration 获取集群配置
func (cm *ClusterManager) GetConfiguration(ctx context.Context) (*ConfigurationResponse, error) {
	url := fmt.Sprintf("%s/api/cluster/config", cm.baseURL)
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return nil, fmt.Errorf("创建请求失败: %w", err)
	}

	resp, err := cm.client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("发送请求失败: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("读取响应失败: %w", err)
	}

	var configResp ConfigurationResponse
	if err := json.Unmarshal(body, &configResp); err != nil {
		return nil, fmt.Errorf("解析响应失败: %w", err)
	}

	return &configResp, nil
}

// ListServers 列出集群中的所有服务器
func (cm *ClusterManager) ListServers(ctx context.Context) error {
	config, err := cm.GetConfiguration(ctx)
	if err != nil {
		return fmt.Errorf("获取配置失败: %w", err)
	}

	fmt.Println("=== 集群配置 ===")
	fmt.Printf("配置变更中: %v\n", config.Changing)
	fmt.Printf("服务器数量: %d\n\n", len(config.Configuration.Servers))

	if len(config.Configuration.Servers) == 0 {
		fmt.Println("集群中没有服务器")
		return nil
	}

	fmt.Println("服务器列表:")
	for i, server := range config.Configuration.Servers {
		fmt.Printf("  %d. ID: %s, 地址: %s\n", i+1, server.ID, server.Address)
	}

	return nil
}

// GetStatus 获取节点状态
func (cm *ClusterManager) GetStatus(ctx context.Context) error {
	url := fmt.Sprintf("%s/api/status", cm.baseURL)
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return fmt.Errorf("创建请求失败: %w", err)
	}

	resp, err := cm.client.Do(req)
	if err != nil {
		return fmt.Errorf("发送请求失败: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %w", err)
	}

	var status map[string]interface{}
	if err := json.Unmarshal(body, &status); err != nil {
		return fmt.Errorf("解析响应失败: %w", err)
	}

	fmt.Println("=== 节点状态 ===")
	for key, value := range status {
		fmt.Printf("%s: %v\n", key, value)
	}

	return nil
}

// 使用说明
func printUsage() {
	fmt.Println("ConcordKV 集群管理工具")
	fmt.Println()
	fmt.Println("用法:")
	fmt.Println("  cluster_manager [选项] <命令> [参数...]")
	fmt.Println()
	fmt.Println("选项:")
	fmt.Println("  -server string    服务器地址 (默认: http://127.0.0.1:21081)")
	fmt.Println("  -timeout int      请求超时时间，秒 (默认: 30)")
	fmt.Println("  -h, -help         显示帮助信息")
	fmt.Println()
	fmt.Println("命令:")
	fmt.Println("  add <id> <address>    添加服务器到集群")
	fmt.Println("  remove <id>           从集群中移除服务器")
	fmt.Println("  list                  列出集群中的所有服务器")
	fmt.Println("  status                显示节点状态")
	fmt.Println()
	fmt.Println("示例:")
	fmt.Println("  cluster_manager add node4 :24080")
	fmt.Println("  cluster_manager remove node4")
	fmt.Println("  cluster_manager list")
	fmt.Println("  cluster_manager -server http://192.168.1.100:21081 status")
}

func main() {
	var (
		serverAddr = flag.String("server", "http://127.0.0.1:21081", "服务器地址")
		timeout    = flag.Int("timeout", 30, "请求超时时间，秒")
		help       = flag.Bool("help", false, "显示帮助信息")
	)
	flag.BoolVar(help, "h", false, "显示帮助信息")
	flag.Parse()

	if *help {
		printUsage()
		return
	}

	args := flag.Args()
	if len(args) == 0 {
		fmt.Println("错误: 缺少命令")
		fmt.Println()
		printUsage()
		os.Exit(1)
	}

	// 创建集群管理器
	cm := NewClusterManager(*serverAddr)
	cm.client.Timeout = time.Duration(*timeout) * time.Second

	// 创建上下文
	ctx, cancel := context.WithTimeout(context.Background(), cm.client.Timeout)
	defer cancel()

	command := args[0]

	switch command {
	case "add":
		if len(args) != 3 {
			fmt.Println("错误: add 命令需要 2 个参数: <id> <address>")
			os.Exit(1)
		}

		serverID := args[1]
		address := args[2]

		if err := cm.AddServer(ctx, serverID, address); err != nil {
			fmt.Printf("错误: %v\n", err)
			os.Exit(1)
		}

	case "remove":
		if len(args) != 2 {
			fmt.Println("错误: remove 命令需要 1 个参数: <id>")
			os.Exit(1)
		}

		serverID := args[1]

		if err := cm.RemoveServer(ctx, serverID); err != nil {
			fmt.Printf("错误: %v\n", err)
			os.Exit(1)
		}

	case "list":
		if err := cm.ListServers(ctx); err != nil {
			fmt.Printf("错误: %v\n", err)
			os.Exit(1)
		}

	case "status":
		if err := cm.GetStatus(ctx); err != nil {
			fmt.Printf("错误: %v\n", err)
			os.Exit(1)
		}

	default:
		fmt.Printf("错误: 未知命令 '%s'\n", command)
		fmt.Println()
		printUsage()
		os.Exit(1)
	}
}
