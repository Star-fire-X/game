package notify

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"go.uber.org/zap"
)

// WeChatNotifier sends alerts to WeChat Work robot
type WeChatNotifier struct {
	webhookURL string
	client     *http.Client
	logger     *zap.Logger
	maxRetries int
}

// wechatMessage represents WeChat Work message format
type wechatMessage struct {
	MsgType  string          `json:"msgtype"`
	Markdown *markdownContent `json:"markdown,omitempty"`
}

type markdownContent struct {
	Content string `json:"content"`
}

// NewWeChatNotifier creates a new WeChat notifier
func NewWeChatNotifier(webhookURL string, logger *zap.Logger) *WeChatNotifier {
	return &WeChatNotifier{
		webhookURL: webhookURL,
		client: &http.Client{
			Timeout: 10 * time.Second,
		},
		logger:     logger,
		maxRetries: 3,
	}
}

func (w *WeChatNotifier) Name() string {
	return "wechat"
}
