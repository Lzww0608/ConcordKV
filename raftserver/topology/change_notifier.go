/*
* @Author: Lzww0608
* @Date: 2025-6-30 22:21:46
* @LastEditors: Lzww0608
* @LastEditTime: 2025-06-30 22:21:46
* @Description: ConcordKV中央拓扑服务 - 拓扑变更通知器
 */
package topology

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"
)

// ChangeNotifier 拓扑变更通知器
// 基于现有HTTP服务器实现Server-Sent Events (SSE)
type ChangeNotifier struct {
	// 复用现有HTTP框架
	httpServer *http.Server

	// 事件管理
	eventQueue   chan *TopologyEvent
	subscribers  map[string]*Subscription
	eventHistory []*TopologyEvent

	// 同步控制
	mu         sync.RWMutex
	shutdownCh chan struct{}
	logger     *log.Logger

	// 配置
	config *NotifierConfig
}

// Subscription SSE订阅连接
type Subscription struct {
	ID         string
	ClientAddr string
	Writer     http.ResponseWriter
	EventTypes []EventType
	CreatedAt  time.Time
	LastSent   time.Time
	Active     bool
}

// NotifierConfig 通知器配置
type NotifierConfig struct {
	EventBufferSize     int           `yaml:"eventBufferSize"`
	SubscriptionTimeout time.Duration `yaml:"subscriptionTimeout"`
	MaxEventHistory     int           `yaml:"maxEventHistory"`
	HeartbeatInterval   time.Duration `yaml:"heartbeatInterval"`
}

// NewChangeNotifier 创建新的变更通知器
func NewChangeNotifier(config *NotifierConfig) *ChangeNotifier {
	return &ChangeNotifier{
		eventQueue:   make(chan *TopologyEvent, config.EventBufferSize),
		subscribers:  make(map[string]*Subscription),
		eventHistory: make([]*TopologyEvent, 0, config.MaxEventHistory),
		shutdownCh:   make(chan struct{}),
		config:       config,
		logger:       log.New(log.Writer(), "[change-notifier] ", log.LstdFlags),
	}
}

// Start 启动变更通知器
func (cn *ChangeNotifier) Start() error {
	cn.mu.Lock()
	defer cn.mu.Unlock()

	cn.logger.Printf("启动拓扑变更通知器...")

	// 启动事件分发goroutine
	go cn.eventDistributor()

	// 启动心跳检查goroutine
	go cn.heartbeatLoop()

	cn.logger.Printf("拓扑变更通知器启动成功")
	return nil
}

// Stop 停止变更通知器
func (cn *ChangeNotifier) Stop() error {
	cn.mu.Lock()
	defer cn.mu.Unlock()

	cn.logger.Printf("停止拓扑变更通知器...")

	// 发送关闭信号
	close(cn.shutdownCh)

	// 关闭所有SSE连接
	for _, subscription := range cn.subscribers {
		subscription.Active = false
	}

	cn.logger.Printf("拓扑变更通知器已停止")
	return nil
}

// HandleSSESubscribe 处理SSE订阅请求
// 扩展现有HTTP API，支持Server-Sent Events
func (cn *ChangeNotifier) HandleSSESubscribe(w http.ResponseWriter, r *http.Request) {
	// 设置SSE响应头
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Headers", "Cache-Control")

	// 解析订阅参数
	eventTypesParam := r.URL.Query().Get("eventTypes")
	var eventTypes []EventType
	if eventTypesParam != "" {
		// 解析事件类型，这里简化处理
		eventTypes = []EventType{EventNodeHealthChange, EventShardMove}
	} else {
		// 默认订阅所有事件类型
		eventTypes = []EventType{
			EventNodeJoin, EventNodeLeave, EventNodeHealthChange,
			EventShardAdd, EventShardRemove, EventShardMove, EventConfigChange,
		}
	}

	// 创建订阅
	clientAddr := r.RemoteAddr
	subscription := &Subscription{
		ID:         fmt.Sprintf("%s-%d", clientAddr, time.Now().UnixNano()),
		ClientAddr: clientAddr,
		Writer:     w,
		EventTypes: eventTypes,
		CreatedAt:  time.Now(),
		LastSent:   time.Now(),
		Active:     true,
	}

	cn.mu.Lock()
	cn.subscribers[subscription.ID] = subscription
	cn.mu.Unlock()

	cn.logger.Printf("新增SSE订阅: %s (客户端: %s)", subscription.ID, clientAddr)

	// 发送连接成功消息
	cn.sendSSEEvent(subscription, &TopologyEvent{
		Type:      EventConfigChange,
		Timestamp: time.Now(),
		Metadata:  map[string]interface{}{"message": "SSE连接已建立"},
	})

	// 发送历史事件（如果客户端请求）
	sinceVersion := r.URL.Query().Get("sinceVersion")
	if sinceVersion != "" {
		cn.sendHistoryEvents(subscription, sinceVersion)
	}

	// 保持连接，直到客户端断开或服务停止
	cn.keepSSEConnection(subscription, r)
}

