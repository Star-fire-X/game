package notify

// Severity levels for alerts
type Severity string

const (
	SeverityCritical Severity = "critical"
	SeverityWarning  Severity = "warning"
	SeverityInfo     Severity = "info"
)

// Alert represents an alert notification
type Alert struct {
	RuleName    string   `json:"rule_name"`
	ServiceName string   `json:"service_name"`
	Severity    Severity `json:"severity"`
	Message     string   `json:"message"`
	Details     string   `json:"details,omitempty"`
	Timestamp   string   `json:"timestamp"`
}

// NotifyResult represents the result of a notification attempt
type NotifyResult struct {
	Success bool   `json:"success"`
	Channel string `json:"channel"`
	Error   string `json:"error,omitempty"`
}

// Notifier interface for different notification channels
type Notifier interface {
	Send(alert *Alert) *NotifyResult
	Name() string
}
