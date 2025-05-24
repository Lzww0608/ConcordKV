package transport

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"sync"
	"time"

	"raftserver/raft"
)

// HTTPTransport HTTP传输层实现
type HTTPTransport struct {
	mu      sync.RWMutex
	addr    string
	server  *http.Server
	client  *http.Client
	peers   map[raft.NodeID]string
	handler TransportHandler
	running bool
}

// TransportHandler 传输处理器接口
type TransportHandler interface {
	HandleVoteRequest(req *raft.VoteRequest) *raft.VoteResponse
	HandleAppendEntries(req *raft.AppendEntriesRequest) *raft.AppendEntriesResponse
	HandleInstallSnapshot(req *raft.InstallSnapshotRequest) *raft.InstallSnapshotResponse
}

// NewHTTPTransport 创建新的HTTP传输层
func NewHTTPTransport(addr string, peers map[raft.NodeID]string) *HTTPTransport {
	return &HTTPTransport{
		addr:  addr,
		peers: peers,
		client: &http.Client{
			Timeout: time.Second * 5,
		},
	}
}

// SetHandler 设置传输处理器
func (t *HTTPTransport) SetHandler(handler TransportHandler) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.handler = handler
}

// Start 启动传输层
func (t *HTTPTransport) Start() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.running {
		return fmt.Errorf("传输层已经启动")
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/vote", t.handleVoteRequest)
	mux.HandleFunc("/append", t.handleAppendEntries)
	mux.HandleFunc("/snapshot", t.handleInstallSnapshot)
	mux.HandleFunc("/health", t.handleHealth)

	t.server = &http.Server{
		Addr:    t.addr,
		Handler: mux,
	}

	listener, err := net.Listen("tcp", t.addr)
	if err != nil {
		return fmt.Errorf("监听地址失败: %w", err)
	}

	go func() {
		if err := t.server.Serve(listener); err != nil && err != http.ErrServerClosed {
			fmt.Printf("HTTP服务器错误: %v\n", err)
		}
	}()

	t.running = true
	fmt.Printf("HTTP传输层启动在 %s\n", t.addr)

	return nil
}

// Stop 停止传输层
func (t *HTTPTransport) Stop() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if !t.running {
		return nil
	}

	ctx, cancel := context.WithTimeout(context.Background(), time.Second*5)
	defer cancel()

	if err := t.server.Shutdown(ctx); err != nil {
		return fmt.Errorf("关闭HTTP服务器失败: %w", err)
	}

	t.running = false
	fmt.Printf("HTTP传输层已停止\n")

	return nil
}

// LocalAddr 获取本地地址
func (t *HTTPTransport) LocalAddr() string {
	return t.addr
}

// SendVoteRequest 发送投票请求
func (t *HTTPTransport) SendVoteRequest(ctx context.Context, target raft.NodeID, req *raft.VoteRequest) (*raft.VoteResponse, error) {
	t.mu.RLock()
	addr, exists := t.peers[target]
	t.mu.RUnlock()

	if !exists {
		return nil, fmt.Errorf("未找到节点 %s 的地址", target)
	}

	url := fmt.Sprintf("http://%s/vote", addr)
	resp := &raft.VoteResponse{}
	err := t.sendRequest(ctx, url, req, resp)
	return resp, err
}

// SendAppendEntries 发送追加日志请求
func (t *HTTPTransport) SendAppendEntries(ctx context.Context, target raft.NodeID, req *raft.AppendEntriesRequest) (*raft.AppendEntriesResponse, error) {
	t.mu.RLock()
	addr, exists := t.peers[target]
	t.mu.RUnlock()

	if !exists {
		return nil, fmt.Errorf("未找到节点 %s 的地址", target)
	}

	url := fmt.Sprintf("http://%s/append", addr)
	resp := &raft.AppendEntriesResponse{}
	err := t.sendRequest(ctx, url, req, resp)
	return resp, err
}

// SendInstallSnapshot 发送安装快照请求
func (t *HTTPTransport) SendInstallSnapshot(ctx context.Context, target raft.NodeID, req *raft.InstallSnapshotRequest) (*raft.InstallSnapshotResponse, error) {
	t.mu.RLock()
	addr, exists := t.peers[target]
	t.mu.RUnlock()

	if !exists {
		return nil, fmt.Errorf("未找到节点 %s 的地址", target)
	}

	url := fmt.Sprintf("http://%s/snapshot", addr)
	resp := &raft.InstallSnapshotResponse{}
	err := t.sendRequest(ctx, url, req, resp)
	return resp, err
}

