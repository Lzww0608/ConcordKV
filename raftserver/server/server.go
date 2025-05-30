/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV Raft consensus server - server.go
 */
package server

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"

	"raftserver/config"
	"raftserver/raft"
	"raftserver/statemachine"
	"raftserver/storage"
	"raftserver/transport"
)

// Server ConcordKV Raft服务器
type Server struct {
	mu           sync.RWMutex
	config       *ServerConfig
	raftNode     *raft.Node
	transport    *transport.HTTPTransport
	storage      *storage.MemoryStorage
	stateMachine *statemachine.KVStateMachine
	apiServer    *http.Server
	logger       *log.Logger
	running      bool
}

// ServerConfig 服务器配置
type ServerConfig struct {
	NodeID            raft.NodeID            `yaml:"nodeId"`
	ListenAddr        string                 `yaml:"listenAddr"`
	APIAddr           string                 `yaml:"apiAddr"`
	ElectionTimeout   time.Duration          `yaml:"electionTimeout"`
	HeartbeatInterval time.Duration          `yaml:"heartbeatInterval"`
	MaxLogEntries     int                    `yaml:"maxLogEntries"`
	SnapshotThreshold int                    `yaml:"snapshotThreshold"`
	Peers             map[raft.NodeID]string `yaml:"peers"`
}

// NewServer 创建新的服务器
func NewServer(configPath string) (*Server, error) {
	// 加载配置
	cfg, err := config.Load(configPath)
	if err != nil {
		return nil, fmt.Errorf("加载配置失败: %w", err)
	}

	serverConfig := &ServerConfig{
		NodeID:            raft.NodeID(cfg.GetString("server.nodeId", "node1")),
		ListenAddr:        cfg.GetString("server.listenAddr", ":8080"),
		APIAddr:           cfg.GetString("server.apiAddr", ":8081"),
		ElectionTimeout:   time.Duration(cfg.GetInt("server.electionTimeout", 5000)) * time.Millisecond,
		HeartbeatInterval: time.Duration(cfg.GetInt("server.heartbeatInterval", 1000)) * time.Millisecond,
		MaxLogEntries:     cfg.GetInt("server.maxLogEntries", 100),
		SnapshotThreshold: cfg.GetInt("server.snapshotThreshold", 1000),
		Peers:             make(map[raft.NodeID]string),
	}

	// 加载节点列表
	peersList := cfg.GetStringSlice("server.peers", []string{})
	for _, peer := range peersList {
		// 解析节点信息，格式：nodeId:address
		// 这里简化处理，实际应该更严格
		if len(peer) > 0 {
			serverConfig.Peers[raft.NodeID(peer)] = peer
		}
	}

	return NewServerWithConfig(serverConfig)
}

// NewServerWithConfig 使用配置创建服务器
func NewServerWithConfig(config *ServerConfig) (*Server, error) {
	logger := log.New(log.Writer(), fmt.Sprintf("[server-%s] ", config.NodeID), log.LstdFlags)

	// 创建存储
	storage := storage.NewMemoryStorage()

	// 创建状态机
	stateMachine := statemachine.NewKVStateMachine()

	// 创建传输层
	transport := transport.NewHTTPTransport(config.ListenAddr, config.Peers)

	// 创建Raft配置
	raftConfig := &raft.Config{
		NodeID:            config.NodeID,
		ElectionTimeout:   config.ElectionTimeout,
		HeartbeatInterval: config.HeartbeatInterval,
		MaxLogEntries:     config.MaxLogEntries,
		SnapshotThreshold: config.SnapshotThreshold,
		Servers:           make([]raft.Server, 0),
	}

	// 添加服务器列表
	for nodeID, addr := range config.Peers {
		raftConfig.Servers = append(raftConfig.Servers, raft.Server{
			ID:      nodeID,
			Address: addr,
		})
	}

	// 如果没有配置其他节点，添加自己作为单节点集群
	if len(raftConfig.Servers) == 0 {
		raftConfig.Servers = append(raftConfig.Servers, raft.Server{
			ID:      config.NodeID,
			Address: config.ListenAddr,
		})
	}

	// 创建Raft节点
	raftNode, err := raft.NewNode(raftConfig, transport, storage, stateMachine)
	if err != nil {
		return nil, fmt.Errorf("创建Raft节点失败: %w", err)
	}

	server := &Server{
		config:       config,
		raftNode:     raftNode,
		transport:    transport,
		storage:      storage,
		stateMachine: stateMachine,
		logger:       logger,
	}

	// 设置传输处理器
	transport.SetHandler(server)

	return server, nil
}

