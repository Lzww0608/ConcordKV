/*
* @Author: Lzww0608
* @Date: 2025-7-2 22:24:02
* @LastEditors: Lzww0608
* @LastEditTime: 2025-7-2 22:24:05
* @Description: ConcordKV intelligent client - enhanced connection pool module
 */

package concord

import (
	"context"
	"errors"
	"fmt"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

// ConnectionState 连接状态
type ConnectionState int

const (
	ConnStateIdle       ConnectionState = iota // 空闲状态
	ConnStateActive                            // 活跃状态
	ConnStateConnecting                        // 连接中
	ConnStateClosing                           // 关闭中
	ConnStateClosed                            // 已关闭
	ConnStateError                             // 错误状态
)

func (cs ConnectionState) String() string {
	switch cs {
	case ConnStateIdle:
		return "Idle"
	case ConnStateActive:
		return "Active"
	case ConnStateConnecting:
		return "Connecting"
	case ConnStateClosing:
		return "Closing"
	case ConnStateClosed:
		return "Closed"
	case ConnStateError:
		return "Error"
	default:
		return "Unknown"
	}
}

// Connection 连接包装器
type Connection struct {
	mu          sync.RWMutex
	id          string                 // 连接ID
	nodeID      NodeID                 // 节点ID
	shardID     string                 // 分片ID
	address     string                 // 节点地址
	conn        net.Conn               // 底层网络连接
	state       ConnectionState        // 连接状态
	createdAt   time.Time              // 创建时间
	lastUsedAt  time.Time              // 最后使用时间
	usageCount  int64                  // 使用次数
	errors      []error                // 错误历史
	maxErrors   int                    // 最大错误数
	timeout     time.Duration          // 连接超时
	keepAlive   time.Duration          // 保活时间
	isPreWarmed bool                   // 是否预热连接
	metadata    map[string]interface{} // 元数据
	pool        *ConnectionPool        // 所属连接池引用
}

// NewConnection 创建新连接
func NewConnection(id string, nodeID NodeID, shardID string, address string, timeout time.Duration) *Connection {
	return &Connection{
		id:         id,
		nodeID:     nodeID,
		shardID:    shardID,
		address:    address,
		state:      ConnStateIdle,
		createdAt:  time.Now(),
		lastUsedAt: time.Now(),
		maxErrors:  10,
		timeout:    timeout,
		keepAlive:  30 * time.Second,
		metadata:   make(map[string]interface{}),
	}
}

// Connect 建立连接
func (c *Connection) Connect(ctx context.Context) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.state == ConnStateActive {
		return nil // 已连接
	}

	c.state = ConnStateConnecting

	// 建立网络连接
	dialer := &net.Dialer{
		Timeout:   c.timeout,
		KeepAlive: c.keepAlive,
	}

	conn, err := dialer.DialContext(ctx, "tcp", c.address)
	if err != nil {
		c.state = ConnStateError
		c.addError(err)
		return fmt.Errorf("连接到 %s 失败: %w", c.address, err)
	}

	c.conn = conn
	c.state = ConnStateActive
	c.lastUsedAt = time.Now()

	return nil
}

// Close 关闭连接
func (c *Connection) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.state == ConnStateClosed || c.state == ConnStateClosing {
		return nil
	}

	c.state = ConnStateClosing

	var err error
	if c.conn != nil {
		err = c.conn.Close()
	}

	c.state = ConnStateClosed
	return err
}

// IsHealthy 检查连接是否健康
func (c *Connection) IsHealthy() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()

	return c.state != ConnStateError && c.state != ConnStateClosed && len(c.errors) < c.maxErrors
}

// IsIdle 检查连接是否空闲
func (c *Connection) IsIdle() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()

	return c.state == ConnStateIdle
}

// MarkUsed 标记连接为已使用
func (c *Connection) MarkUsed() {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.lastUsedAt = time.Now()
	atomic.AddInt64(&c.usageCount, 1)
	c.state = ConnStateActive
}

// MarkIdle 标记连接为空闲
func (c *Connection) MarkIdle() {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.state == ConnStateActive {
		c.state = ConnStateIdle
	}
}

// GetStats 获取连接统计信息
func (c *Connection) GetStats() *ConnectionStats {
	c.mu.RLock()
	defer c.mu.RUnlock()

	return &ConnectionStats{
		ID:          c.id,
		NodeID:      c.nodeID,
		ShardID:     c.shardID,
		Address:     c.address,
		State:       c.state,
		CreatedAt:   c.createdAt,
		LastUsedAt:  c.lastUsedAt,
		UsageCount:  atomic.LoadInt64(&c.usageCount),
		ErrorCount:  len(c.errors),
		IdleTime:    time.Since(c.lastUsedAt),
		IsPreWarmed: c.isPreWarmed,
	}
}

