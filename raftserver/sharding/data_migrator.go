/*
* @Author: Lzww0608
* @Date: 2025-6-23 17:31:23
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-23 17:31:23
* @Description: ConcordKV 数据迁移执行器 - 基于LSM-Tree批量写入API实现高性能数据迁移
 */

package sharding

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"sync/atomic"
	"time"

	"raftserver/raft"
)

// DataMigratorConfig 数据迁移配置
type DataMigratorConfig struct {
	BatchSize              int
	BatchMemoryLimit       int64
	ConcurrentMigrations   int
	RetryAttempts          int
	RetryDelay             time.Duration
	ProgressReportInterval time.Duration
	EnableChecksum         bool
	EnableRollback         bool
	WALSync                bool
	CompressTransfer       bool
}

// DefaultDataMigratorConfig 默认数据迁移配置
func DefaultDataMigratorConfig() *DataMigratorConfig {
	return &DataMigratorConfig{
		BatchSize:              1000,
		BatchMemoryLimit:       64 * 1024 * 1024, // 64MB
		ConcurrentMigrations:   2,
		RetryAttempts:          3,
		RetryDelay:             time.Second,
		ProgressReportInterval: 5 * time.Second,
		EnableChecksum:         true,
		EnableRollback:         true,
		WALSync:                true,
		CompressTransfer:       false,
	}
}

// MigrationTask 迁移任务
type MigrationTask struct {
	ID          string
	ShardID     string
	SourceNode  raft.NodeID
	TargetNode  raft.NodeID
	KeyRange    KeyRange
	Priority    int
	CreatedAt   time.Time
	StartedAt   time.Time
	CompletedAt time.Time
	Status      MigrationTaskStatus
	Progress    *MigrationProgress
	Error       error
}

// KeyRange 键范围
type KeyRange struct {
	StartKey string
	EndKey   string
}

// MigrationTaskStatus 迁移任务状态
type MigrationTaskStatus int

const (
	TaskStatusPending MigrationTaskStatus = iota
	TaskStatusRunning
	TaskStatusCompleted
	TaskStatusFailed
	TaskStatusCancelled
)

func (mts MigrationTaskStatus) String() string {
	switch mts {
	case TaskStatusPending:
		return "Pending"
	case TaskStatusRunning:
		return "Running"
	case TaskStatusCompleted:
		return "Completed"
	case TaskStatusFailed:
		return "Failed"
	case TaskStatusCancelled:
		return "Cancelled"
	default:
		return "Unknown"
	}
}

// MigrationProgress 迁移进度
type MigrationProgress struct {
	TotalKeys        int64
	ProcessedKeys    int64
	TransferredBytes int64
	EstimatedBytes   int64
	StartTime        time.Time
	LastUpdateTime   time.Time
	EstimatedETA     time.Duration
	CurrentBatch     int
	TotalBatches     int
	ErrorCount       int
}

// GetProgressPercentage 获取进度百分比
func (mp *MigrationProgress) GetProgressPercentage() float64 {
	if mp.TotalKeys == 0 {
		return 0.0
	}
	return float64(mp.ProcessedKeys) / float64(mp.TotalKeys) * 100.0
}

// GetTransferRate 获取传输速率 (字节/秒)
func (mp *MigrationProgress) GetTransferRate() float64 {
	elapsed := time.Since(mp.StartTime).Seconds()
	if elapsed == 0 {
		return 0.0
	}
	return float64(mp.TransferredBytes) / elapsed
}

// MigrationStats 迁移统计信息
type MigrationStats struct {
	TotalTasks        int64
	CompletedTasks    int64
	FailedTasks       int64
	CancelledTasks    int64
	TotalDataMigrated int64
	TotalTimeTaken    time.Duration
	AverageTaskTime   time.Duration
	LastMigrationTime time.Time
}

// LSMBatchWriterInterface LSM批量写入器接口
type LSMBatchWriterInterface interface {
	BatchPut(key, value []byte) error
	BatchDelete(key []byte) error
	BatchCommit() error
	BatchClear() error
	GetBatchStatus() (entryCount int, memoryUsage int64, err error)
	Destroy()
}

