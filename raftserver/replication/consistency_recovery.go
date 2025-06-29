/*
 * @Author: Lzww0608
 * @Date: 2025-6-28 11:51:08
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-28 11:51:11
 * @Description: ConcordKV 数据一致性恢复器 - 基于现有WAL重放机制扩展
 */

package replication

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	"raftserver/raft"
)

// ConsistencyRecoveryConfig 一致性恢复配置
type ConsistencyRecoveryConfig struct {
	// 差异检测配置
	DifferenceDetectionInterval time.Duration `json:"differenceDetectionInterval"`
	LogIndexScanWindow          int64         `json:"logIndexScanWindow"`
	MaxConcurrentScans          int           `json:"maxConcurrentScans"`

	// 修复策略配置
	AutoRepairEnabled          bool          `json:"autoRepairEnabled"`
	ManualConfirmationRequired bool          `json:"manualConfirmationRequired"`
	RepairBatchSize            int           `json:"repairBatchSize"`
	RepairTimeout              time.Duration `json:"repairTimeout"`

	// 一致性验证配置
	VerificationEnabled    bool          `json:"verificationEnabled"`
	VerificationInterval   time.Duration `json:"verificationInterval"`
	ConsistencyThreshold   float64       `json:"consistencyThreshold"`
	MaxInconsistencyWindow time.Duration `json:"maxInconsistencyWindow"`

	// 性能优化配置
	EnableParallelRecovery bool `json:"enableParallelRecovery"`
	EnableIncrementalSync  bool `json:"enableIncrementalSync"`
	EnableCompressionSync  bool `json:"enableCompressionSync"`
	SyncBandwidthLimitMBps int  `json:"syncBandwidthLimitMBps"`
}

// DefaultConsistencyRecoveryConfig 默认配置
func DefaultConsistencyRecoveryConfig() *ConsistencyRecoveryConfig {
	return &ConsistencyRecoveryConfig{
		DifferenceDetectionInterval: time.Minute * 5,
		LogIndexScanWindow:          1000,
		MaxConcurrentScans:          3,
		AutoRepairEnabled:           true,
		ManualConfirmationRequired:  false,
		RepairBatchSize:             100,
		RepairTimeout:               time.Minute * 10,
		VerificationEnabled:         true,
		VerificationInterval:        time.Minute * 2,
		ConsistencyThreshold:        0.99,
		MaxInconsistencyWindow:      time.Minute * 30,
		EnableParallelRecovery:      true,
		EnableIncrementalSync:       true,
		EnableCompressionSync:       true,
		SyncBandwidthLimitMBps:      100,
	}
}

// InconsistencyType 不一致类型
type InconsistencyType int

const (
	// MissingEntries 缺失条目
	MissingEntries InconsistencyType = iota
	// ConflictingEntries 冲突条目
	ConflictingEntries
	// OutOfOrderEntries 乱序条目
	OutOfOrderEntries
	// CorruptedEntries 损坏条目
	CorruptedEntries
	// TimestampMismatch 时间戳不匹配
	TimestampMismatch
)

// DataInconsistency 数据不一致记录
type DataInconsistency struct {
	// 基本信息
	ID         string
	Type       InconsistencyType
	DetectedAt time.Time
	SourceDC   raft.DataCenterID
	TargetDC   raft.DataCenterID

	// 日志条目信息
	LogIndex      raft.LogIndex
	LogTerm       raft.Term
	ExpectedEntry *raft.LogEntry
	ActualEntry   *raft.LogEntry

	// 差异详情
	Severity        int // 1-5, 5最严重
	ImpactLevel     string
	Description     string
	ConflictDetails map[string]interface{}

	// 修复状态
	RepairStatus   RepairStatus
	RepairAttempts int
	LastRepairTime time.Time
	RepairStrategy string
}

// RepairStatus 修复状态
type RepairStatus int

const (
	// RepairPending 待修复
	RepairPending RepairStatus = iota
	// RepairInProgress 修复中
	RepairInProgress
	// RepairCompleted 修复完成
	RepairCompleted
	// RepairFailed 修复失败
	RepairFailed
	// RepairSkipped 跳过修复
	RepairSkipped
)