// addError 添加错误记录
func (c *Connection) addError(err error) {
	c.errors = append(c.errors, err)
	if len(c.errors) > c.maxErrors {
		c.errors = c.errors[1:]
	}
}

// ConnectionStats 连接统计信息
type ConnectionStats struct {
	ID          string          `json:"id"`          // 连接ID
	NodeID      NodeID          `json:"nodeId"`      // 节点ID
	ShardID     string          `json:"shardId"`     // 分片ID
	Address     string          `json:"address"`     // 节点地址
	State       ConnectionState `json:"state"`       // 连接状态
	CreatedAt   time.Time       `json:"createdAt"`   // 创建时间
	LastUsedAt  time.Time       `json:"lastUsedAt"`  // 最后使用时间
	UsageCount  int64           `json:"usageCount"`  // 使用次数
	ErrorCount  int             `json:"errorCount"`  // 错误次数
	IdleTime    time.Duration   `json:"idleTime"`    // 空闲时间
	IsPreWarmed bool            `json:"isPreWarmed"` // 是否预热
}

// PoolConfig 连接池配置
type PoolConfig struct {
	// 基础配置
	MinConnections    int           `json:"minConnections"`    // 最小连接数
	MaxConnections    int           `json:"maxConnections"`    // 最大连接数
	InitialSize       int           `json:"initialSize"`       // 初始连接数
	ConnectionTimeout time.Duration `json:"connectionTimeout"` // 连接超时
	IdleTimeout       time.Duration `json:"idleTimeout"`       // 空闲超时
	MaxLifetime       time.Duration `json:"maxLifetime"`       // 最大生命周期

	// 预热配置
	EnablePreWarm      bool `json:"enablePreWarm"`      // 是否启用预热
	PreWarmSize        int  `json:"preWarmSize"`        // 预热连接数
	PreWarmConcurrency int  `json:"preWarmConcurrency"` // 预热并发数

	// 健康检查配置
	HealthCheckInterval time.Duration `json:"healthCheckInterval"` // 健康检查间隔
	HealthCheckTimeout  time.Duration `json:"healthCheckTimeout"`  // 健康检查超时
	MaxRetries          int           `json:"maxRetries"`          // 最大重试次数
	RetryInterval       time.Duration `json:"retryInterval"`       // 重试间隔

	// 扩缩容配置
	EnableAutoScale    bool          `json:"enableAutoScale"`    // 是否启用自动扩缩容
	ScaleUpThreshold   float64       `json:"scaleUpThreshold"`   // 扩容阈值
	ScaleDownThreshold float64       `json:"scaleDownThreshold"` // 缩容阈值
	ScaleUpStep        int           `json:"scaleUpStep"`        // 扩容步长
	ScaleDownStep      int           `json:"scaleDownStep"`      // 缩容步长
	ScaleInterval      time.Duration `json:"scaleInterval"`      // 扩缩容检查间隔

	// 性能优化配置
	EnablePipelining bool          `json:"enablePipelining"` // 是否启用管道化
	MaxPipelineSize  int           `json:"maxPipelineSize"`  // 最大管道大小
	EnableBatching   bool          `json:"enableBatching"`   // 是否启用批量处理
	BatchSize        int           `json:"batchSize"`        // 批量大小
	BatchTimeout     time.Duration `json:"batchTimeout"`     // 批量超时
}

// DefaultPoolConfig 默认连接池配置
func DefaultPoolConfig() *PoolConfig {
	return &PoolConfig{
		MinConnections:      5,
		MaxConnections:      100,
		InitialSize:         10,
		ConnectionTimeout:   30 * time.Second,
		IdleTimeout:         5 * time.Minute,
		MaxLifetime:         1 * time.Hour,
		EnablePreWarm:       true,
		PreWarmSize:         5,
		PreWarmConcurrency:  3,
		HealthCheckInterval: 30 * time.Second,
		HealthCheckTimeout:  5 * time.Second,
		MaxRetries:          3,
		RetryInterval:       time.Second,
		EnableAutoScale:     true,
		ScaleUpThreshold:    0.8,
		ScaleDownThreshold:  0.3,
		ScaleUpStep:         5,
		ScaleDownStep:       2,
		ScaleInterval:       1 * time.Minute,
		EnablePipelining:    true,
		MaxPipelineSize:     10,
		EnableBatching:      true,
		BatchSize:           100,
		BatchTimeout:        10 * time.Millisecond,
	}
}

