package alert

import (
	"context"
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/pkg/notify"
	"go.uber.org/zap"
)

// TriggerAlert processes and sends an alert
func (s *Service) TriggerAlert(ctx context.Context, ruleID int, serviceName, message string) error {
	config, err := s.configRepo.GetByID(ruleID)
	if err != nil || !config.IsEnabled {
		return err
	}

	// Check deduplication
	if s.dedup.IsDuplicate(ctx, ruleID, serviceName, message, config.DedupWindowSeconds) {
		s.logger.Debug("alert deduplicated", zap.Int("rule_id", ruleID))
		return nil
	}

	// Mark as sent for dedup
	s.dedup.MarkSent(ctx, ruleID, serviceName, message, config.DedupWindowSeconds)

	// Create alert
	alert := &notify.Alert{
		RuleName:    config.RuleName,
		ServiceName: serviceName,
		Severity:    notify.Severity(config.Severity),
		Message:     message,
		Timestamp:   time.Now().Format(time.RFC3339),
	}

	// Save to history
	s.saveHistory(config, serviceName, message)

	// Send notifications
	s.sendNotifications(config, alert)

	return nil
}
