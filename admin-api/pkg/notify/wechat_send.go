package notify

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// Send sends an alert to WeChat Work
func (w *WeChatNotifier) Send(alert *Alert) *NotifyResult {
	result := &NotifyResult{
		Channel: w.Name(),
	}

	content := w.formatMarkdown(alert)
	msg := &wechatMessage{
		MsgType: "markdown",
		Markdown: &markdownContent{
			Content: content,
		},
	}

	body, err := json.Marshal(msg)
	if err != nil {
		result.Error = fmt.Sprintf("marshal error: %v", err)
		return result
	}

	var lastErr error
	for i := 0; i < w.maxRetries; i++ {
		if i > 0 {
			time.Sleep(time.Duration(i) * time.Second)
		}

		resp, err := w.client.Post(w.webhookURL, "application/json", bytes.NewReader(body))
		if err != nil {
			lastErr = err
			w.logger.Warn("wechat notify failed, retrying",
				zap.Int("attempt", i+1),
				zap.Error(err))
			continue
		}
		defer resp.Body.Close()

		if resp.StatusCode == http.StatusOK {
			result.Success = true
			w.logger.Info("wechat notify sent",
				zap.String("rule", alert.RuleName),
				zap.String("service", alert.ServiceName))
			return result
		}

		lastErr = fmt.Errorf("status code: %d", resp.StatusCode)
		w.logger.Warn("wechat notify failed, retrying",
			zap.Int("attempt", i+1),
			zap.Int("status", resp.StatusCode))
	}

	result.Error = fmt.Sprintf("max retries exceeded: %v", lastErr)
	return result
}