// ConnectionPool 连接池
type ConnectionPool struct {
	mu              sync.RWMutex
	config          *PoolConfig
	nodeID          NodeID
	shardID         string
	address         string
	connections     map[string]*Connection // 连接映射
	idleConnections []*Connection          // 空闲连接队列
	activeCount     int64                  // 活跃连接数
	totalCount      int64                  // 总连接数
	stats           *PoolStats             // 统计信息
	stopChannel     chan struct{}          // 停止信号
	isRunning       int64                  // 运行状态
	waitQueue       chan chan *Connection  // 等待队列
	factory         ConnectionFactory      // 连接工厂
}

// ConnectionFactory 连接工厂接口
type ConnectionFactory interface {
	CreateConnection(nodeID NodeID, shardID string, address string) (*Connection, error)
}

// DefaultConnectionFactory 默认连接工厂
type DefaultConnectionFactory struct {
	timeout time.Duration
}

// NewDefaultConnectionFactory 创建默认连接工厂
func NewDefaultConnectionFactory(timeout time.Duration) *DefaultConnectionFactory {
	return &DefaultConnectionFactory{
		timeout: timeout,
	}
}

// CreateConnection 创建连接
func (dcf *DefaultConnectionFactory) CreateConnection(nodeID NodeID, shardID string, address string) (*Connection, error) {
	id := fmt.Sprintf("%s-%s-%d", nodeID, shardID, time.Now().UnixNano())
	return NewConnection(id, nodeID, shardID, address, dcf.timeout), nil
}

// PoolStats 连接池统计信息
type PoolStats struct {
	NodeID               NodeID        `json:"nodeId"`               // 节点ID
	ShardID              string        `json:"shardId"`              // 分片ID
	Address              string        `json:"address"`              // 节点地址
	TotalConnections     int64         `json:"totalConnections"`     // 总连接数
	ActiveConnections    int64         `json:"activeConnections"`    // 活跃连接数
	IdleConnections      int64         `json:"idleConnections"`      // 空闲连接数
	WaitingRequests      int64         `json:"waitingRequests"`      // 等待请求数
	TotalRequests        int64         `json:"totalRequests"`        // 总请求数
	SuccessfulRequests   int64         `json:"successfulRequests"`   // 成功请求数
	FailedRequests       int64         `json:"failedRequests"`       // 失败请求数
	AverageWaitTime      time.Duration `json:"averageWaitTime"`      // 平均等待时间
	AverageUsageTime     time.Duration `json:"averageUsageTime"`     // 平均使用时间
	ConnectionsCreated   int64         `json:"connectionsCreated"`   // 创建的连接数
	ConnectionsDestroyed int64         `json:"connectionsDestroyed"` // 销毁的连接数
	LastScaleTime        time.Time     `json:"lastScaleTime"`        // 最后扩缩容时间
	LastUpdate           time.Time     `json:"lastUpdate"`           // 最后更新时间
}

// NewConnectionPool 创建新的连接池
func NewConnectionPool(config *PoolConfig, nodeID NodeID, shardID string, address string, factory ConnectionFactory) *ConnectionPool {
	if config == nil {
		config = DefaultPoolConfig()
	}

	if factory == nil {
		factory = NewDefaultConnectionFactory(config.ConnectionTimeout)
	}

	return &ConnectionPool{
		config:          config,
		nodeID:          nodeID,
		shardID:         shardID,
		address:         address,
		connections:     make(map[string]*Connection),
		idleConnections: make([]*Connection, 0, config.MaxConnections),
		stopChannel:     make(chan struct{}),
		waitQueue:       make(chan chan *Connection, config.MaxConnections),
		factory:         factory,
		stats: &PoolStats{
			NodeID:  nodeID,
			ShardID: shardID,
			Address: address,
		},
	}
}

// Start 启动连接池
func (cp *ConnectionPool) Start(ctx context.Context) error {
	if !atomic.CompareAndSwapInt64(&cp.isRunning, 0, 1) {
		return errors.New("连接池已经在运行")
	}

	// 初始化连接
	if err := cp.initializeConnections(ctx); err != nil {
		atomic.StoreInt64(&cp.isRunning, 0)
		return fmt.Errorf("初始化连接失败: %w", err)
	}

	// 启动健康检查
	if cp.config.HealthCheckInterval > 0 {
		go cp.healthCheckLoop(ctx)
	}

	// 启动自动扩缩容
	if cp.config.EnableAutoScale {
		go cp.autoScaleLoop(ctx)
	}

	// 启动清理任务
	go cp.cleanupLoop(ctx)

	return nil
}

