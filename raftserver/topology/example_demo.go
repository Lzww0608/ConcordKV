/*
* @Author: Lzww0608
* @Date: 2025-6-30 22:29:23
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-30 22:29:26
* @Description: ConcordKV中央拓扑服务 - 完整示例和演示
 */
package main

import (
	"fmt"
	"log"
	"net/http"
	"time"

	"raftserver/topology"
)

// 演示中央拓扑服务的完整功能
func main() {
	fmt.Println("🚀 ConcordKV 中央拓扑服务演示")
	fmt.Println("================================")

	// 1. 创建拓扑服务配置
	topologyConfig := &topology.TopologyConfig{
		Enabled:             true,
		HealthCheckInterval: time.Second * 10,
		HealthCheckTimeout:  time.Second * 3,
		CacheTTL:            time.Minute * 2,
		MaxSubscribers:      100,
		EventBufferSize:     1000,
		SnapshotInterval:    time.Second * 30,
	}

	// 2. 创建变更通知器配置
	notifierConfig := &topology.NotifierConfig{
		EventBufferSize:     1000,
		SubscriptionTimeout: time.Minute * 5,
		MaxEventHistory:     1000,
		HeartbeatInterval:   time.Second * 30,
	}

	fmt.Println("📋 1. 初始化拓扑服务组件...")

	// 3. 创建拓扑服务（使用模拟组件）
	topologyService := topology.NewTopologyService(nil, nil, nil, topologyConfig)

	// 4. 创建变更通知器
	changeNotifier := topology.NewChangeNotifier(notifierConfig)

	// 5. 创建API处理器
	apiHandler := topology.NewTopologyAPIHandler(topologyService, changeNotifier)

	fmt.Println("🔧 2. 启动拓扑服务...")

	// 6. 启动拓扑服务
	if err := topologyService.Start(); err != nil {
		log.Fatalf("启动拓扑服务失败: %v", err)
	}
	defer topologyService.Stop()

	// 7. 启动变更通知器
	if err := changeNotifier.Start(); err != nil {
		log.Fatalf("启动变更通知器失败: %v", err)
	}
	defer changeNotifier.Stop()

	// 8. 设置HTTP API服务器
	mux := http.NewServeMux()

	// 设置拓扑API端点
	apiHandler.SetupTopologyAPI(mux)

	// 添加基础健康检查端点
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, `{"status":"ok","service":"topology","timestamp":"%s"}`,
			time.Now().Format(time.RFC3339))
	})

	// 添加根路径信息
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html")
		fmt.Fprintf(w, `
		<h1>ConcordKV 中央拓扑服务</h1>
		<h2>可用的API端点:</h2>
		<ul>
			<li><a href="/api/topology/snapshot">GET /api/topology/snapshot</a> - 获取拓扑快照</li>
			<li><a href="/api/topology/nodes">GET /api/topology/nodes</a> - 获取节点状态</li>
			<li><a href="/api/topology/shards">GET /api/topology/shards</a> - 获取分片映射</li>
			<li><a href="/api/topology/health">GET /api/topology/health</a> - 拓扑服务健康检查</li>
			<li><a href="/api/topology/version">GET /api/topology/version</a> - 获取拓扑版本</li>
			<li><a href="/api/topology/stats">GET /api/topology/stats</a> - 获取拓扑统计</li>
			<li><a href="/api/topology/subscribe">GET /api/topology/subscribe</a> - SSE事件订阅</li>
			<li><a href="/api/topology/events">GET /api/topology/events</a> - 获取事件历史</li>
		</ul>
		<h2>示例命令:</h2>
		<pre>
		# 获取拓扑快照
		curl http://localhost:8081/api/topology/snapshot
		
		# 获取节点状态
		curl http://localhost:8081/api/topology/nodes
		
		# SSE事件订阅
		curl -N http://localhost:8081/api/topology/subscribe
		</pre>
		`)
	})

	// 9. 启动HTTP服务器
	server := &http.Server{
		Addr:    ":8081",
		Handler: apiHandler.TopologyMiddleware(mux),
	}

	fmt.Println("🌐 3. 启动HTTP API服务器 (端口: 8081)...")

	go func() {
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("HTTP服务器错误: %v", err)
		}
	}()

	// 等待服务器启动
	time.Sleep(time.Second)

	fmt.Println("✅ 4. 拓扑服务启动完成!")
	fmt.Println()
	fmt.Println("📊 5. 演示拓扑服务功能...")

	// 10. 演示拓扑服务功能
	demonstrateTopologyService(topologyService, changeNotifier)

	fmt.Println()
	fmt.Println("🎯 6. 演示客户端功能...")

	// 11. 演示客户端功能
	demonstrateTopologyClient()

	fmt.Println()
	fmt.Println("🔄 服务正在运行中...")
	fmt.Println("访问 http://localhost:8081 查看API文档")
	fmt.Println("按 Ctrl+C 停止服务")

	// 12. 保持服务运行
	select {}
}

