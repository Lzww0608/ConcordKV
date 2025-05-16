package main

import (
	"fmt"
	"log"
	"time"

	"github.com/concordkv/client/go/concord"
)

func main() {
	// 创建基本客户端配置
	config := concord.Config{
		Endpoints:   []string{"127.0.0.1:5001"}, // 初始只需要知道一个节点
		Timeout:     3 * time.Second,
		RetryCount:  3,
		EnableCache: true,
		CacheSize:   1000,
		CacheTTL:    5 * time.Minute,
	}

	// 创建客户端
	client, err := concord.NewClient(config)
	if err != nil {
		log.Fatalf("创建客户端失败: %v", err)
	}
	defer client.Close()

	// 启用监控（为负载均衡提供性能数据）
	err = client.EnableMonitoring(30 * time.Second)
	if err != nil {
		log.Printf("启用监控失败: %v", err)
	}

	// 配置节点发现
	discoveryConfig := concord.DiscoveryConfig{
		Type:               concord.DiscoveryTypeService,  // 使用服务注册中心发现
		ServiceRegistryURL: "http://registry.example.com", // 服务注册中心地址
		ServiceName:        "concordkv",                   // 服务名称
		RefreshInterval:    60 * time.Second,              // 每60秒刷新一次
		BalanceStrategy:    concord.BalanceWeighted,       // 使用加权负载均衡
		AutoDiscover:       true,                          // 自动发现新节点
	}

	// 启用节点发现和负载均衡
	err = client.EnableDiscovery(discoveryConfig)
	if err != nil {
		log.Printf("启用节点发现失败: %v", err)
	}

	fmt.Println("客户端已启用节点发现和负载均衡")

	// 等待一段时间让节点发现完成
	time.Sleep(2 * time.Second)

	// 执行一些基本操作，这些操作会自动使用负载均衡
	for i := 0; i < 10; i++ {
		key := fmt.Sprintf("key%d", i)
		value := fmt.Sprintf("value%d", i)

		// 设置键值
		err = client.Set(key, value)
		if err != nil {
			log.Printf("设置键值失败: %v", err)
			continue
		}
		fmt.Printf("已设置键 '%s'\n", key)

		// 获取键值
		val, err := client.Get(key)
		if err != nil {
			log.Printf("获取键值失败: %v", err)
		} else {
			fmt.Printf("键 '%s' 的值: %s\n", key, val)
		}
	}

	// 获取集群健康状态
	stats, err := client.GetClusterHealth()
	if err != nil {
		log.Printf("获取集群健康状态失败: %v", err)
	} else {
		fmt.Printf("\n集群状态:\n")
		fmt.Printf("总节点数: %d\n", stats.TotalNodes)
		fmt.Printf("健康节点数: %d\n", stats.HealthyNodes)
		fmt.Printf("平均延迟: %dms\n", stats.AvgLatency)

		// 显示各节点统计信息
		fmt.Printf("\n节点统计信息:\n")
		for endpoint, nodeStat := range stats.NodeStats {
			fmt.Printf("节点: %s\n", endpoint)
			fmt.Printf("  状态: %s\n", nodeStat.Status)
			fmt.Printf("  延迟: %dms\n", nodeStat.Latency)
			fmt.Printf("  成功率: %.2f%%\n", nodeStat.SuccessRate*100)
			fmt.Printf("  请求数: %d\n", nodeStat.RequestCount)
		}
	}

	fmt.Println("\n节点发现和负载均衡示例完成")
}