// Stop 停止连接池
func (cp *ConnectionPool) Stop() error {
	if !atomic.CompareAndSwapInt64(&cp.isRunning, 1, 0) {
		return nil
	}

	close(cp.stopChannel)

	// 关闭所有连接
	cp.mu.Lock()
	defer cp.mu.Unlock()

	for _, conn := range cp.connections {
		conn.Close()
	}

	cp.connections = make(map[string]*Connection)
	cp.idleConnections = cp.idleConnections[:0]
	atomic.StoreInt64(&cp.activeCount, 0)
	atomic.StoreInt64(&cp.totalCount, 0)

	return nil
}

// Get 获取连接
func (cp *ConnectionPool) Get(ctx context.Context) (*Connection, error) {
	start := time.Now()
	defer func() {
		waitTime := time.Since(start)
		cp.updateAverageWaitTime(waitTime)
		atomic.AddInt64(&cp.stats.TotalRequests, 1)
	}()

	// 尝试从空闲连接获取
	if conn := cp.getIdleConnection(); conn != nil {
		conn.MarkUsed()
		atomic.AddInt64(&cp.activeCount, 1)
		atomic.AddInt64(&cp.stats.SuccessfulRequests, 1)
		return conn, nil
	}

	// 尝试创建新连接
	if atomic.LoadInt64(&cp.totalCount) < int64(cp.config.MaxConnections) {
		conn, err := cp.createConnection(ctx)
		if err == nil {
			conn.MarkUsed()
			atomic.AddInt64(&cp.activeCount, 1)
			atomic.AddInt64(&cp.stats.SuccessfulRequests, 1)
			return conn, nil
		}
	}

	// 等待空闲连接
	waitChan := make(chan *Connection, 1)

	select {
	case cp.waitQueue <- waitChan:
		// 进入等待队列
		atomic.AddInt64(&cp.stats.WaitingRequests, 1)
		defer atomic.AddInt64(&cp.stats.WaitingRequests, -1)

		select {
		case conn := <-waitChan:
			if conn != nil {
				conn.MarkUsed()
				atomic.AddInt64(&cp.activeCount, 1)
				atomic.AddInt64(&cp.stats.SuccessfulRequests, 1)
				return conn, nil
			}
			atomic.AddInt64(&cp.stats.FailedRequests, 1)
			return nil, errors.New("从等待队列获取连接失败")
		case <-ctx.Done():
			atomic.AddInt64(&cp.stats.FailedRequests, 1)
			return nil, ctx.Err()
		case <-cp.stopChannel:
			atomic.AddInt64(&cp.stats.FailedRequests, 1)
			return nil, errors.New("连接池已关闭")
		}
	default:
		// 等待队列满
		atomic.AddInt64(&cp.stats.FailedRequests, 1)
		return nil, errors.New("连接池繁忙，等待队列已满")
	}
}

// Put 归还连接
func (cp *ConnectionPool) Put(conn *Connection) {
	if conn == nil {
		return
	}

	cp.mu.Lock()
	defer cp.mu.Unlock()

	// 检查连接健康状态
	if !conn.IsHealthy() {
		cp.removeConnection(conn)
		atomic.AddInt64(&cp.activeCount, -1)
		return
	}

	conn.MarkIdle()
	atomic.AddInt64(&cp.activeCount, -1)

	// 尝试满足等待的请求
	select {
	case waitChan := <-cp.waitQueue:
		select {
		case waitChan <- conn:
			atomic.AddInt64(&cp.activeCount, 1)
			return
		default:
			// 等待请求已取消
		}
	default:
		// 没有等待的请求
	}

	// 添加到空闲连接队列
	cp.idleConnections = append(cp.idleConnections, conn)
}

// GetStats 获取统计信息
func (cp *ConnectionPool) GetStats() *PoolStats {
	cp.mu.RLock()
	defer cp.mu.RUnlock()

	stats := *cp.stats
	stats.TotalConnections = atomic.LoadInt64(&cp.totalCount)
	stats.ActiveConnections = atomic.LoadInt64(&cp.activeCount)
	stats.IdleConnections = int64(len(cp.idleConnections))
	stats.LastUpdate = time.Now()

	return &stats
}