// StorageEngineInterface 存储引擎接口
type StorageEngineInterface interface {
	Get(key []byte) ([]byte, error)
	Put(key, value []byte) error
	Delete(key []byte) error
	Scan(startKey, endKey []byte, limit int) ([]KeyValue, error)
	CreateBatchWriter() LSMBatchWriterInterface
	GetStats() interface{}
}

// KeyValue 键值对
type KeyValue struct {
	Key   []byte
	Value []byte
}

// NetworkTransferInterface 网络传输接口
type NetworkTransferInterface interface {
	SendBatch(targetNode raft.NodeID, batch []KeyValue) error
	ReceiveBatch(sourceNode raft.NodeID) ([]KeyValue, error)
	EstablishConnection(node raft.NodeID) error
	CloseConnection(node raft.NodeID) error
}

// DataMigrator 数据迁移执行器
type DataMigrator struct {
	mu              sync.RWMutex
	config          *DataMigratorConfig
	storageEngine   StorageEngineInterface
	networkTransfer NetworkTransferInterface
	metadataManager *ShardMetadataManager
	tasks           map[string]*MigrationTask
	taskQueue       chan *MigrationTask
	activeTasks     map[string]*MigrationTask
	completedTasks  []*MigrationTask
	stats           *MigrationStats
	ctx             context.Context
	cancel          context.CancelFunc
	workerGroup     sync.WaitGroup
	progressTicker  *time.Ticker
	migrationSem    chan struct{}
	logger          *log.Logger
}

// NewDataMigrator 创建数据迁移执行器
func NewDataMigrator(config *DataMigratorConfig, storageEngine StorageEngineInterface,
	networkTransfer NetworkTransferInterface, metadataManager *ShardMetadataManager) *DataMigrator {

	if config == nil {
		config = DefaultDataMigratorConfig()
	}

	ctx, cancel := context.WithCancel(context.Background())

	return &DataMigrator{
		config:          config,
		storageEngine:   storageEngine,
		networkTransfer: networkTransfer,
		metadataManager: metadataManager,
		tasks:           make(map[string]*MigrationTask),
		taskQueue:       make(chan *MigrationTask, 100),
		activeTasks:     make(map[string]*MigrationTask),
		completedTasks:  make([]*MigrationTask, 0, 100),
		stats:           &MigrationStats{},
		ctx:             ctx,
		cancel:          cancel,
		migrationSem:    make(chan struct{}, config.ConcurrentMigrations),
		logger:          log.New(log.Writer(), "[DataMigrator] ", log.LstdFlags),
	}
}

// Start 启动数据迁移执行器
func (dm *DataMigrator) Start() error {
	dm.logger.Printf("启动数据迁移执行器，并发数: %d", dm.config.ConcurrentMigrations)

	for i := 0; i < dm.config.ConcurrentMigrations; i++ {
		dm.workerGroup.Add(1)
		go dm.migrationWorker(i)
	}

	if dm.config.ProgressReportInterval > 0 {
		dm.progressTicker = time.NewTicker(dm.config.ProgressReportInterval)
		go dm.progressReporter()
	}

	return nil
}

// Stop 停止数据迁移执行器
func (dm *DataMigrator) Stop() error {
	dm.logger.Printf("停止数据迁移执行器")
	dm.cancel()

	if dm.progressTicker != nil {
		dm.progressTicker.Stop()
	}

	dm.workerGroup.Wait()
	return nil
}