// Start 启动服务器
func (s *Server) Start() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.running {
		return fmt.Errorf("服务器已经启动")
	}

	s.logger.Printf("启动ConcordKV Raft服务器，节点ID: %s", s.config.NodeID)

	// 启动Raft节点
	if err := s.raftNode.Start(); err != nil {
		return fmt.Errorf("启动Raft节点失败: %w", err)
	}

	// 启动API服务器
	if err := s.startAPIServer(); err != nil {
		s.raftNode.Stop()
		return fmt.Errorf("启动API服务器失败: %w", err)
	}

	s.running = true
	s.logger.Printf("服务器启动成功")

	return nil
}

// Stop 停止服务器
func (s *Server) Stop() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if !s.running {
		return nil
	}

	s.logger.Printf("停止ConcordKV Raft服务器")

	// 停止API服务器
	if s.apiServer != nil {
		s.apiServer.Close()
	}

	// 停止Raft节点
	if err := s.raftNode.Stop(); err != nil {
		s.logger.Printf("停止Raft节点失败: %v", err)
	}

	s.running = false
	s.logger.Printf("服务器已停止")

	return nil
}

// startAPIServer 启动API服务器
func (s *Server) startAPIServer() error {
	mux := http.NewServeMux()

	// 客户端API
	mux.HandleFunc("/api/get", s.handleGet)
	mux.HandleFunc("/api/set", s.handleSet)
	mux.HandleFunc("/api/delete", s.handleDelete)
	mux.HandleFunc("/api/keys", s.handleKeys)

	// 管理API
	mux.HandleFunc("/api/status", s.handleStatus)
	mux.HandleFunc("/api/metrics", s.handleMetrics)
	mux.HandleFunc("/api/logs", s.handleLogs)

	// 集群管理API
	mux.HandleFunc("/api/cluster/add", s.handleAddServer)
	mux.HandleFunc("/api/cluster/remove", s.handleRemoveServer)
	mux.HandleFunc("/api/cluster/config", s.handleGetConfiguration)

	s.apiServer = &http.Server{
		Addr:    s.config.APIAddr,
		Handler: mux,
	}

	go func() {
		s.logger.Printf("API服务器开始监听 %s", s.config.APIAddr)
		if err := s.apiServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			s.logger.Printf("API服务器错误: %v", err)
		}
	}()

	s.logger.Printf("API服务器启动在 %s", s.config.APIAddr)

	// 等待一小段时间确保服务器启动
	time.Sleep(time.Millisecond * 100)

	return nil
}

// Transport处理器实现

// HandleVoteRequest 处理投票请求
func (s *Server) HandleVoteRequest(req *raft.VoteRequest) *raft.VoteResponse {
	return s.raftNode.HandleVoteRequest(req)
}

// HandleAppendEntries 处理追加日志请求
func (s *Server) HandleAppendEntries(req *raft.AppendEntriesRequest) *raft.AppendEntriesResponse {
	return s.raftNode.HandleAppendEntries(req)
}

// HandleInstallSnapshot 处理安装快照请求
func (s *Server) HandleInstallSnapshot(req *raft.InstallSnapshotRequest) *raft.InstallSnapshotResponse {
	return s.raftNode.HandleInstallSnapshot(req)
}

// API处理器