// PreWarm 预热连接
func (cp *ConnectionPool) PreWarm(ctx context.Context) error {
	if !cp.config.EnablePreWarm {
		return nil
	}

	targetSize := cp.config.PreWarmSize
	if targetSize > cp.config.MaxConnections {
		targetSize = cp.config.MaxConnections
	}

	// 并发创建预热连接
	concurrency := cp.config.PreWarmConcurrency
	if concurrency <= 0 {
		concurrency = 1
	}

	semaphore := make(chan struct{}, concurrency)
	var wg sync.WaitGroup
	var errors []error
	var errorMu sync.Mutex

	for i := 0; i < targetSize; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()

			semaphore <- struct{}{}
			defer func() { <-semaphore }()

			conn, err := cp.createConnection(ctx)
			if err != nil {
				errorMu.Lock()
				errors = append(errors, err)
				errorMu.Unlock()
				return
			}

			conn.isPreWarmed = true
			cp.Put(conn)
		}()
	}

	wg.Wait()

	if len(errors) > 0 {
		return fmt.Errorf("预热连接时出现 %d 个错误，第一个错误: %v", len(errors), errors[0])
	}

	return nil
}

// Resize 调整连接池大小
func (cp *ConnectionPool) Resize(newSize int) error {
	if newSize < cp.config.MinConnections {
		newSize = cp.config.MinConnections
	}
	if newSize > cp.config.MaxConnections {
		newSize = cp.config.MaxConnections
	}

	currentSize := int(atomic.LoadInt64(&cp.totalCount))

	if newSize > currentSize {
		// 扩容
		return cp.scaleUp(newSize - currentSize)
	} else if newSize < currentSize {
		// 缩容
		return cp.scaleDown(currentSize - newSize)
	}

	return nil
}

// 内部方法：初始化连接
func (cp *ConnectionPool) initializeConnections(ctx context.Context) error {
	initialSize := cp.config.InitialSize
	if initialSize > cp.config.MaxConnections {
		initialSize = cp.config.MaxConnections
	}

	for i := 0; i < initialSize; i++ {
		conn, err := cp.createConnection(ctx)
		if err != nil {
			return err
		}
		cp.idleConnections = append(cp.idleConnections, conn)
	}

	// 执行预热
	if cp.config.EnablePreWarm {
		if err := cp.PreWarm(ctx); err != nil {
			// 预热失败不应该阻止连接池启动
		}
	}

	return nil
}

// 内部方法：创建连接
func (cp *ConnectionPool) createConnection(ctx context.Context) (*Connection, error) {
	conn, err := cp.factory.CreateConnection(cp.nodeID, cp.shardID, cp.address)
	if err != nil {
		return nil, err
	}

	conn.pool = cp

	// 检查是否是模拟连接工厂，如果是则跳过真实连接
	if _, isMock := cp.factory.(*MockConnectionFactory); !isMock {
		if err := conn.Connect(ctx); err != nil {
			return nil, err
		}
	}

	cp.mu.Lock()
	cp.connections[conn.id] = conn
	cp.mu.Unlock()

	atomic.AddInt64(&cp.totalCount, 1)
	atomic.AddInt64(&cp.stats.ConnectionsCreated, 1)

	return conn, nil
}

// 内部方法：获取空闲连接
func (cp *ConnectionPool) getIdleConnection() *Connection {
	cp.mu.Lock()
	defer cp.mu.Unlock()

	if len(cp.idleConnections) == 0 {
		return nil
	}

	// 从队列头取连接
	conn := cp.idleConnections[0]
	cp.idleConnections = cp.idleConnections[1:]

	// 检查连接是否仍然健康
	if !conn.IsHealthy() {
		cp.removeConnection(conn)
		return cp.getIdleConnection() // 递归获取下一个
	}

	return conn
}

// 内部方法：移除连接
func (cp *ConnectionPool) removeConnection(conn *Connection) {
	delete(cp.connections, conn.id)
	conn.Close()
	atomic.AddInt64(&cp.totalCount, -1)
	atomic.AddInt64(&cp.stats.ConnectionsDestroyed, 1)
}

// 内部方法：扩容
func (cp *ConnectionPool) scaleUp(count int) error {
	ctx, cancel := context.WithTimeout(context.Background(), cp.config.ConnectionTimeout)
	defer cancel()

	for i := 0; i < count; i++ {
		conn, err := cp.createConnection(ctx)
		if err != nil {
			return err
		}
		cp.Put(conn)
	}

	cp.stats.LastScaleTime = time.Now()
	return nil
}