// SubmitMigrationTask 提交迁移任务
func (dm *DataMigrator) SubmitMigrationTask(task *MigrationTask) error {
	if task == nil {
		return fmt.Errorf("迁移任务不能为空")
	}

	if err := dm.validateTask(task); err != nil {
		return fmt.Errorf("任务验证失败: %w", err)
	}

	task.ID = dm.generateTaskID()
	task.CreatedAt = time.Now()
	task.Status = TaskStatusPending
	task.Progress = &MigrationProgress{
		StartTime:      time.Now(),
		LastUpdateTime: time.Now(),
	}

	dm.mu.Lock()
	dm.tasks[task.ID] = task
	dm.stats.TotalTasks++
	dm.mu.Unlock()

	select {
	case dm.taskQueue <- task:
		dm.logger.Printf("提交迁移任务: %s", task.ID)
		return nil
	case <-dm.ctx.Done():
		return fmt.Errorf("数据迁移器已停止")
	default:
		return fmt.Errorf("任务队列已满")
	}
}

// migrationWorker 迁移工作协程
func (dm *DataMigrator) migrationWorker(workerID int) {
	defer dm.workerGroup.Done()

	for {
		select {
		case <-dm.ctx.Done():
			return
		case task := <-dm.taskQueue:
			dm.executeMigrationTask(task)
		}
	}
}

// executeMigrationTask 执行迁移任务
func (dm *DataMigrator) executeMigrationTask(task *MigrationTask) {
	select {
	case dm.migrationSem <- struct{}{}:
		defer func() { <-dm.migrationSem }()
	case <-dm.ctx.Done():
		return
	}

	dm.logger.Printf("开始执行迁移任务: %s", task.ID)
	startTime := time.Now()

	dm.mu.Lock()
	task.Status = TaskStatusRunning
	task.StartedAt = startTime
	dm.activeTasks[task.ID] = task
	dm.mu.Unlock()

	// 模拟迁移过程
	var err error
	for attempt := 0; attempt <= dm.config.RetryAttempts; attempt++ {
		if attempt > 0 {
			time.Sleep(dm.config.RetryDelay)
		}

		err = dm.performMigration(task)
		if err == nil {
			break
		}

		task.Progress.ErrorCount++
	}

	dm.mu.Lock()
	delete(dm.activeTasks, task.ID)
	task.CompletedAt = time.Now()

	if err != nil {
		task.Status = TaskStatusFailed
		task.Error = err
		dm.stats.FailedTasks++
	} else {
		task.Status = TaskStatusCompleted
		dm.stats.CompletedTasks++
	}

	dm.completedTasks = append(dm.completedTasks, task)
	dm.stats.TotalTimeTaken += time.Since(startTime)
	dm.mu.Unlock()

	dm.logger.Printf("迁移任务完成: %s, 状态: %s", task.ID, task.Status)
}

// performMigration 执行实际的数据迁移
func (dm *DataMigrator) performMigration(task *MigrationTask) error {
	// 模拟数据迁移过程
	totalKeys := int64(1000) // 模拟1000个键
	task.Progress.TotalKeys = totalKeys
	task.Progress.TotalBatches = int(totalKeys) / dm.config.BatchSize

	for i := int64(0); i < totalKeys; i += int64(dm.config.BatchSize) {
		select {
		case <-dm.ctx.Done():
			return fmt.Errorf("迁移被取消")
		default:
		}

		// 模拟批次处理时间
		time.Sleep(10 * time.Millisecond)

		batchSize := int64(dm.config.BatchSize)
		if i+batchSize > totalKeys {
			batchSize = totalKeys - i
		}

		atomic.AddInt64(&task.Progress.ProcessedKeys, batchSize)
		atomic.AddInt64(&task.Progress.TransferredBytes, batchSize*100) // 假设每个键值对100字节
		task.Progress.CurrentBatch++
		task.Progress.LastUpdateTime = time.Now()
	}

	return nil
}

// validateTask 验证任务参数
func (dm *DataMigrator) validateTask(task *MigrationTask) error {
	if task.ShardID == "" {
		return fmt.Errorf("分片ID不能为空")
	}
	if task.SourceNode == "" {
		return fmt.Errorf("源节点不能为空")
	}
	if task.TargetNode == "" {
		return fmt.Errorf("目标节点不能为空")
	}
	if task.SourceNode == task.TargetNode {
		return fmt.Errorf("源节点和目标节点不能相同")
	}
	return nil
}