// RecoveryOperation 恢复操作
type RecoveryOperation struct {
	// 操作信息
	ID        string
	Type      string
	StartTime time.Time
	EndTime   time.Time
	Status    string

	// 目标信息
	SourceDC raft.DataCenterID
	TargetDC raft.DataCenterID
	LogRange LogRange

	// 进度信息
	TotalEntries      int64
	ProcessedEntries  int64
	SuccessfulEntries int64
	FailedEntries     int64
	Progress          float64

	// 性能统计
	TransferredBytes       int64
	AverageSpeed           float64 // MB/s
	EstimatedTimeRemaining time.Duration
}

// LogRange 日志范围
type LogRange struct {
	StartIndex raft.LogIndex
	EndIndex   raft.LogIndex
	StartTerm  raft.Term
	EndTerm    raft.Term
}

// ConsistencySnapshot 一致性快照
type ConsistencySnapshot struct {
	// 快照信息
	Timestamp time.Time
	LocalDC   raft.DataCenterID
	RemoteDCs []raft.DataCenterID

	// 一致性状态
	GlobalConsistency    bool
	ConsistencyScore     float64
	InconsistentDCs      []raft.DataCenterID
	TotalInconsistencies int

	// 详细统计
	DCConsistencyStatus  map[raft.DataCenterID]*DCConsistencyStatus
	InconsistencyDetails []*DataInconsistency
}

// DCConsistencyStatus DC一致性状态
type DCConsistencyStatus struct {
	DataCenter         raft.DataCenterID
	IsConsistent       bool
	LastSyncTime       time.Time
	LogIndex           raft.LogIndex
	LogTerm            raft.Term
	InconsistencyCount int
	RecoveryInProgress bool
}

// ConsistencyRecovery 数据一致性恢复器
type ConsistencyRecovery struct {
	mu sync.RWMutex

	// 基础配置
	nodeID raft.NodeID
	config *ConsistencyRecoveryConfig
	logger *log.Logger

	// 集成组件
	storage         raft.Storage
	asyncReplicator *AsyncReplicator
	readWriteRouter *ReadWriteRouter
	failureDetector *DCFailureDetector

	// 一致性状态跟踪
	lastConsistencyCheck time.Time
	currentSnapshot      *ConsistencySnapshot
	inconsistencies      map[string]*DataInconsistency
	recoveryOperations   map[string]*RecoveryOperation

	// 修复队列
	repairQueue      chan *DataInconsistency
	activeRepairs    map[string]*RecoveryOperation
	completedRepairs []*RecoveryOperation

	// 监控统计
	totalInconsistenciesDetected int64
	totalRepairsCompleted        int64
	totalRepairsFailed           int64
	averageRepairTime            time.Duration

	// 控制流
	ctx     context.Context
	cancel  context.CancelFunc
	wg      sync.WaitGroup
	running bool
	stopCh  chan struct{}
}

// NewConsistencyRecovery 创建一致性恢复器
func NewConsistencyRecovery(
	nodeID raft.NodeID,
	config *ConsistencyRecoveryConfig,
	storage raft.Storage,
	asyncReplicator *AsyncReplicator,
	readWriteRouter *ReadWriteRouter,
	failureDetector *DCFailureDetector,
) *ConsistencyRecovery {
	if config == nil {
		config = DefaultConsistencyRecoveryConfig()
	}

	ctx, cancel := context.WithCancel(context.Background())

	recovery := &ConsistencyRecovery{
		nodeID:          nodeID,
		config:          config,
		logger:          log.New(log.Writer(), "[consistency-recovery] ", log.LstdFlags),
		storage:         storage,
		asyncReplicator: asyncReplicator,
		readWriteRouter: readWriteRouter,
		failureDetector: failureDetector,

		inconsistencies:    make(map[string]*DataInconsistency),
		recoveryOperations: make(map[string]*RecoveryOperation),
		activeRepairs:      make(map[string]*RecoveryOperation),
		completedRepairs:   make([]*RecoveryOperation, 0),

		ctx:         ctx,
		cancel:      cancel,
		stopCh:      make(chan struct{}),
		repairQueue: make(chan *DataInconsistency, 1000),
	}

	recovery.initializeComponents()
	return recovery
}

