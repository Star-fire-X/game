package game

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// Client 游戏服务 HTTP 客户端
type Client struct {
	baseURL    string
	httpClient *http.Client
}

// NewClient 创建游戏服务客户端
func NewClient(host string, port, timeout int) *Client {
	return &Client{
		baseURL: fmt.Sprintf("http://%s:%d", host, port),
		httpClient: &http.Client{
			Timeout: time.Duration(timeout) * time.Second,
		},
	}
}

// APIResponse 通用响应结构
type APIResponse struct {
	Code    int             `json:"code"`
	Message string          `json:"message"`
	Data    json.RawMessage `json:"data"`
}
