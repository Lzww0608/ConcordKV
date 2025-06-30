/*
* @Author: Lzww0608
* @Date: 2025-6-30 22:24:45
* @LastEditors: Lzww0608
* @LastEditTime: 2025-06-30 22:24:45
* @Description: ConcordKV中央拓扑服务 - 服务器API扩展
 */
package topology

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"time"

	"raftserver/raft"
)

// TopologyAPIHandler 拓扑API处理器
// 扩展现有server.go的HTTP API框架
type TopologyAPIHandler struct {
	topologyService *TopologyService
	changeNotifier  *ChangeNotifier
}

// NewTopologyAPIHandler 创建拓扑API处理器
func NewTopologyAPIHandler(topologyService *TopologyService, changeNotifier *ChangeNotifier) *TopologyAPIHandler {
	return &TopologyAPIHandler{
		topologyService: topologyService,
		changeNotifier:  changeNotifier,
	}
}

// SetupTopologyAPI 设置拓扑管理API
// 扩展现有server.go的API端点
func (handler *TopologyAPIHandler) SetupTopologyAPI(mux *http.ServeMux) {
	// 拓扑查询API
	mux.HandleFunc("/api/topology/snapshot", handler.handleTopologySnapshot)
	mux.HandleFunc("/api/topology/shards", handler.handleShardMapping)
	mux.HandleFunc("/api/topology/nodes", handler.handleNodeStatus)

	// 事件订阅API
	mux.HandleFunc("/api/topology/subscribe", handler.handleTopologySubscribe)
	mux.HandleFunc("/api/topology/events", handler.handleTopologyEvents)

	// 管理操作API
	mux.HandleFunc("/api/topology/health", handler.handleHealthCheck)
	mux.HandleFunc("/api/topology/version", handler.handleTopologyVersion)
	mux.HandleFunc("/api/topology/stats", handler.handleTopologyStats)
}

// handleTopologySnapshot 处理拓扑快照查询
func (handler *TopologyAPIHandler) handleTopologySnapshot(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	// 获取当前拓扑快照
	snapshot, err := handler.topologyService.GetTopologySnapshot()
	if err != nil {
		http.Error(w, fmt.Sprintf("获取拓扑快照失败: %v", err), http.StatusInternalServerError)
		return
	}

	// 返回JSON响应
	handler.sendJSONResponse(w, map[string]interface{}{
		"success":  true,
		"snapshot": snapshot,
		"message":  "拓扑快照获取成功",
	})
}

// handleShardMapping 处理分片映射查询
func (handler *TopologyAPIHandler) handleShardMapping(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	snapshot, err := handler.topologyService.GetTopologySnapshot()
	if err != nil {
		http.Error(w, fmt.Sprintf("获取分片映射失败: %v", err), http.StatusInternalServerError)
		return
	}

	// 可选：过滤特定分片
	shardID := r.URL.Query().Get("shardId")
	if shardID != "" {
		if shardInfo, exists := snapshot.ShardMap[shardID]; exists {
			handler.sendJSONResponse(w, map[string]interface{}{
				"success": true,
				"shard":   shardInfo,
				"message": fmt.Sprintf("分片 %s 信息获取成功", shardID),
			})
		} else {
			http.Error(w, fmt.Sprintf("分片 %s 不存在", shardID), http.StatusNotFound)
		}
		return
	}

	// 返回所有分片映射
	handler.sendJSONResponse(w, map[string]interface{}{
		"success":     true,
		"shardMap":    snapshot.ShardMap,
		"totalShards": len(snapshot.ShardMap),
		"message":     "分片映射获取成功",
	})
}

// handleNodeStatus 处理节点状态查询
func (handler *TopologyAPIHandler) handleNodeStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	snapshot, err := handler.topologyService.GetTopologySnapshot()
	if err != nil {
		http.Error(w, fmt.Sprintf("获取节点状态失败: %v", err), http.StatusInternalServerError)
		return
	}

	// 可选：过滤特定节点
	nodeIDParam := r.URL.Query().Get("nodeId")
	if nodeIDParam != "" {
		nodeID := raft.NodeID(nodeIDParam)
		if nodeStatus, exists := snapshot.NodeStatus[nodeID]; exists {
			handler.sendJSONResponse(w, map[string]interface{}{
				"success": true,
				"node":    nodeStatus,
				"message": fmt.Sprintf("节点 %s 状态获取成功", nodeID),
			})
		} else {
			http.Error(w, fmt.Sprintf("节点 %s 不存在", nodeID), http.StatusNotFound)
		}
		return
	}

	// 返回所有节点状态
	handler.sendJSONResponse(w, map[string]interface{}{
		"success":    true,
		"nodeStatus": snapshot.NodeStatus,
		"totalNodes": len(snapshot.NodeStatus),
		"message":    "节点状态获取成功",
	})
}