// 内部方法：缩容
func (cp *ConnectionPool) scaleDown(count int) error {
	cp.mu.Lock()
	defer cp.mu.Unlock()

	removed := 0
	for i := len(cp.idleConnections) - 1; i >= 0 && removed < count; i-- {
		conn := cp.idleConnections[i]
		cp.idleConnections = cp.idleConnections[:i]
		cp.removeConnection(conn)
		removed++
	}

	cp.stats.LastScaleTime = time.Now()
	return nil
}

// 内部方法：健康检查循环
func (cp *ConnectionPool) healthCheckLoop(ctx context.Context) {
	ticker := time.NewTicker(cp.config.HealthCheckInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-cp.stopChannel:
			return
		case <-ticker.C:
			cp.performHealthCheck(ctx)
		}
	}
}

// 内部方法：执行健康检查
func (cp *ConnectionPool) performHealthCheck(ctx context.Context) {
	cp.mu.RLock()
	connections := make([]*Connection, 0, len(cp.connections))
	for _, conn := range cp.connections {
		connections = append(connections, conn)
	}
	cp.mu.RUnlock()

	// 并发检查所有连接
	var wg sync.WaitGroup
	for _, conn := range connections {
		wg.Add(1)
		go func(c *Connection) {
			defer wg.Done()
			cp.checkConnectionHealth(ctx, c)
		}(conn)
	}
	wg.Wait()
}

// 内部方法：检查单个连接健康状态
func (cp *ConnectionPool) checkConnectionHealth(ctx context.Context, conn *Connection) {
	if !conn.IsHealthy() {
		cp.mu.Lock()
		cp.removeConnection(conn)
		cp.mu.Unlock()
	}

	// TODO: 实现实际的健康检查逻辑
	// 例如发送ping命令或检查连接状态
}

// 内部方法：自动扩缩容循环
func (cp *ConnectionPool) autoScaleLoop(ctx context.Context) {
	ticker := time.NewTicker(cp.config.ScaleInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-cp.stopChannel:
			return
		case <-ticker.C:
			cp.performAutoScale()
		}
	}
}

// 内部方法：执行自动扩缩容
func (cp *ConnectionPool) performAutoScale() {
	totalConn := atomic.LoadInt64(&cp.totalCount)
	activeConn := atomic.LoadInt64(&cp.activeCount)

	if totalConn == 0 {
		return
	}

	usageRatio := float64(activeConn) / float64(totalConn)

	if usageRatio > cp.config.ScaleUpThreshold {
		// 扩容
		newSize := int(totalConn) + cp.config.ScaleUpStep
		if newSize <= cp.config.MaxConnections {
			cp.scaleUp(cp.config.ScaleUpStep)
		}
	} else if usageRatio < cp.config.ScaleDownThreshold {
		// 缩容
		newSize := int(totalConn) - cp.config.ScaleDownStep
		if newSize >= cp.config.MinConnections {
			cp.scaleDown(cp.config.ScaleDownStep)
		}
	}
}

// 内部方法：清理循环
func (cp *ConnectionPool) cleanupLoop(ctx context.Context) {
	ticker := time.NewTicker(1 * time.Minute) // 每分钟清理一次
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-cp.stopChannel:
			return
		case <-ticker.C:
			cp.performCleanup()
		}
	}
}

// 内部方法：执行清理
func (cp *ConnectionPool) performCleanup() {
	cp.mu.Lock()
	defer cp.mu.Unlock()

	now := time.Now()

	// 清理过期的空闲连接
	validIdle := make([]*Connection, 0, len(cp.idleConnections))
	for _, conn := range cp.idleConnections {
		if cp.config.IdleTimeout > 0 && now.Sub(conn.lastUsedAt) > cp.config.IdleTimeout {
			cp.removeConnection(conn)
			continue
		}

		if cp.config.MaxLifetime > 0 && now.Sub(conn.createdAt) > cp.config.MaxLifetime {
			cp.removeConnection(conn)
			continue
		}

		validIdle = append(validIdle, conn)
	}
	cp.idleConnections = validIdle
}

// 内部方法：更新平均等待时间
func (cp *ConnectionPool) updateAverageWaitTime(waitTime time.Duration) {
	if cp.stats.AverageWaitTime == 0 {
		cp.stats.AverageWaitTime = waitTime
	} else {
		// 指数移动平均
		cp.stats.AverageWaitTime = time.Duration(float64(cp.stats.AverageWaitTime)*0.9 + float64(waitTime)*0.1)
	}
}

