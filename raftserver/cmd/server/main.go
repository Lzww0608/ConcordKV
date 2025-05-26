package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"raftserver/raft"
	"raftserver/server"
)

var (
	configPath = flag.String("config", "config/server.yaml", "配置文件路径")
	nodeID     = flag.String("node", "", "节点ID")
	listenAddr = flag.String("listen", "", "监听地址")
	apiAddr    = flag.String("api", "", "API服务器地址")
	peers      = flag.String("peers", "", "集群节点列表，用逗号分隔")
	help       = flag.Bool("help", false, "显示帮助信息")
)

func main() {
	flag.Parse()

	if *help {
		printUsage()
		os.Exit(0)
	}

	log.Printf("启动ConcordKV Raft服务器...")

	var srv *server.Server
	var err error

	// 如果提供了命令行参数，使用参数创建服务器
	if *nodeID != "" || *listenAddr != "" || *apiAddr != "" || *peers != "" {
		srv, err = createServerFromFlags()
	} else {
		// 否则从配置文件创建服务器
		srv, err = server.NewServer(*configPath)
	}

	if err != nil {
		log.Fatalf("创建服务器失败: %v", err)
	}

	// 启动服务器
	if err := srv.Start(); err != nil {
		log.Fatalf("启动服务器失败: %v", err)
	}

	// 设置信号处理
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	log.Printf("服务器已启动，按 Ctrl+C 停止")

	// 等待信号
	<-sigChan

	log.Printf("收到停止信号，正在关闭服务器...")

	// 停止服务器
	if err := srv.Stop(); err != nil {
		log.Printf("停止服务器失败: %v", err)
	}

	log.Printf("服务器已关闭")
}

// createServerFromFlags 从命令行参数创建服务器
func createServerFromFlags() (*server.Server, error) {
	if *nodeID == "" {
		return nil, fmt.Errorf("必须指定节点ID")
	}

	listenAddr := getOrDefault(*listenAddr, ":8080")

	config := &server.ServerConfig{
		NodeID:            raft.NodeID(*nodeID),
		ListenAddr:        listenAddr,
		APIAddr:           getOrDefault(*apiAddr, "127.0.0.1:8081"),
		ElectionTimeout:   5 * time.Second,
		HeartbeatInterval: 1 * time.Second,
		MaxLogEntries:     100,
		SnapshotThreshold: 1000,
		Peers:             make(map[raft.NodeID]string),
	}

	// 在单节点模式下，将自己添加到peers列表
	config.Peers[raft.NodeID(*nodeID)] = listenAddr

	// 解析peers参数
	if *peers != "" {
		// 简化的peers解析，格式：node1:addr1,node2:addr2
		// 实际实现中应该更严格
		log.Printf("警告：peers参数解析功能尚未完全实现，请使用配置文件")
	}

	return server.NewServerWithConfig(config)
}

// getOrDefault 获取值或默认值
func getOrDefault(value, defaultValue string) string {
	if value == "" {
		return defaultValue
	}
	return value
}

// printUsage 打印使用说明
func printUsage() {
	fmt.Printf("ConcordKV Raft 服务器\n\n")
	fmt.Printf("用法:\n")
	fmt.Printf("  %s [选项]\n\n", filepath.Base(os.Args[0]))
	fmt.Printf("选项:\n")
	fmt.Printf("  -config string\n")
	fmt.Printf("        配置文件路径 (默认 \"config/server.yaml\")\n")
	fmt.Printf("  -node string\n")
	fmt.Printf("        节点ID\n")
	fmt.Printf("  -listen string\n")
	fmt.Printf("        Raft监听地址\n")
	fmt.Printf("  -api string\n")
	fmt.Printf("        API服务器地址\n")
	fmt.Printf("  -peers string\n")
	fmt.Printf("        集群节点列表，用逗号分隔\n")
	fmt.Printf("  -help\n")
	fmt.Printf("        显示帮助信息\n\n")
	fmt.Printf("示例:\n")
	fmt.Printf("  # 使用配置文件启动\n")
	fmt.Printf("  %s -config config/node1.yaml\n\n", filepath.Base(os.Args[0]))
	fmt.Printf("  # 使用命令行参数启动\n")
	fmt.Printf("  %s -node node1 -listen :8080 -api :8081\n\n", filepath.Base(os.Args[0]))
	fmt.Printf("API 端点:\n")
	fmt.Printf("  GET  /api/get?key=<key>     - 获取键值\n")
	fmt.Printf("  POST /api/set               - 设置键值\n")
	fmt.Printf("  DEL  /api/delete?key=<key>  - 删除键值\n")
	fmt.Printf("  GET  /api/keys              - 获取所有键\n")
	fmt.Printf("  GET  /api/status            - 获取节点状态\n")
	fmt.Printf("  GET  /api/metrics           - 获取详细指标\n")
	fmt.Printf("  GET  /api/logs              - 获取调试日志\n")
}