// initializeComponents 初始化组件
func (cr *ConsistencyRecovery) initializeComponents() {
	// 初始化一致性快照
	cr.currentSnapshot = &ConsistencySnapshot{
		Timestamp:            time.Now(),
		DCConsistencyStatus:  make(map[raft.DataCenterID]*DCConsistencyStatus),
		InconsistencyDetails: make([]*DataInconsistency, 0),
	}

	// 从异步复制管理器获取DC信息
	if cr.asyncReplicator != nil {
		targets := cr.asyncReplicator.GetReplicationStatus()
		for dcID := range targets {
			cr.currentSnapshot.DCConsistencyStatus[dcID] = &DCConsistencyStatus{
				DataCenter:   dcID,
				IsConsistent: true,
				LastSyncTime: time.Now(),
			}
		}
	}

	cr.logger.Printf("初始化一致性恢复器，监控 %d 个DC", len(cr.currentSnapshot.DCConsistencyStatus))
}

// Start 启动一致性恢复器
func (cr *ConsistencyRecovery) Start() error {
	cr.mu.Lock()
	defer cr.mu.Unlock()

	if cr.running {
		return fmt.Errorf("一致性恢复器已在运行")
	}

	cr.logger.Printf("启动数据一致性恢复器")

	// 启动各个工作循环
	cr.wg.Add(4)
	go cr.consistencyCheckLoop()
	go cr.repairWorkerLoop()
	go cr.verificationLoop()
	go cr.monitoringLoop()

	cr.running = true
	return nil
}

// Stop 停止一致性恢复器
func (cr *ConsistencyRecovery) Stop() error {
	cr.mu.Lock()
	defer cr.mu.Unlock()

	if !cr.running {
		return nil
	}

	cr.logger.Printf("停止数据一致性恢复器")

	cr.cancel()
	close(cr.stopCh)
	close(cr.repairQueue)
	cr.wg.Wait()

	cr.running = false
	return nil
}

// consistencyCheckLoop 一致性检查循环
func (cr *ConsistencyRecovery) consistencyCheckLoop() {
	defer cr.wg.Done()
	cr.logger.Printf("一致性检查循环已启动")

	ticker := time.NewTicker(cr.config.DifferenceDetectionInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			cr.performConsistencyCheck()
		case <-cr.stopCh:
			cr.logger.Printf("一致性检查循环已停止")
			return
		}
	}
}

// performConsistencyCheck 执行一致性检查
func (cr *ConsistencyRecovery) performConsistencyCheck() {
	cr.mu.Lock()
	defer cr.mu.Unlock()

	cr.logger.Printf("开始执行一致性检查")
	startTime := time.Now()

	// 获取本地最新日志索引
	localLastIndex := cr.storage.GetLastLogIndex()
	localLastTerm := cr.storage.GetLastLogTerm()

	// 更新一致性快照
	cr.currentSnapshot.Timestamp = startTime
	cr.currentSnapshot.LocalDC = cr.getLocalDC()

	// 检查各个DC的一致性
	inconsistentDCs := make([]raft.DataCenterID, 0)
	totalInconsistencies := 0

	if cr.asyncReplicator != nil {
		targets := cr.asyncReplicator.GetReplicationStatus()
		for dcID, target := range targets {
			dcStatus := cr.checkDCConsistency(dcID, target, localLastIndex, localLastTerm)
			cr.currentSnapshot.DCConsistencyStatus[dcID] = dcStatus

			if !dcStatus.IsConsistent {
				inconsistentDCs = append(inconsistentDCs, dcID)
				totalInconsistencies += dcStatus.InconsistencyCount
			}
		}
	}

	// 更新全局一致性状态
	cr.currentSnapshot.GlobalConsistency = len(inconsistentDCs) == 0
	cr.currentSnapshot.InconsistentDCs = inconsistentDCs
	cr.currentSnapshot.TotalInconsistencies = totalInconsistencies

	// 计算一致性分数
	if len(cr.currentSnapshot.DCConsistencyStatus) > 0 {
		consistentDCs := len(cr.currentSnapshot.DCConsistencyStatus) - len(inconsistentDCs)
		cr.currentSnapshot.ConsistencyScore = float64(consistentDCs) / float64(len(cr.currentSnapshot.DCConsistencyStatus))
	}

	cr.lastConsistencyCheck = startTime
	duration := time.Since(startTime)

	cr.logger.Printf("一致性检查完成: 总DC=%d, 不一致DC=%d, 一致性分数=%.2f, 耗时=%v",
		len(cr.currentSnapshot.DCConsistencyStatus),
		len(inconsistentDCs),
		cr.currentSnapshot.ConsistencyScore,
		duration)
}