// sendRequest 发送HTTP请求的通用方法
func (t *HTTPTransport) sendRequest(ctx context.Context, url string, reqData interface{}, respData interface{}) error {
	// 序列化请求
	reqJSON, err := json.Marshal(reqData)
	if err != nil {
		return fmt.Errorf("序列化请求失败: %w", err)
	}

	// 创建HTTP请求
	httpReq, err := http.NewRequestWithContext(ctx, "POST", url, bytes.NewBuffer(reqJSON))
	if err != nil {
		return fmt.Errorf("创建HTTP请求失败: %w", err)
	}

	httpReq.Header.Set("Content-Type", "application/json")

	// 发送请求
	resp, err := t.client.Do(httpReq)
	if err != nil {
		return fmt.Errorf("发送HTTP请求失败: %w", err)
	}
	defer resp.Body.Close()

	// 检查状态码
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("HTTP请求失败，状态码: %d", resp.StatusCode)
	}

	// 读取响应
	respJSON, err := io.ReadAll(resp.Body)
	if err != nil {
		return fmt.Errorf("读取响应失败: %w", err)
	}

	// 反序列化响应
	if err := json.Unmarshal(respJSON, respData); err != nil {
		return fmt.Errorf("反序列化响应失败: %w", err)
	}

	return nil
}

// handleVoteRequest 处理投票请求
func (t *HTTPTransport) handleVoteRequest(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "只支持POST方法", http.StatusMethodNotAllowed)
		return
	}

	var req raft.VoteRequest
	if err := t.decodeRequest(r, &req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	t.mu.RLock()
	handler := t.handler
	t.mu.RUnlock()

	if handler == nil {
		http.Error(w, "处理器未设置", http.StatusInternalServerError)
		return
	}

	resp := handler.HandleVoteRequest(&req)
	t.encodeResponse(w, resp)
}

// handleAppendEntries 处理追加日志请求
func (t *HTTPTransport) handleAppendEntries(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "只支持POST方法", http.StatusMethodNotAllowed)
		return
	}

	var req raft.AppendEntriesRequest
	if err := t.decodeRequest(r, &req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	t.mu.RLock()
	handler := t.handler
	t.mu.RUnlock()

	if handler == nil {
		http.Error(w, "处理器未设置", http.StatusInternalServerError)
		return
	}

	resp := handler.HandleAppendEntries(&req)
	t.encodeResponse(w, resp)
}

// handleInstallSnapshot 处理安装快照请求
func (t *HTTPTransport) handleInstallSnapshot(w http.ResponseWriter, r *http.Request) {
	if r.Method != "POST" {
		http.Error(w, "只支持POST方法", http.StatusMethodNotAllowed)
		return
	}

	var req raft.InstallSnapshotRequest
	if err := t.decodeRequest(r, &req); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	t.mu.RLock()
	handler := t.handler
	t.mu.RUnlock()

	if handler == nil {
		http.Error(w, "处理器未设置", http.StatusInternalServerError)
		return
	}

	resp := handler.HandleInstallSnapshot(&req)
	t.encodeResponse(w, resp)
}

// handleHealth 处理健康检查请求
func (t *HTTPTransport) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status": "ok",
		"time":   time.Now().Format(time.RFC3339),
	})
}

// decodeRequest 解码HTTP请求
func (t *HTTPTransport) decodeRequest(r *http.Request, v interface{}) error {
	body, err := io.ReadAll(r.Body)
	if err != nil {
		return fmt.Errorf("读取请求体失败: %w", err)
	}

	if err := json.Unmarshal(body, v); err != nil {
		return fmt.Errorf("解析JSON失败: %w", err)
	}

	return nil
}

// encodeResponse 编码HTTP响应
func (t *HTTPTransport) encodeResponse(w http.ResponseWriter, v interface{}) {
	w.Header().Set("Content-Type", "application/json")

	if err := json.NewEncoder(w).Encode(v); err != nil {
		http.Error(w, "编码响应失败", http.StatusInternalServerError)
		return
	}
}
