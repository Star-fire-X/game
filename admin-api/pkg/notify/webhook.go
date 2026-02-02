package notify

import (
	"net/http"
	"time"

	"go.uber.org/zap"
)

// WebhookNotifier sends alerts to custom webhook endpoints
type WebhookNotifier struct {
	url        string
	headers    map[string]string
	client     *http.Client
	logger     *zap.Logger
	maxRetries int
}

// NewWebhookNotifier creates a new webhook notifier
func NewWebhookNotifier(url string, headers map[string]string, logger *zap.Logger) *WebhookNotifier {
	return &WebhookNotifier{
		url:     url,
		headers: headers,
		client: &http.Client{
			Timeout: 10 * time.Second,
		},
		logger:     logger,
		maxRetries: 3,
	}
}

func (w *WebhookNotifier) Name() string {
	return "webhook"
}