// handleGet 处理GET请求
func (s *Server) handleGet(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	key := r.URL.Query().Get("key")
	if key == "" {
		http.Error(w, "缺少key参数", http.StatusBadRequest)
		return
	}

	value, exists := s.stateMachine.Get(key)

	response := map[string]interface{}{
		"key":    key,
		"exists": exists,
	}

	if exists {
		response["value"] = value
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleSet 处理SET请求
func (s *Server) handleSet(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "只支持POST方法", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		Key   string      `json:"key"`
		Value interface{} `json:"value"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "解析请求失败", http.StatusBadRequest)
		return
	}

	if req.Key == "" {
		http.Error(w, "key不能为空", http.StatusBadRequest)
		return
	}

	// 创建命令
	cmdData, err := statemachine.CreateSetCommand(req.Key, req.Value)
	if err != nil {
		http.Error(w, "创建命令失败", http.StatusInternalServerError)
		return
	}

	// 提议到Raft
	if err := s.raftNode.Propose(cmdData); err != nil {
		if err == raft.ErrNotLeader {
			leader := s.raftNode.GetLeader()
			response := map[string]interface{}{
				"success": false,
				"error":   "不是领导者",
				"leader":  leader,
			}
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(response)
			return
		}

		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	response := map[string]interface{}{
		"success": true,
		"key":     req.Key,
		"value":   req.Value,
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleDelete 处理DELETE请求
func (s *Server) handleDelete(w http.ResponseWriter, r *http.Request) {
	if r.Method != "DELETE" {
		http.Error(w, "只支持DELETE方法", http.StatusMethodNotAllowed)
		return
	}

	key := r.URL.Query().Get("key")
	if key == "" {
		http.Error(w, "缺少key参数", http.StatusBadRequest)
		return
	}

	// 创建命令
	cmdData, err := statemachine.CreateDeleteCommand(key)
	if err != nil {
		http.Error(w, "创建命令失败", http.StatusInternalServerError)
		return
	}

	// 提议到Raft
	if err := s.raftNode.Propose(cmdData); err != nil {
		if err == raft.ErrNotLeader {
			leader := s.raftNode.GetLeader()
			response := map[string]interface{}{
				"success": false,
				"error":   "不是领导者",
				"leader":  leader,
			}
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(response)
			return
		}

		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	response := map[string]interface{}{
		"success": true,
		"key":     key,
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleKeys 处理获取所有键的请求
func (s *Server) handleKeys(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	keys := s.stateMachine.Keys()

	response := map[string]interface{}{
		"keys":  keys,
		"count": len(keys),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleStatus 处理状态查询请求
func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	s.logger.Printf("处理状态查询请求")

	s.logger.Printf("获取Raft指标...")
	metrics := s.raftNode.GetMetrics()
	s.logger.Printf("获取Raft指标完成")

	s.logger.Printf("检查是否为领导者...")
	isLeader := s.raftNode.IsLeader()
	s.logger.Printf("检查是否为领导者完成: %v", isLeader)

	s.logger.Printf("获取存储大小...")
	storageSize := s.stateMachine.Size()
	s.logger.Printf("获取存储大小完成: %d", storageSize)

	response := map[string]interface{}{
		"nodeId":       s.config.NodeID,
		"state":        metrics.State.String(),
		"term":         metrics.CurrentTerm,
		"leader":       metrics.Leader,
		"lastLogIndex": metrics.LastLogIndex,
		"commitIndex":  metrics.CommitIndex,
		"lastApplied":  metrics.LastApplied,
		"isLeader":     isLeader,
		"storageSize":  storageSize,
	}

	s.logger.Printf("发送响应...")
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
	s.logger.Printf("状态查询请求处理完成")
}

// handleMetrics 处理指标查询请求
func (s *Server) handleMetrics(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	metrics := s.raftNode.GetMetrics()
	storageStats := s.storage.GetLogStats()

	response := map[string]interface{}{
		"raft":    metrics,
		"storage": storageStats,
		"data":    s.stateMachine.GetAll(),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleLogs 处理日志查询请求（用于调试）
func (s *Server) handleLogs(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	logs := s.storage.DebugLogs()

	w.Header().Set("Content-Type", "application/json")
	w.Write([]byte(logs))
}

// handleAddServer 处理添加服务器请求
func (s *Server) handleAddServer(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "只支持POST方法", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		ID      string `json:"id"`
		Address string `json:"address"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "解析请求失败", http.StatusBadRequest)
		return
	}

	if req.ID == "" || req.Address == "" {
		http.Error(w, "id和address不能为空", http.StatusBadRequest)
		return
	}

	server := raft.Server{
		ID:      raft.NodeID(req.ID),
		Address: req.Address,
	}

	if err := s.raftNode.AddServer(server); err != nil {
		if err == raft.ErrNotLeader {
			leader := s.raftNode.GetLeader()
			response := map[string]interface{}{
				"success": false,
				"error":   "不是领导者",
				"leader":  leader,
			}
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(response)
			return
		}

		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	response := map[string]interface{}{
		"success": true,
		"message": fmt.Sprintf("服务器 %s 添加成功", req.ID),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleRemoveServer 处理移除服务器请求
func (s *Server) handleRemoveServer(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "只支持POST方法", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		ID string `json:"id"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "解析请求失败", http.StatusBadRequest)
		return
	}

	if req.ID == "" {
		http.Error(w, "id不能为空", http.StatusBadRequest)
		return
	}

	if err := s.raftNode.RemoveServer(raft.NodeID(req.ID)); err != nil {
		if err == raft.ErrNotLeader {
			leader := s.raftNode.GetLeader()
			response := map[string]interface{}{
				"success": false,
				"error":   "不是领导者",
				"leader":  leader,
			}
			w.Header().Set("Content-Type", "application/json")
			json.NewEncoder(w).Encode(response)
			return
		}

		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	response := map[string]interface{}{
		"success": true,
		"message": fmt.Sprintf("服务器 %s 移除成功", req.ID),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// handleGetConfiguration 处理获取集群配置请求
func (s *Server) handleGetConfiguration(w http.ResponseWriter, r *http.Request) {
	if r.Method != "GET" {
		http.Error(w, "只支持GET方法", http.StatusMethodNotAllowed)
		return
	}

	config := s.raftNode.GetConfiguration()

	response := map[string]interface{}{
		"configuration": config,
		"changing":      s.raftNode.IsConfigurationChanging(),
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(response)
}

// GetRaftNode 获取Raft节点（用于测试）
func (s *Server) GetRaftNode() *raft.Node {
	return s.raftNode
}