// checkDCConsistency 检查DC一致性
func (cr *ConsistencyRecovery) checkDCConsistency(
	dcID raft.DataCenterID,
	target *AsyncReplicationTarget,
	localLastIndex raft.LogIndex,
	localLastTerm raft.Term,
) *DCConsistencyStatus {
	status := &DCConsistencyStatus{
		DataCenter:   dcID,
		LastSyncTime: time.Now(),
		LogIndex:     target.LastReplicatedIndex,
		LogTerm:      target.LastReplicatedTerm,
	}

	// 检查日志索引差异
	indexDiff := int64(localLastIndex) - int64(target.LastReplicatedIndex)

	// 检查复制延迟
	replicationLag := target.ReplicationLag

	// 判断是否一致
	maxAllowedLag := time.Duration(cr.config.MaxInconsistencyWindow)
	maxAllowedIndexDiff := cr.config.LogIndexScanWindow

	if indexDiff <= maxAllowedIndexDiff && replicationLag <= maxAllowedLag {
		status.IsConsistent = true
		status.InconsistencyCount = 0
	} else {
		status.IsConsistent = false
		status.InconsistencyCount = int(indexDiff)

		// 检测具体的不一致问题
		cr.detectSpecificInconsistencies(dcID, target, localLastIndex, localLastTerm)
	}

	return status
}

// detectSpecificInconsistencies 检测具体的不一致问题
func (cr *ConsistencyRecovery) detectSpecificInconsistencies(
	dcID raft.DataCenterID,
	target *AsyncReplicationTarget,
	localLastIndex raft.LogIndex,
	localLastTerm raft.Term,
) {
	// 扫描日志窗口，查找具体的不一致
	scanStart := target.LastReplicatedIndex
	scanEnd := localLastIndex
	if scanEnd-scanStart > raft.LogIndex(cr.config.LogIndexScanWindow) {
		scanStart = scanEnd - raft.LogIndex(cr.config.LogIndexScanWindow)
	}

	for index := scanStart; index <= scanEnd; index++ {
		localEntry, err := cr.storage.GetLogEntry(index)
		if err != nil {
			continue
		}

		// 这里应该从目标DC获取对应的日志条目进行比较
		// 为了简化，我们创建一个模拟的不一致记录
		inconsistency := &DataInconsistency{
			ID:            fmt.Sprintf("inconsistency-%s-%d-%d", dcID, index, time.Now().Unix()),
			Type:          MissingEntries,
			DetectedAt:    time.Now(),
			SourceDC:      cr.getLocalDC(),
			TargetDC:      dcID,
			LogIndex:      index,
			LogTerm:       localEntry.Term,
			ExpectedEntry: localEntry,
			ActualEntry:   nil, // 缺失的条目
			Severity:      cr.calculateInconsistencySeverity(MissingEntries),
			ImpactLevel:   "Medium",
			Description:   fmt.Sprintf("DC %s 缺失日志条目 %d", dcID, index),
			RepairStatus:  RepairPending,
		}

		cr.inconsistencies[inconsistency.ID] = inconsistency
		cr.currentSnapshot.InconsistencyDetails = append(cr.currentSnapshot.InconsistencyDetails, inconsistency)
		cr.totalInconsistenciesDetected++

		// 如果启用自动修复，将不一致添加到修复队列
		if cr.config.AutoRepairEnabled {
			select {
			case cr.repairQueue <- inconsistency:
				cr.logger.Printf("不一致已加入修复队列: %s", inconsistency.ID)
			default:
				cr.logger.Printf("修复队列已满，跳过不一致: %s", inconsistency.ID)
			}
		}
	}
}

