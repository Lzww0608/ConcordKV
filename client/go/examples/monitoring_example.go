package main

import (
	"fmt"
	"log"
	"time"

	"github.com/concordkv/client/go/concord"
)

func main() {
	// 创建客户端配置
	config := concord.Config{
		Endpoints:     []string{"127.0.0.1:5001", "127.0.0.1:5002", "127.0.0.1:5003"},
		Timeout:       3 * time.Second,
		RetryCount:    3,
		RetryInterval: 500 * time.Millisecond,
		EnableCache:   true,
		CacheSize:     1000,
		CacheTTL:      5 * time.Minute,
	}

	// 创建客户端
	client, err := concord.NewClient(config)
	if err != nil {
		log.Fatalf("创建客户端失败: %v", err)
	}
	defer client.Close()

	// 启用监控（每30秒检查一次）
	if err := client.EnableMonitoring(30 * time.Second); err != nil {
		log.Printf("启用监控失败: %v", err)
	}

	// 获取集群健康状态
	stats, err := client.GetClusterHealth()
	if err != nil {
		log.Printf("获取集群健康状态失败: %v", err)
	} else {
		// 显示集群统计信息
		fmt.Println("==== 集群健康状态 ====")
		fmt.Printf("总节点数: %d\n", stats.TotalNodes)
		fmt.Printf("健康节点数: %d\n", stats.HealthyNodes)
		fmt.Printf("平均延迟: %dms\n", stats.AvgLatency)
		fmt.Printf("总请求数: %d\n", stats.TotalRequests)
		fmt.Printf("上次更新时间: %v\n", stats.LastUpdated)

		// 显示各节点状态
		fmt.Println("\n==== 节点状态详情 ====")
		for endpoint, nodeStat := range stats.NodeStats {
			fmt.Printf("节点: %s\n", endpoint)
			fmt.Printf("  状态: %s\n", nodeStat.Status)
			fmt.Printf("  延迟: %dms\n", nodeStat.Latency)
			fmt.Printf("  成功率: %.2f%%\n", nodeStat.SuccessRate*100)
			fmt.Printf("  请求数: %d\n", nodeStat.RequestCount)
			fmt.Printf("  错误数: %d\n", nodeStat.ErrorCount)
			fmt.Printf("  上次检查时间: %v\n\n", nodeStat.LastChecked)
		}
	}

	// 执行一些基本操作
	fmt.Println("执行基本操作...")

	// 设置键值
	err = client.Set("monitoring_test", "监控测试值")
	if err != nil {
		log.Printf("设置键值失败: %v", err)
	} else {
		fmt.Println("已设置键 'monitoring_test'")
	}

	// 获取键值
	value, err := client.Get("monitoring_test")
	if err != nil {
		log.Printf("获取键值失败: %v", err)
	} else {
		fmt.Printf("键 'monitoring_test' 的值: %s\n", value)
	}

	// 再次获取健康状态（操作后）
	stats, err = client.GetClusterHealth()
	if err != nil {
		log.Printf("获取集群健康状态失败: %v", err)
	} else {
		fmt.Println("\n==== 操作后集群状态 ====")
		fmt.Printf("总请求数: %d\n", stats.TotalRequests)
		fmt.Printf("平均延迟: %dms\n", stats.AvgLatency)
	}

	fmt.Println("\n监控示例完成")
}