// PublishEvent 发布事件到通知队列
func (cn *ChangeNotifier) PublishEvent(event *TopologyEvent) {
	select {
	case cn.eventQueue <- event:
		cn.logger.Printf("发布拓扑事件: 类型=%s, 版本=%d", event.Type.String(), event.Version)
	default:
		cn.logger.Printf("事件队列已满，丢弃事件: %+v", event)
	}
}

// GetEventHistory 获取事件历史
func (cn *ChangeNotifier) GetEventHistory(sinceVersion int64, limit int) ([]*TopologyEvent, error) {
	cn.mu.RLock()
	defer cn.mu.RUnlock()

	events := make([]*TopologyEvent, 0)
	for _, event := range cn.eventHistory {
		if event.Version > sinceVersion {
			events = append(events, event)
			if len(events) >= limit {
				break
			}
		}
	}

	return events, nil
}

// 私有方法实现

// eventDistributor 事件分发器
func (cn *ChangeNotifier) eventDistributor() {
	for {
		select {
		case event := <-cn.eventQueue:
			cn.distributeEvent(event)
		case <-cn.shutdownCh:
			return
		}
	}
}

// distributeEvent 分发事件给所有订阅者
func (cn *ChangeNotifier) distributeEvent(event *TopologyEvent) {
	// 添加到历史记录
	cn.addToHistory(event)

	cn.mu.RLock()
	activeSubscriptions := make([]*Subscription, 0)
	for _, subscription := range cn.subscribers {
		if subscription.Active && cn.isEventTypeSubscribed(subscription, event.Type) {
			activeSubscriptions = append(activeSubscriptions, subscription)
		}
	}
	cn.mu.RUnlock()

	// 并发发送给所有订阅者
	var wg sync.WaitGroup
	for _, subscription := range activeSubscriptions {
		wg.Add(1)
		go func(sub *Subscription) {
			defer wg.Done()
			cn.sendSSEEvent(sub, event)
		}(subscription)
	}
	wg.Wait()

	cn.logger.Printf("事件分发完成: 类型=%s, 版本=%d, 发送给%d个订阅者",
		event.Type.String(), event.Version, len(activeSubscriptions))
}

// addToHistory 添加事件到历史记录
func (cn *ChangeNotifier) addToHistory(event *TopologyEvent) {
	cn.mu.Lock()
	defer cn.mu.Unlock()

	cn.eventHistory = append(cn.eventHistory, event)

	// 限制历史记录大小
	if len(cn.eventHistory) > cn.config.MaxEventHistory {
		cn.eventHistory = cn.eventHistory[1:]
	}
}

// sendSSEEvent 发送SSE事件
func (cn *ChangeNotifier) sendSSEEvent(subscription *Subscription, event *TopologyEvent) {
	if !subscription.Active {
		return
	}

	// 序列化事件数据
	eventData, err := json.Marshal(event)
	if err != nil {
		cn.logger.Printf("序列化事件失败: %v", err)
		return
	}

	// 构造SSE消息格式
	sseMessage := fmt.Sprintf("id: %d\nevent: %s\ndata: %s\n\n",
		event.Version, event.Type.String(), string(eventData))

	// 发送SSE消息
	_, err = fmt.Fprint(subscription.Writer, sseMessage)
	if err != nil {
		cn.logger.Printf("发送SSE消息失败 (订阅者: %s): %v", subscription.ID, err)
		cn.removeSubscription(subscription.ID)
		return
	}

	// 刷新响应缓冲区
	if flusher, ok := subscription.Writer.(http.Flusher); ok {
		flusher.Flush()
	}

	subscription.LastSent = time.Now()
}