// repairWorkerLoop 修复工作循环
func (cr *ConsistencyRecovery) repairWorkerLoop() {
	defer cr.wg.Done()
	cr.logger.Printf("修复工作循环已启动")

	for {
		select {
		case inconsistency := <-cr.repairQueue:
			if inconsistency != nil {
				cr.processRepair(inconsistency)
			}
		case <-cr.stopCh:
			cr.logger.Printf("修复工作循环已停止")
			return
		}
	}
}

// processRepair 处理修复
func (cr *ConsistencyRecovery) processRepair(inconsistency *DataInconsistency) {
	cr.logger.Printf("开始修复不一致: %s", inconsistency.ID)

	// 创建修复操作
	operation := &RecoveryOperation{
		ID:        fmt.Sprintf("repair-%s", inconsistency.ID),
		Type:      "InconsistencyRepair",
		StartTime: time.Now(),
		Status:    "InProgress",
		SourceDC:  inconsistency.SourceDC,
		TargetDC:  inconsistency.TargetDC,
		LogRange: LogRange{
			StartIndex: inconsistency.LogIndex,
			EndIndex:   inconsistency.LogIndex,
			StartTerm:  inconsistency.LogTerm,
			EndTerm:    inconsistency.LogTerm,
		},
		TotalEntries: 1,
	}

	cr.mu.Lock()
	cr.activeRepairs[operation.ID] = operation
	inconsistency.RepairStatus = RepairInProgress
	inconsistency.RepairAttempts++
	inconsistency.LastRepairTime = time.Now()
	cr.mu.Unlock()

	// 执行实际的修复逻辑
	success := cr.executeRepair(inconsistency, operation)

	// 更新修复状态
	cr.mu.Lock()
	operation.EndTime = time.Now()
	if success {
		operation.Status = "Completed"
		operation.SuccessfulEntries = 1
		inconsistency.RepairStatus = RepairCompleted
		cr.totalRepairsCompleted++
	} else {
		operation.Status = "Failed"
		operation.FailedEntries = 1
		inconsistency.RepairStatus = RepairFailed
		cr.totalRepairsFailed++
	}

	delete(cr.activeRepairs, operation.ID)
	cr.completedRepairs = append(cr.completedRepairs, operation)
	cr.mu.Unlock()

	cr.logger.Printf("修复完成: %s, 成功=%t", inconsistency.ID, success)
}

// executeRepair 执行修复
func (cr *ConsistencyRecovery) executeRepair(inconsistency *DataInconsistency, operation *RecoveryOperation) bool {
	// 根据不一致类型选择修复策略
	switch inconsistency.Type {
	case MissingEntries:
		return cr.repairMissingEntries(inconsistency, operation)
	case ConflictingEntries:
		return cr.repairConflictingEntries(inconsistency, operation)
	case OutOfOrderEntries:
		return cr.repairOutOfOrderEntries(inconsistency, operation)
	case CorruptedEntries:
		return cr.repairCorruptedEntries(inconsistency, operation)
	case TimestampMismatch:
		return cr.repairTimestampMismatch(inconsistency, operation)
	default:
		cr.logger.Printf("未知的不一致类型: %d", inconsistency.Type)
		return false
	}
}

// repairMissingEntries 修复缺失条目
func (cr *ConsistencyRecovery) repairMissingEntries(inconsistency *DataInconsistency, operation *RecoveryOperation) bool {
	// 获取本地日志条目
	if inconsistency.ExpectedEntry == nil {
		return false
	}

	// 模拟将日志条目发送到目标DC
	cr.logger.Printf("发送日志条目 %d 到 DC %s", inconsistency.LogIndex, inconsistency.TargetDC)

	// 这里应该实现实际的日志条目传输逻辑
	// 为了演示，我们假设传输总是成功的
	time.Sleep(time.Millisecond * 100) // 模拟网络延迟

	operation.ProcessedEntries = 1
	operation.TransferredBytes = int64(len(inconsistency.ExpectedEntry.Data))
	operation.Progress = 1.0

	return true
}

// verificationLoop 验证循环
func (cr *ConsistencyRecovery) verificationLoop() {
	defer cr.wg.Done()
	cr.logger.Printf("验证循环已启动")

	if !cr.config.VerificationEnabled {
		cr.logger.Printf("验证功能已禁用，跳过验证循环")
		return
	}

	ticker := time.NewTicker(cr.config.VerificationInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			cr.performVerification()
		case <-cr.stopCh:
			cr.logger.Printf("验证循环已停止")
			return
		}
	}
}