// demonstrateTopologyService 演示拓扑服务功能
func demonstrateTopologyService(service *topology.TopologyService, notifier *topology.ChangeNotifier) {
	// 获取拓扑快照
	fmt.Println("  📸 获取拓扑快照...")
	snapshot, err := service.GetTopologySnapshot()
	if err != nil {
		log.Printf("获取拓扑快照失败: %v", err)
		return
	}

	fmt.Printf("    ✅ 快照版本: %d\n", snapshot.Version)
	fmt.Printf("    ✅ 节点数量: %d\n", len(snapshot.NodeStatus))
	fmt.Printf("    ✅ 分片数量: %d\n", len(snapshot.ShardMap))
	fmt.Printf("    ✅ 时间戳: %s\n", snapshot.Timestamp.Format("15:04:05"))

	// 订阅拓扑事件
	fmt.Println("  📡 订阅拓扑事件...")
	subscriber, err := service.Subscribe("demo-client", []topology.EventType{
		topology.EventNodeHealthChange,
		topology.EventConfigChange,
	})
	if err != nil {
		log.Printf("订阅事件失败: %v", err)
		return
	}

	fmt.Printf("    ✅ 订阅ID: %s\n", subscriber.ID)
	fmt.Printf("    ✅ 事件类型: %d个\n", len(subscriber.EventTypes))

	// 发布测试事件
	fmt.Println("  🚀 发布测试事件...")
	testEvent := &topology.TopologyEvent{
		Type: topology.EventConfigChange,
		Metadata: map[string]interface{}{
			"source": "demo",
			"action": "configuration_update",
		},
	}

	service.PublishEvent(testEvent)

	// 尝试接收事件
	go func() {
		select {
		case event := <-subscriber.EventChan:
			fmt.Printf("    ✅ 接收到事件: 类型=%s, 版本=%d\n",
				event.Type.String(), event.Version)
		case <-time.After(time.Second * 3):
			fmt.Println("    ⏰ 事件接收超时")
		}
	}()

	time.Sleep(time.Second * 2)

	// 显示通知器统计
	if notifier != nil {
		activeCount := notifier.GetActiveSubscriptions()
		fmt.Printf("  📊 活跃订阅数: %d\n", activeCount)
	}
}

// demonstrateTopologyClient 演示客户端功能
func demonstrateTopologyClient() {
	// 创建客户端配置
	clientConfig := &topology.ClientConfig{
		ServerURLs:        []string{"http://localhost:8081"},
		RequestTimeout:    time.Second * 5,
		RetryInterval:     time.Second * 2,
		MaxRetries:        3,
		HealthCheckPeriod: time.Second * 30,
		CachePolicy: &topology.CachePolicy{
			TTL:              time.Minute * 2,
			RefreshThreshold: time.Minute,
			MaxEntries:       1000,
			CompressionLevel: 1,
		},
	}

	// 创建拓扑客户端
	fmt.Println("  🔧 创建拓扑客户端...")
	topologyClient := topology.NewTopologyClient(clientConfig)

	fmt.Printf("    ✅ 服务器数量: %d\n", len(clientConfig.ServerURLs))
	fmt.Printf("    ✅ 请求超时: %s\n", clientConfig.RequestTimeout)
	fmt.Printf("    ✅ 缓存TTL: %s\n", clientConfig.CachePolicy.TTL)

	// 显示客户端能力
	fmt.Println("  🎯 客户端主要功能:")
	fmt.Println("    • 智能分片路由")
	fmt.Println("    • 本地拓扑缓存")
	fmt.Println("    • 实时事件订阅")
	fmt.Println("    • 自动故障切换")
	fmt.Println("    • 负载感知路由")

	// 获取当前版本（客户端未启动，所以是0）
	version := topologyClient.GetTopologyVersion()
	fmt.Printf("    ✅ 当前拓扑版本: %d\n", version)
}

// 辅助函数用于优雅关闭
func gracefulShutdown() {
	fmt.Println("\n🛑 正在优雅关闭服务...")
	// 这里可以添加清理逻辑
	fmt.Println("✅ 服务已关闭")
}