// handleTopologySubscribe 处理拓扑订阅请求（SSE）
func (handler *TopologyAPIHandler) handleTopologySubscribe(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	// 委托给变更通知器处理SSE订阅
	if handler.changeNotifier != nil {
		handler.changeNotifier.HandleSSESubscribe(w, r)
	} else {
		http.Error(w, "拓扑事件通知服务未启用", http.StatusServiceUnavailable)
	}
}

// handleTopologyEvents 处理拓扑事件历史查询
func (handler *TopologyAPIHandler) handleTopologyEvents(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	// 解析查询参数
	sinceVersionParam := r.URL.Query().Get("sinceVersion")
	limitParam := r.URL.Query().Get("limit")

	var sinceVersion int64 = 0
	var limit int = 100 // 默认限制

	if sinceVersionParam != "" {
		if v, err := strconv.ParseInt(sinceVersionParam, 10, 64); err == nil {
			sinceVersion = v
		}
	}

	if limitParam != "" {
		if l, err := strconv.Atoi(limitParam); err == nil && l > 0 && l <= 1000 {
			limit = l
		}
	}

	// 获取事件历史
	if handler.changeNotifier != nil {
		events, err := handler.changeNotifier.GetEventHistory(sinceVersion, limit)
		if err != nil {
			http.Error(w, fmt.Sprintf("获取事件历史失败: %v", err), http.StatusInternalServerError)
			return
		}

		handler.sendJSONResponse(w, map[string]interface{}{
			"success":      true,
			"events":       events,
			"sinceVersion": sinceVersion,
			"count":        len(events),
			"message":      "事件历史获取成功",
		})
	} else {
		http.Error(w, "拓扑事件通知服务未启用", http.StatusServiceUnavailable)
	}
}

// handleHealthCheck 处理拓扑服务健康检查
func (handler *TopologyAPIHandler) handleHealthCheck(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	// 检查拓扑服务状态
	snapshot, err := handler.topologyService.GetTopologySnapshot()
	if err != nil {
		handler.sendJSONResponse(w, map[string]interface{}{
			"success": false,
			"health":  "unhealthy",
			"error":   err.Error(),
			"message": "拓扑服务不健康",
		})
		return
	}

	// 统计健康状态
	healthStats := handler.calculateHealthStats(snapshot)

	handler.sendJSONResponse(w, map[string]interface{}{
		"success":     true,
		"health":      "healthy",
		"version":     snapshot.Version,
		"timestamp":   snapshot.Timestamp,
		"healthStats": healthStats,
		"message":     "拓扑服务健康",
	})
}

// handleTopologyVersion 处理拓扑版本查询
func (handler *TopologyAPIHandler) handleTopologyVersion(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	snapshot, err := handler.topologyService.GetTopologySnapshot()
	if err != nil {
		http.Error(w, fmt.Sprintf("获取拓扑版本失败: %v", err), http.StatusInternalServerError)
		return
	}

	handler.sendJSONResponse(w, map[string]interface{}{
		"success":    true,
		"version":    snapshot.Version,
		"timestamp":  snapshot.Timestamp,
		"configHash": snapshot.ConfigHash,
		"message":    "拓扑版本获取成功",
	})
}

// handleTopologyStats 处理拓扑统计信息查询
func (handler *TopologyAPIHandler) handleTopologyStats(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	snapshot, err := handler.topologyService.GetTopologySnapshot()
	if err != nil {
		http.Error(w, fmt.Sprintf("获取拓扑统计失败: %v", err), http.StatusInternalServerError)
		return
	}

	// 计算统计信息
	stats := handler.calculateTopologyStats(snapshot)

	// 添加订阅者信息（如果可用）
	if handler.changeNotifier != nil {
		stats["activeSubscriptions"] = handler.changeNotifier.GetActiveSubscriptions()
		stats["subscriptionInfo"] = handler.changeNotifier.GetSubscriptionInfo()
	}

	handler.sendJSONResponse(w, map[string]interface{}{
		"success":   true,
		"stats":     stats,
		"timestamp": time.Now(),
		"message":   "拓扑统计信息获取成功",
	})
}

// 辅助方法