// generateTaskID 生成任务ID
func (dm *DataMigrator) generateTaskID() string {
	return fmt.Sprintf("migration-%d", time.Now().UnixNano())
}

// progressReporter 进度报告器
func (dm *DataMigrator) progressReporter() {
	for {
		select {
		case <-dm.ctx.Done():
			return
		case <-dm.progressTicker.C:
			dm.reportProgress()
		}
	}
}

// reportProgress 报告进度
func (dm *DataMigrator) reportProgress() {
	dm.mu.RLock()
	activeCount := len(dm.activeTasks)
	if activeCount > 0 {
		dm.logger.Printf("迁移进度报告 - 活跃任务: %d", activeCount)
		for taskID, task := range dm.activeTasks {
			progress := task.Progress
			percentage := progress.GetProgressPercentage()
			dm.logger.Printf("任务 %s: %.1f%% (%d/%d)", taskID, percentage, progress.ProcessedKeys, progress.TotalKeys)
		}
	}
	dm.mu.RUnlock()
}

// GetTaskStatus 获取任务状态
func (dm *DataMigrator) GetTaskStatus(taskID string) (*MigrationTask, error) {
	dm.mu.RLock()
	defer dm.mu.RUnlock()

	task, exists := dm.tasks[taskID]
	if !exists {
		return nil, fmt.Errorf("任务不存在: %s", taskID)
	}

	taskCopy := *task
	if task.Progress != nil {
		progressCopy := *task.Progress
		taskCopy.Progress = &progressCopy
	}

	return &taskCopy, nil
}

// GetAllTasks 获取所有任务状态
func (dm *DataMigrator) GetAllTasks() []*MigrationTask {
	dm.mu.RLock()
	defer dm.mu.RUnlock()

	tasks := make([]*MigrationTask, 0, len(dm.tasks))
	for _, task := range dm.tasks {
		taskCopy := *task
		if task.Progress != nil {
			progressCopy := *task.Progress
			taskCopy.Progress = &progressCopy
		}
		tasks = append(tasks, &taskCopy)
	}

	return tasks
}

// GetStats 获取迁移统计信息
func (dm *DataMigrator) GetStats() *MigrationStats {
	dm.mu.RLock()
	defer dm.mu.RUnlock()

	var avgTime time.Duration
	if dm.stats.CompletedTasks > 0 {
		avgTime = dm.stats.TotalTimeTaken / time.Duration(dm.stats.CompletedTasks)
	}

	return &MigrationStats{
		TotalTasks:        dm.stats.TotalTasks,
		CompletedTasks:    dm.stats.CompletedTasks,
		FailedTasks:       dm.stats.FailedTasks,
		CancelledTasks:    dm.stats.CancelledTasks,
		TotalDataMigrated: dm.stats.TotalDataMigrated,
		TotalTimeTaken:    dm.stats.TotalTimeTaken,
		AverageTaskTime:   avgTime,
		LastMigrationTime: dm.stats.LastMigrationTime,
	}
}

// CancelTask 取消任务
func (dm *DataMigrator) CancelTask(taskID string) error {
	dm.mu.Lock()
	defer dm.mu.Unlock()

	task, exists := dm.tasks[taskID]
	if !exists {
		return fmt.Errorf("任务不存在: %s", taskID)
	}

	if task.Status == TaskStatusCompleted || task.Status == TaskStatusFailed {
		return fmt.Errorf("任务已完成，无法取消: %s", taskID)
	}

	task.Status = TaskStatusCancelled
	task.CompletedAt = time.Now()
	delete(dm.activeTasks, taskID)
	dm.stats.CancelledTasks++

	return nil
}

// ExportTasksJSON 导出任务信息为JSON
func (dm *DataMigrator) ExportTasksJSON() ([]byte, error) {
	tasks := dm.GetAllTasks()
	return json.MarshalIndent(tasks, "", "  ")
}