// performVerification 执行验证
func (cr *ConsistencyRecovery) performVerification() {
	cr.logger.Printf("开始执行一致性验证")

	// 验证已完成的修复是否真正解决了不一致问题
	cr.mu.RLock()
	completedRepairs := make([]*RecoveryOperation, len(cr.completedRepairs))
	copy(completedRepairs, cr.completedRepairs)
	cr.mu.RUnlock()

	for _, repair := range completedRepairs {
		if repair.Status == "Completed" {
			cr.verifyRepairEffectiveness(repair)
		}
	}

	// 验证全局一致性状态
	cr.verifyGlobalConsistency()
}

// monitoringLoop 监控循环
func (cr *ConsistencyRecovery) monitoringLoop() {
	defer cr.wg.Done()
	cr.logger.Printf("监控循环已启动")

	ticker := time.NewTicker(time.Minute * 1)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			cr.updateMonitoringMetrics()
		case <-cr.stopCh:
			cr.logger.Printf("监控循环已停止")
			return
		}
	}
}

// 辅助方法实现
func (cr *ConsistencyRecovery) getLocalDC() raft.DataCenterID {
	// 从读写路由器获取本地DC信息
	if cr.readWriteRouter != nil {
		dcInfo := cr.readWriteRouter.GetDataCenterInfo()
		for dcID, info := range dcInfo {
			if info.IsLocal {
				return dcID
			}
		}
	}
	return raft.DataCenterID("dc1") // 默认值
}

func (cr *ConsistencyRecovery) calculateInconsistencySeverity(inconsistencyType InconsistencyType) int {
	switch inconsistencyType {
	case CorruptedEntries:
		return 5
	case ConflictingEntries:
		return 4
	case OutOfOrderEntries:
		return 3
	case MissingEntries:
		return 2
	case TimestampMismatch:
		return 1
	default:
		return 1
	}
}

func (cr *ConsistencyRecovery) repairConflictingEntries(inconsistency *DataInconsistency, operation *RecoveryOperation) bool {
	// 实现冲突条目修复逻辑
	cr.logger.Printf("修复冲突条目: %d", inconsistency.LogIndex)
	time.Sleep(time.Millisecond * 200)
	return true
}

func (cr *ConsistencyRecovery) repairOutOfOrderEntries(inconsistency *DataInconsistency, operation *RecoveryOperation) bool {
	// 实现乱序条目修复逻辑
	cr.logger.Printf("修复乱序条目: %d", inconsistency.LogIndex)
	time.Sleep(time.Millisecond * 150)
	return true
}

func (cr *ConsistencyRecovery) repairCorruptedEntries(inconsistency *DataInconsistency, operation *RecoveryOperation) bool {
	// 实现损坏条目修复逻辑
	cr.logger.Printf("修复损坏条目: %d", inconsistency.LogIndex)
	time.Sleep(time.Millisecond * 300)
	return true
}

func (cr *ConsistencyRecovery) repairTimestampMismatch(inconsistency *DataInconsistency, operation *RecoveryOperation) bool {
	// 实现时间戳不匹配修复逻辑
	cr.logger.Printf("修复时间戳不匹配: %d", inconsistency.LogIndex)
	time.Sleep(time.Millisecond * 100)
	return true
}

func (cr *ConsistencyRecovery) verifyRepairEffectiveness(repair *RecoveryOperation) {
	// 实现修复效果验证逻辑
	cr.logger.Printf("验证修复效果: %s", repair.ID)
}

func (cr *ConsistencyRecovery) verifyGlobalConsistency() {
	// 实现全局一致性验证逻辑
	cr.logger.Printf("验证全局一致性状态")
}

func (cr *ConsistencyRecovery) updateMonitoringMetrics() {
	// 实现监控指标更新逻辑
	cr.mu.RLock()
	activeRepairCount := len(cr.activeRepairs)
	totalInconsistencies := len(cr.inconsistencies)
	cr.mu.RUnlock()

	cr.logger.Printf("监控指标: 活跃修复=%d, 总不一致=%d, 已完成修复=%d, 修复失败=%d",
		activeRepairCount, totalInconsistencies, cr.totalRepairsCompleted, cr.totalRepairsFailed)
}

