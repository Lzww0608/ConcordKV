package sharding

import (
	"raftserver/raft"
	"testing"
	"time"
)

// MockStorageEngine 模拟存储引擎
type MockStorageEngine struct {
	data map[string][]byte
}

func NewMockStorageEngine() *MockStorageEngine {
	return &MockStorageEngine{
		data: make(map[string][]byte),
	}
}

func (m *MockStorageEngine) Get(key []byte) ([]byte, error) {
	value, exists := m.data[string(key)]
	if !exists {
		return nil, nil
	}
	return value, nil
}

func (m *MockStorageEngine) Put(key, value []byte) error {
	m.data[string(key)] = value
	return nil
}

func (m *MockStorageEngine) Delete(key []byte) error {
	delete(m.data, string(key))
	return nil
}

func (m *MockStorageEngine) Scan(startKey, endKey []byte, limit int) ([]KeyValue, error) {
	var result []KeyValue
	count := 0

	for k, v := range m.data {
		if count >= limit {
			break
		}
		if k >= string(startKey) && k < string(endKey) {
			result = append(result, KeyValue{
				Key:   []byte(k),
				Value: v,
			})
			count++
		}
	}

	return result, nil
}

func (m *MockStorageEngine) CreateBatchWriter() LSMBatchWriterInterface {
	return NewMockBatchWriter(m)
}

func (m *MockStorageEngine) GetStats() interface{} {
	return nil
}

// MockBatchWriter 模拟批量写入器
type MockBatchWriter struct {
	engine *MockStorageEngine
	batch  map[string][]byte
}

func NewMockBatchWriter(engine *MockStorageEngine) *MockBatchWriter {
	return &MockBatchWriter{
		engine: engine,
		batch:  make(map[string][]byte),
	}
}

func (m *MockBatchWriter) BatchPut(key, value []byte) error {
	m.batch[string(key)] = value
	return nil
}

func (m *MockBatchWriter) BatchDelete(key []byte) error {
	m.batch[string(key)] = nil
	return nil
}

func (m *MockBatchWriter) BatchCommit() error {
	for k, v := range m.batch {
		if v == nil {
			delete(m.engine.data, k)
		} else {
			m.engine.data[k] = v
		}
	}
	m.batch = make(map[string][]byte)
	return nil
}

func (m *MockBatchWriter) BatchClear() error {
	m.batch = make(map[string][]byte)
	return nil
}

func (m *MockBatchWriter) GetBatchStatus() (int, int64, error) {
	return len(m.batch), int64(len(m.batch) * 100), nil
}

func (m *MockBatchWriter) Destroy() {
	// 清理资源
}

// MockNetworkTransfer 模拟网络传输
type MockNetworkTransfer struct{}

func NewMockNetworkTransfer() *MockNetworkTransfer {
	return &MockNetworkTransfer{}
}

func (m *MockNetworkTransfer) SendBatch(targetNode raft.NodeID, batch []KeyValue) error {
	return nil
}

func (m *MockNetworkTransfer) ReceiveBatch(sourceNode raft.NodeID) ([]KeyValue, error) {
	return nil, nil
}

func (m *MockNetworkTransfer) EstablishConnection(node raft.NodeID) error {
	return nil
}

func (m *MockNetworkTransfer) CloseConnection(node raft.NodeID) error {
	return nil
}

// TestDataMigratorCreation 测试数据迁移器创建
func TestDataMigratorCreation(t *testing.T) {
	config := DefaultDataMigratorConfig()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()

	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)

	if migrator == nil {
		t.Fatal("数据迁移器创建失败")
	}

	if migrator.config.BatchSize != 1000 {
		t.Errorf("批量大小配置不正确，期望: 1000, 实际: %d", migrator.config.BatchSize)
	}

	if migrator.config.ConcurrentMigrations != 2 {
		t.Errorf("并发数配置不正确，期望: 2, 实际: %d", migrator.config.ConcurrentMigrations)
	}
}

// TestMigrationTaskValidation 测试迁移任务验证
func TestMigrationTaskValidation(t *testing.T) {
	config := DefaultDataMigratorConfig()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()
	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)

	// 测试有效任务
	validTask := &MigrationTask{
		ShardID:    "shard-1",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "key-000",
			EndKey:   "key-999",
		},
	}

	err := migrator.validateTask(validTask)
	if err != nil {
		t.Errorf("有效任务验证失败: %v", err)
	}

	// 测试无效任务 - 空分片ID
	invalidTask1 := &MigrationTask{
		SourceNode: "node-1",
		TargetNode: "node-2",
	}

	err = migrator.validateTask(invalidTask1)
	if err == nil {
		t.Error("应该检测到空分片ID错误")
	}

	// 测试无效任务 - 相同源和目标节点
	invalidTask2 := &MigrationTask{
		ShardID:    "shard-1",
		SourceNode: "node-1",
		TargetNode: "node-1",
	}

	err = migrator.validateTask(invalidTask2)
	if err == nil {
		t.Error("应该检测到相同源和目标节点错误")
	}
}

