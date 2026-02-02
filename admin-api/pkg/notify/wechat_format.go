package notify

import "fmt"

// formatMarkdown formats alert as WeChat markdown
func (w *WeChatNotifier) formatMarkdown(alert *Alert) string {
	var severityColor string
	var severityText string

	switch alert.Severity {
	case SeverityCritical:
		severityColor = "warning"
		severityText = "严重"
	case SeverityWarning:
		severityColor = "comment"
		severityText = "警告"
	default:
		severityColor = "info"
		severityText = "信息"
	}

	content := fmt.Sprintf(`### <font color="%s">%s告警</font>
> **规则**: %s
> **服务**: %s
> **时间**: %s

**详情**: %s`,
		severityColor,
		severityText,
		alert.RuleName,
		alert.ServiceName,
		alert.Timestamp,
		alert.Message,
	)

	if alert.Details != "" {
		content += fmt.Sprintf("\n\n```\n%s\n```", alert.Details)
	}

	return content
}