// sendJSONResponse 发送JSON响应
func (handler *TopologyAPIHandler) sendJSONResponse(w http.ResponseWriter, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

	if err := json.NewEncoder(w).Encode(data); err != nil {
		http.Error(w, "编码响应失败", http.StatusInternalServerError)
		return
	}
}

// calculateHealthStats 计算健康统计信息
func (handler *TopologyAPIHandler) calculateHealthStats(snapshot *TopologySnapshot) map[string]interface{} {
	stats := map[string]interface{}{
		"totalNodes":     len(snapshot.NodeStatus),
		"healthyNodes":   0,
		"unhealthyNodes": 0,
		"unknownNodes":   0,
		"totalShards":    len(snapshot.ShardMap),
		"activeShards":   0,
		"inactiveShards": 0,
	}

	// 统计节点健康状态
	for _, nodeStatus := range snapshot.NodeStatus {
		switch nodeStatus.Health {
		case HealthStatusHealthy:
			stats["healthyNodes"] = stats["healthyNodes"].(int) + 1
		case HealthStatusUnhealthy:
			stats["unhealthyNodes"] = stats["unhealthyNodes"].(int) + 1
		default:
			stats["unknownNodes"] = stats["unknownNodes"].(int) + 1
		}
	}

	// 统计分片状态
	for _, shardInfo := range snapshot.ShardMap {
		switch shardInfo.Status {
		case ShardStatusActive:
			stats["activeShards"] = stats["activeShards"].(int) + 1
		default:
			stats["inactiveShards"] = stats["inactiveShards"].(int) + 1
		}
	}

	// 计算健康度百分比
	if totalNodes := stats["totalNodes"].(int); totalNodes > 0 {
		healthyNodes := stats["healthyNodes"].(int)
		stats["healthPercentage"] = float64(healthyNodes) / float64(totalNodes) * 100
	} else {
		stats["healthPercentage"] = 0.0
	}

	return stats
}

// calculateTopologyStats 计算拓扑统计信息
func (handler *TopologyAPIHandler) calculateTopologyStats(snapshot *TopologySnapshot) map[string]interface{} {
	stats := map[string]interface{}{
		"version":        snapshot.Version,
		"timestamp":      snapshot.Timestamp,
		"configHash":     snapshot.ConfigHash,
		"totalNodes":     len(snapshot.NodeStatus),
		"totalShards":    len(snapshot.ShardMap),
		"nodesByHealth":  make(map[string]int),
		"shardsByStatus": make(map[string]int),
		"loadInfo":       make(map[string]interface{}),
	}

	// 统计节点按健康状态分布
	nodesByHealth := make(map[string]int)
	var totalCPU, totalMemory, totalDisk float64
	var totalQPS int64

	for _, nodeStatus := range snapshot.NodeStatus {
		healthStr := nodeStatus.Health.String()
		nodesByHealth[healthStr]++

		// 累加负载信息
		totalCPU += nodeStatus.Load.CPUUsage
		totalMemory += nodeStatus.Load.MemoryUsage
		totalDisk += nodeStatus.Load.DiskUsage
		totalQPS += nodeStatus.Load.QPS
	}
	stats["nodesByHealth"] = nodesByHealth

	// 统计分片按状态分布
	shardsByStatus := make(map[string]int)
	for _, shardInfo := range snapshot.ShardMap {
		statusStr := shardInfo.Status.String()
		shardsByStatus[statusStr]++
	}
	stats["shardsByStatus"] = shardsByStatus

	// 计算平均负载
	if nodeCount := len(snapshot.NodeStatus); nodeCount > 0 {
		stats["loadInfo"] = map[string]interface{}{
			"avgCPUUsage":    totalCPU / float64(nodeCount),
			"avgMemoryUsage": totalMemory / float64(nodeCount),
			"avgDiskUsage":   totalDisk / float64(nodeCount),
			"totalQPS":       totalQPS,
			"avgQPS":         totalQPS / int64(nodeCount),
		}
	}

	return stats
}

// TopologyMiddleware 拓扑服务中间件
func (handler *TopologyAPIHandler) TopologyMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// 添加拓扑相关的请求头
		w.Header().Set("X-Topology-Service", "ConcordKV-Central-Topology")

		// 记录拓扑API访问日志
		if len(r.URL.Path) > 14 && r.URL.Path[:14] == "/api/topology/" {
			// 这里可以添加访问统计、限流等逻辑
		}

		next.ServeHTTP(w, r)
	})
}
