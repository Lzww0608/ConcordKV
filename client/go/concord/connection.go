package concord

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"time"
)

// 连接类型
const (
	ConnectionTypeTCP  = "tcp"
	ConnectionTypeHTTP = "http"
)

// connection 实现与服务器的实际通信
type connection struct {
	endpoint    string
	connType    string
	tcpConn     net.Conn
	httpClient  *http.Client
	lastActive  time.Time
	isConnected bool
}

// 创建新的连接
func newConnection(endpoint string) *connection {
	// 默认使用HTTP连接
	connType := ConnectionTypeHTTP
	// 如果明确指定了TCP://前缀，则使用TCP连接
	if len(endpoint) > 6 && endpoint[:6] == "tcp://" {
		connType = ConnectionTypeTCP
		endpoint = endpoint[6:]
	}

	return &connection{
		endpoint: endpoint,
		connType: connType,
		httpClient: &http.Client{
			Timeout: 5 * time.Second,
		},
		lastActive: time.Now(),
	}
}

// 连接到服务器
func (c *connection) connect() error {
	if c.isConnected {
		return nil
	}

	if c.connType == ConnectionTypeTCP {
		conn, err := net.Dial("tcp", c.endpoint)
		if err != nil {
			return fmt.Errorf("无法建立TCP连接: %w", err)
		}
		c.tcpConn = conn
	}

	c.isConnected = true
	c.lastActive = time.Now()
	return nil
}

// 断开连接
func (c *connection) disconnect() error {
	if !c.isConnected {
		return nil
	}

	if c.connType == ConnectionTypeTCP && c.tcpConn != nil {
		if err := c.tcpConn.Close(); err != nil {
			return fmt.Errorf("关闭TCP连接失败: %w", err)
		}
		c.tcpConn = nil
	}

	c.isConnected = false
	return nil
}

// 发送请求并获取响应
func (c *connection) doRequest(req request) (*response, error) {
	if err := c.connect(); err != nil {
		return nil, err
	}

	c.lastActive = time.Now()

	if c.connType == ConnectionTypeTCP {
		return c.doTCPRequest(req)
	}
	return c.doHTTPRequest(req)
}

// 通过TCP发送请求
func (c *connection) doTCPRequest(req request) (*response, error) {
	// 将请求序列化为JSON
	reqData, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("序列化请求失败: %w", err)
	}

	// 发送请求
	if _, err := c.tcpConn.Write(append(reqData, '\n')); err != nil {
		return nil, fmt.Errorf("发送请求失败: %w", err)
	}

	// 读取响应
	buf := make([]byte, 4096)
	n, err := c.tcpConn.Read(buf)
	if err != nil {
		return nil, fmt.Errorf("读取响应失败: %w", err)
	}

	// 反序列化响应
	var resp response
	if err := json.Unmarshal(buf[:n], &resp); err != nil {
		return nil, fmt.Errorf("解析响应失败: %w", err)
	}

	return &resp, nil
}

// 通过HTTP发送请求
func (c *connection) doHTTPRequest(req request) (*response, error) {
	// 构建URL
	url := fmt.Sprintf("http://%s/kv", c.endpoint)

	// 根据操作类型添加路径
	switch req.Type {
	case "GET":
		url = fmt.Sprintf("%s/%s", url, req.Key)
	case "SET":
		url = fmt.Sprintf("%s/%s", url, req.Key)
	case "DELETE":
		url = fmt.Sprintf("%s/%s", url, req.Key)
	case "TXN":
		url = fmt.Sprintf("%s/txn", url)
	}

	// 将请求序列化为JSON
	reqData, err := json.Marshal(req)
	if err != nil {
		return nil, fmt.Errorf("序列化请求失败: %w", err)
	}

	// 创建HTTP请求
	var httpReq *http.Request
	var httpMethod string

	switch req.Type {
	case "GET":
		httpMethod = http.MethodGet
		httpReq, err = http.NewRequest(httpMethod, url, nil)
	case "SET":
		httpMethod = http.MethodPut
		httpReq, err = http.NewRequest(httpMethod, url, bytes.NewBuffer(reqData))
	case "DELETE":
		httpMethod = http.MethodDelete
		httpReq, err = http.NewRequest(httpMethod, url, nil)
	case "TXN":
		httpMethod = http.MethodPost
		httpReq, err = http.NewRequest(httpMethod, url, bytes.NewBuffer(reqData))
	}

	if err != nil {
		return nil, fmt.Errorf("创建HTTP请求失败: %w", err)
	}

	// 设置请求头
	httpReq.Header.Set("Content-Type", "application/json")

	// 发送请求
	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return nil, fmt.Errorf("HTTP请求失败: %w", err)
	}
	defer httpResp.Body.Close()

	// 读取响应体
	respBody, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return nil, fmt.Errorf("读取响应失败: %w", err)
	}

	// 检查状态码
	if httpResp.StatusCode != http.StatusOK {
		return &response{
			Success: false,
			Error:   fmt.Sprintf("服务器返回错误状态码: %d", httpResp.StatusCode),
		}, nil
	}

	// 反序列化响应
	var resp response
	if err := json.Unmarshal(respBody, &resp); err != nil {
		return nil, fmt.Errorf("解析响应失败: %w", err)
	}

	return &resp, nil
}

// 检查连接是否健康
func (c *connection) isHealthy() bool {
	if !c.isConnected {
		return false
	}

	// 如果是TCP连接且最近没有活动，尝试发送心跳
	if c.connType == ConnectionTypeTCP {
		healthReq := request{
			Type: "PING",
		}
		resp, err := c.doRequest(healthReq)
		if err != nil || !resp.Success {
			return false
		}
	}

	return true
}