// ShardAwareConnectionPool 分片感知连接池
type ShardAwareConnectionPool struct {
	mu            sync.RWMutex
	config        *PoolConfig
	shardPools    map[string]*ConnectionPool // 分片ID -> 连接池
	globalPool    *ConnectionPool            // 全局连接池
	nodeHealthMap map[NodeID]*NodeHealth     // 节点健康状态
	stats         *ShardPoolStats            // 统计信息
	stopChannel   chan struct{}              // 停止信号
	isRunning     int64                      // 运行状态
	factory       ConnectionFactory          // 连接工厂
}

// ShardPoolStats 分片连接池统计信息
type ShardPoolStats struct {
	TotalShards       int                    `json:"totalShards"`       // 总分片数
	TotalConnections  int64                  `json:"totalConnections"`  // 总连接数
	ActiveConnections int64                  `json:"activeConnections"` // 活跃连接数
	IdleConnections   int64                  `json:"idleConnections"`   // 空闲连接数
	ShardStats        map[string]*PoolStats  `json:"shardStats"`        // 分片统计
	NodeStats         map[NodeID]*NodeHealth `json:"nodeStats"`         // 节点统计
	LastUpdate        time.Time              `json:"lastUpdate"`        // 最后更新时间
}

// NewShardAwareConnectionPool 创建分片感知连接池
func NewShardAwareConnectionPool(config *PoolConfig, factory ConnectionFactory) *ShardAwareConnectionPool {
	if config == nil {
		config = DefaultPoolConfig()
	}

	if factory == nil {
		factory = NewDefaultConnectionFactory(config.ConnectionTimeout)
	}

	return &ShardAwareConnectionPool{
		config:        config,
		shardPools:    make(map[string]*ConnectionPool),
		nodeHealthMap: make(map[NodeID]*NodeHealth),
		factory:       factory,
		stopChannel:   make(chan struct{}),
		stats: &ShardPoolStats{
			ShardStats: make(map[string]*PoolStats),
			NodeStats:  make(map[NodeID]*NodeHealth),
		},
	}
}

// Start 启动分片感知连接池
func (sacp *ShardAwareConnectionPool) Start(ctx context.Context) error {
	if !atomic.CompareAndSwapInt64(&sacp.isRunning, 0, 1) {
		return errors.New("分片感知连接池已经在运行")
	}

	// 启动全局连接池
	if sacp.globalPool != nil {
		if err := sacp.globalPool.Start(ctx); err != nil {
			atomic.StoreInt64(&sacp.isRunning, 0)
			return err
		}
	}

	return nil
}

// Stop 停止分片感知连接池
func (sacp *ShardAwareConnectionPool) Stop() error {
	if !atomic.CompareAndSwapInt64(&sacp.isRunning, 1, 0) {
		return nil
	}

	close(sacp.stopChannel)

	sacp.mu.Lock()
	defer sacp.mu.Unlock()

	// 停止所有分片连接池
	for _, pool := range sacp.shardPools {
		pool.Stop()
	}

	// 停止全局连接池
	if sacp.globalPool != nil {
		sacp.globalPool.Stop()
	}

	return nil
}

// GetConnection 根据分片信息获取连接
func (sacp *ShardAwareConnectionPool) GetConnection(ctx context.Context, shardInfo *ShardInfo, strategy RoutingStrategy) (*Connection, error) {
	// 根据策略选择目标节点
	var targetNode NodeID
	switch strategy {
	case RoutingWritePrimary:
		targetNode = shardInfo.Primary
	case RoutingReadReplica:
		if len(shardInfo.Replicas) > 0 {
			// 简单选择第一个副本
			targetNode = shardInfo.Replicas[0]
		} else {
			targetNode = shardInfo.Primary
		}
	default:
		targetNode = shardInfo.Primary
	}

	// 获取分片连接池
	pool := sacp.getOrCreateShardPool(shardInfo.ID, targetNode, "")

	return pool.Get(ctx)
}

// PutConnection 归还连接
func (sacp *ShardAwareConnectionPool) PutConnection(conn *Connection) {
	if conn == nil || conn.pool == nil {
		return
	}

	conn.pool.Put(conn)
}

