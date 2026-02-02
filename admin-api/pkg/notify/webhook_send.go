package notify

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

// Send sends an alert to the webhook endpoint
func (w *WebhookNotifier) Send(alert *Alert) *NotifyResult {
	result := &NotifyResult{
		Channel: w.Name(),
	}

	body, err := json.Marshal(alert)
	if err != nil {
		result.Error = fmt.Sprintf("marshal error: %v", err)
		return result
	}

	var lastErr error
	for i := 0; i < w.maxRetries; i++ {
		if i > 0 {
			time.Sleep(time.Duration(i) * time.Second)
		}

		req, err := http.NewRequest(http.MethodPost, w.url, bytes.NewReader(body))
		if err != nil {
			lastErr = err
			continue
		}

		req.Header.Set("Content-Type", "application/json")
		for k, v := range w.headers {
			req.Header.Set(k, v)
		}

		resp, err := w.client.Do(req)
		if err != nil {
			lastErr = err
			w.logger.Warn("webhook notify failed",
				zap.Int("attempt", i+1),
				zap.Error(err))
			continue
		}
		resp.Body.Close()

		if resp.StatusCode >= 200 && resp.StatusCode < 300 {
			result.Success = true
			w.logger.Info("webhook notify sent",
				zap.String("url", w.url),
				zap.String("rule", alert.RuleName))
			return result
		}

		lastErr = fmt.Errorf("status: %d", resp.StatusCode)
	}

	result.Error = fmt.Sprintf("max retries: %v", lastErr)
	return result
}