// sendHistoryEvents 发送历史事件
func (cn *ChangeNotifier) sendHistoryEvents(subscription *Subscription, sinceVersion string) {
	// 这里简化处理，实际应该解析sinceVersion参数
	cn.mu.RLock()
	historyEvents := make([]*TopologyEvent, len(cn.eventHistory))
	copy(historyEvents, cn.eventHistory)
	cn.mu.RUnlock()

	for _, event := range historyEvents {
		if cn.isEventTypeSubscribed(subscription, event.Type) {
			cn.sendSSEEvent(subscription, event)
		}
	}

	cn.logger.Printf("发送历史事件给订阅者 %s: %d个事件", subscription.ID, len(historyEvents))
}

// isEventTypeSubscribed 检查订阅者是否关注事件类型
func (cn *ChangeNotifier) isEventTypeSubscribed(subscription *Subscription, eventType EventType) bool {
	for _, et := range subscription.EventTypes {
		if et == eventType {
			return true
		}
	}
	return false
}

// keepSSEConnection 保持SSE连接
func (cn *ChangeNotifier) keepSSEConnection(subscription *Subscription, r *http.Request) {
	// 设置连接超时
	timeout := time.NewTimer(cn.config.SubscriptionTimeout)
	defer timeout.Stop()

	// 监听连接状态
	for {
		select {
		case <-timeout.C:
			cn.logger.Printf("SSE连接超时，关闭订阅: %s", subscription.ID)
			cn.removeSubscription(subscription.ID)
			return
		case <-r.Context().Done():
			cn.logger.Printf("客户端断开连接，关闭订阅: %s", subscription.ID)
			cn.removeSubscription(subscription.ID)
			return
		case <-cn.shutdownCh:
			cn.logger.Printf("服务关闭，关闭订阅: %s", subscription.ID)
			cn.removeSubscription(subscription.ID)
			return
		default:
			// 检查连接是否还活跃
			if !subscription.Active {
				return
			}
			time.Sleep(time.Second)
		}
	}
}

// removeSubscription 移除订阅
func (cn *ChangeNotifier) removeSubscription(subscriptionID string) {
	cn.mu.Lock()
	defer cn.mu.Unlock()

	if subscription, exists := cn.subscribers[subscriptionID]; exists {
		subscription.Active = false
		delete(cn.subscribers, subscriptionID)
		cn.logger.Printf("移除SSE订阅: %s", subscriptionID)
	}
}

// heartbeatLoop 心跳检查循环
func (cn *ChangeNotifier) heartbeatLoop() {
	ticker := time.NewTicker(cn.config.HeartbeatInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			cn.sendHeartbeat()
		case <-cn.shutdownCh:
			return
		}
	}
}

// sendHeartbeat 发送心跳给所有订阅者
func (cn *ChangeNotifier) sendHeartbeat() {
	heartbeatEvent := &TopologyEvent{
		Type:      EventConfigChange,
		Timestamp: time.Now(),
		Version:   0, // 心跳事件不增加版本号
		Metadata:  map[string]interface{}{"heartbeat": true},
	}

	cn.mu.RLock()
	activeSubscriptions := make([]*Subscription, 0)
	for _, subscription := range cn.subscribers {
		if subscription.Active {
			activeSubscriptions = append(activeSubscriptions, subscription)
		}
	}
	cn.mu.RUnlock()

	for _, subscription := range activeSubscriptions {
		cn.sendSSEEvent(subscription, heartbeatEvent)
	}

	if len(activeSubscriptions) > 0 {
		cn.logger.Printf("发送心跳给 %d 个订阅者", len(activeSubscriptions))
	}
}

// GetActiveSubscriptions 获取活跃订阅数量
func (cn *ChangeNotifier) GetActiveSubscriptions() int {
	cn.mu.RLock()
	defer cn.mu.RUnlock()

	count := 0
	for _, subscription := range cn.subscribers {
		if subscription.Active {
			count++
		}
	}
	return count
}

// GetSubscriptionInfo 获取订阅信息
func (cn *ChangeNotifier) GetSubscriptionInfo() map[string]*Subscription {
	cn.mu.RLock()
	defer cn.mu.RUnlock()

	info := make(map[string]*Subscription)
	for id, subscription := range cn.subscribers {
		if subscription.Active {
			info[id] = &Subscription{
				ID:         subscription.ID,
				ClientAddr: subscription.ClientAddr,
				EventTypes: subscription.EventTypes,
				CreatedAt:  subscription.CreatedAt,
				LastSent:   subscription.LastSent,
				Active:     subscription.Active,
			}
		}
	}
	return info
}