// TestMigrationTaskSubmission 测试迁移任务提交
func TestMigrationTaskSubmission(t *testing.T) {
	config := DefaultDataMigratorConfig()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()
	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)

	// 启动迁移器
	err := migrator.Start()
	if err != nil {
		t.Fatalf("启动迁移器失败: %v", err)
	}
	defer migrator.Stop()

	// 提交任务
	task := &MigrationTask{
		ShardID:    "shard-1",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "key-000",
			EndKey:   "key-999",
		},
	}

	err = migrator.SubmitMigrationTask(task)
	if err != nil {
		t.Errorf("提交迁移任务失败: %v", err)
	}

	// 验证任务是否被记录
	if task.ID == "" {
		t.Error("任务ID应该被自动生成")
	}

	if task.Status != TaskStatusPending {
		t.Errorf("任务状态应该是Pending，实际: %s", task.Status)
	}

	// 等待任务执行
	time.Sleep(100 * time.Millisecond)

	// 检查任务状态
	taskStatus, err := migrator.GetTaskStatus(task.ID)
	if err != nil {
		t.Errorf("获取任务状态失败: %v", err)
	}

	if taskStatus.Status == TaskStatusPending {
		t.Error("任务应该已经开始执行")
	}
}

// TestMigrationProgress 测试迁移进度
func TestMigrationProgress(t *testing.T) {
	progress := &MigrationProgress{
		TotalKeys:        1000,
		ProcessedKeys:    250,
		TransferredBytes: 25000,
		StartTime:        time.Now().Add(-10 * time.Second),
	}

	percentage := progress.GetProgressPercentage()
	if percentage != 25.0 {
		t.Errorf("进度百分比计算错误，期望: 25.0, 实际: %.1f", percentage)
	}

	rate := progress.GetTransferRate()
	if rate <= 0 {
		t.Errorf("传输速率应该大于0，实际: %.2f", rate)
	}
}

// TestMigrationStats 测试迁移统计
func TestMigrationStats(t *testing.T) {
	config := DefaultDataMigratorConfig()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()
	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)

	// 获取初始统计信息
	stats := migrator.GetStats()
	if stats.TotalTasks != 0 {
		t.Errorf("初始任务数应该为0，实际: %d", stats.TotalTasks)
	}

	// 启动迁移器并提交任务
	migrator.Start()
	defer migrator.Stop()

	task := &MigrationTask{
		ShardID:    "shard-1",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "key-000",
			EndKey:   "key-999",
		},
	}

	migrator.SubmitMigrationTask(task)

	// 等待统计更新
	time.Sleep(50 * time.Millisecond)

	stats = migrator.GetStats()
	if stats.TotalTasks != 1 {
		t.Errorf("总任务数应该为1，实际: %d", stats.TotalTasks)
	}
}

// TestTaskCancellation 测试任务取消
func TestTaskCancellation(t *testing.T) {
	config := DefaultDataMigratorConfig()
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()
	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)

	migrator.Start()
	defer migrator.Stop()

	task := &MigrationTask{
		ShardID:    "shard-1",
		SourceNode: "node-1",
		TargetNode: "node-2",
		KeyRange: KeyRange{
			StartKey: "key-000",
			EndKey:   "key-999",
		},
	}

	err := migrator.SubmitMigrationTask(task)
	if err != nil {
		t.Fatalf("提交任务失败: %v", err)
	}

	// 取消任务
	err = migrator.CancelTask(task.ID)
	if err != nil {
		t.Errorf("取消任务失败: %v", err)
	}

	// 验证任务状态
	taskStatus, err := migrator.GetTaskStatus(task.ID)
	if err != nil {
		t.Errorf("获取任务状态失败: %v", err)
	}

	if taskStatus.Status != TaskStatusCancelled {
		t.Errorf("任务状态应该是Cancelled，实际: %s", taskStatus.Status)
	}
}

// TestDataMigratorStartStop 测试启动和停止
func TestDataMigratorStartStop(t *testing.T) {
	config := DefaultDataMigratorConfig()
	config.ProgressReportInterval = 100 * time.Millisecond
	storageEngine := NewMockStorageEngine()
	networkTransfer := NewMockNetworkTransfer()
	migrator := NewDataMigrator(config, storageEngine, networkTransfer, nil)

	err := migrator.Start()
	if err != nil {
		t.Fatalf("启动迁移器失败: %v", err)
	}

	time.Sleep(50 * time.Millisecond)

	err = migrator.Stop()
	if err != nil {
		t.Errorf("停止迁移器失败: %v", err)
	}
}