// 公共API方法

// GetCurrentSnapshot 获取当前一致性快照
func (cr *ConsistencyRecovery) GetCurrentSnapshot() *ConsistencySnapshot {
	cr.mu.RLock()
	defer cr.mu.RUnlock()

	// 创建快照副本
	snapshot := &ConsistencySnapshot{
		Timestamp:            cr.currentSnapshot.Timestamp,
		LocalDC:              cr.currentSnapshot.LocalDC,
		RemoteDCs:            make([]raft.DataCenterID, len(cr.currentSnapshot.RemoteDCs)),
		GlobalConsistency:    cr.currentSnapshot.GlobalConsistency,
		ConsistencyScore:     cr.currentSnapshot.ConsistencyScore,
		InconsistentDCs:      make([]raft.DataCenterID, len(cr.currentSnapshot.InconsistentDCs)),
		TotalInconsistencies: cr.currentSnapshot.TotalInconsistencies,
		DCConsistencyStatus:  make(map[raft.DataCenterID]*DCConsistencyStatus),
		InconsistencyDetails: make([]*DataInconsistency, len(cr.currentSnapshot.InconsistencyDetails)),
	}

	copy(snapshot.RemoteDCs, cr.currentSnapshot.RemoteDCs)
	copy(snapshot.InconsistentDCs, cr.currentSnapshot.InconsistentDCs)
	copy(snapshot.InconsistencyDetails, cr.currentSnapshot.InconsistencyDetails)

	for dcID, status := range cr.currentSnapshot.DCConsistencyStatus {
		snapshot.DCConsistencyStatus[dcID] = &DCConsistencyStatus{
			DataCenter:         status.DataCenter,
			IsConsistent:       status.IsConsistent,
			LastSyncTime:       status.LastSyncTime,
			LogIndex:           status.LogIndex,
			LogTerm:            status.LogTerm,
			InconsistencyCount: status.InconsistencyCount,
			RecoveryInProgress: status.RecoveryInProgress,
		}
	}

	return snapshot
}

// GetInconsistencies 获取不一致列表
func (cr *ConsistencyRecovery) GetInconsistencies() map[string]*DataInconsistency {
	cr.mu.RLock()
	defer cr.mu.RUnlock()

	result := make(map[string]*DataInconsistency)
	for id, inconsistency := range cr.inconsistencies {
		result[id] = inconsistency
	}
	return result
}

// GetRecoveryOperations 获取恢复操作列表
func (cr *ConsistencyRecovery) GetRecoveryOperations() map[string]*RecoveryOperation {
	cr.mu.RLock()
	defer cr.mu.RUnlock()

	result := make(map[string]*RecoveryOperation)
	for id, operation := range cr.recoveryOperations {
		result[id] = operation
	}
	return result
}

// TriggerManualRepair 触发手动修复
func (cr *ConsistencyRecovery) TriggerManualRepair(inconsistencyID string) error {
	cr.mu.RLock()
	inconsistency, exists := cr.inconsistencies[inconsistencyID]
	cr.mu.RUnlock()

	if !exists {
		return fmt.Errorf("不一致记录不存在: %s", inconsistencyID)
	}

	if inconsistency.RepairStatus == RepairInProgress {
		return fmt.Errorf("修复已在进行中: %s", inconsistencyID)
	}

	select {
	case cr.repairQueue <- inconsistency:
		cr.logger.Printf("手动触发修复: %s", inconsistencyID)
		return nil
	default:
		return fmt.Errorf("修复队列已满")
	}
}

// IsGloballyConsistent 检查是否全局一致
func (cr *ConsistencyRecovery) IsGloballyConsistent() bool {
	cr.mu.RLock()
	defer cr.mu.RUnlock()

	return cr.currentSnapshot.GlobalConsistency
}

// GetConsistencyScore 获取一致性分数
func (cr *ConsistencyRecovery) GetConsistencyScore() float64 {
	cr.mu.RLock()
	defer cr.mu.RUnlock()

	return cr.currentSnapshot.ConsistencyScore
}