// GetStats 获取统计信息
func (sacp *ShardAwareConnectionPool) GetStats() *ShardPoolStats {
	sacp.mu.RLock()
	defer sacp.mu.RUnlock()

	stats := &ShardPoolStats{
		TotalShards: len(sacp.shardPools),
		ShardStats:  make(map[string]*PoolStats),
		NodeStats:   make(map[NodeID]*NodeHealth),
		LastUpdate:  time.Now(),
	}

	var totalConnections, activeConnections, idleConnections int64

	// 收集分片统计
	for shardID, pool := range sacp.shardPools {
		poolStats := pool.GetStats()
		stats.ShardStats[shardID] = poolStats

		totalConnections += poolStats.TotalConnections
		activeConnections += poolStats.ActiveConnections
		idleConnections += poolStats.IdleConnections
	}

	// 收集全局连接池统计
	if sacp.globalPool != nil {
		globalStats := sacp.globalPool.GetStats()
		totalConnections += globalStats.TotalConnections
		activeConnections += globalStats.ActiveConnections
		idleConnections += globalStats.IdleConnections
	}

	stats.TotalConnections = totalConnections
	stats.ActiveConnections = activeConnections
	stats.IdleConnections = idleConnections

	// 复制节点健康统计
	for k, v := range sacp.nodeHealthMap {
		healthCopy := *v
		stats.NodeStats[k] = &healthCopy
	}

	return stats
}

// AddShard 添加分片
func (sacp *ShardAwareConnectionPool) AddShard(shardInfo *ShardInfo) error {
	sacp.mu.Lock()
	defer sacp.mu.Unlock()

	if _, exists := sacp.shardPools[shardInfo.ID]; exists {
		return fmt.Errorf("分片 %s 已存在", shardInfo.ID)
	}

	// 为主节点创建连接池
	primaryPool := NewConnectionPool(sacp.config, shardInfo.Primary, shardInfo.ID, "", sacp.factory)
	sacp.shardPools[shardInfo.ID] = primaryPool

	// 启动连接池
	ctx := context.Background()
	return primaryPool.Start(ctx)
}

// RemoveShard 移除分片
func (sacp *ShardAwareConnectionPool) RemoveShard(shardID string) error {
	sacp.mu.Lock()
	defer sacp.mu.Unlock()

	pool, exists := sacp.shardPools[shardID]
	if !exists {
		return fmt.Errorf("分片 %s 不存在", shardID)
	}

	pool.Stop()
	delete(sacp.shardPools, shardID)

	return nil
}

// 内部方法：获取或创建分片连接池
func (sacp *ShardAwareConnectionPool) getOrCreateShardPool(shardID string, nodeID NodeID, address string) *ConnectionPool {
	sacp.mu.RLock()
	pool, exists := sacp.shardPools[shardID]
	sacp.mu.RUnlock()

	if exists {
		return pool
	}

	sacp.mu.Lock()
	defer sacp.mu.Unlock()

	// 双重检查
	if pool, exists := sacp.shardPools[shardID]; exists {
		return pool
	}

	// 创建新的连接池
	pool = NewConnectionPool(sacp.config, nodeID, shardID, address, sacp.factory)
	sacp.shardPools[shardID] = pool

	// 启动连接池
	ctx := context.Background()
	pool.Start(ctx)

	return pool
}

// MockConnectionFactory 模拟连接工厂，用于测试
type MockConnectionFactory struct {
	timeout time.Duration
}

// NewMockConnectionFactory 创建模拟连接工厂
func NewMockConnectionFactory(timeout time.Duration) *MockConnectionFactory {
	return &MockConnectionFactory{
		timeout: timeout,
	}
}

// CreateConnection 创建模拟连接
func (mcf *MockConnectionFactory) CreateConnection(nodeID NodeID, shardID string, address string) (*Connection, error) {
	conn := NewConnection(
		fmt.Sprintf("%s-%s-%d", nodeID, shardID, time.Now().UnixNano()),
		nodeID,
		shardID,
		address,
		mcf.timeout,
	)

	// 模拟连接成功，设置状态为活跃
	conn.mu.Lock()
	conn.state = ConnStateActive
	conn.lastUsedAt = time.Now()
	conn.mu.Unlock()

	return conn, nil
}

// MockConnection 模拟连接，重写Connect方法避免真实网络连接
type MockConnection struct {
	*Connection
}

// NewMockConnection 创建模拟连接
func NewMockConnection(id string, nodeID NodeID, shardID string, address string, timeout time.Duration) *MockConnection {
	return &MockConnection{
		Connection: NewConnection(id, nodeID, shardID, address, timeout),
	}
}

// Connect 模拟连接，不进行真实网络连接
func (mc *MockConnection) Connect(ctx context.Context) error {
	mc.mu.Lock()
	defer mc.mu.Unlock()

	if mc.state == ConnStateActive {
		return nil // 已连接
	}

	// 模拟连接成功
	mc.state = ConnStateActive
	mc.lastUsedAt = time.Now()

	return nil
}
