package alert

import (
	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/pkg/notify"
	"go.uber.org/zap"
)

func (s *Service) saveHistory(config *model.AlertConfig, serviceName, message string) {
	history := &model.AlertHistory{
		RuleID:      &config.ID,
		RuleName:    config.RuleName,
		ServiceName: &serviceName,
		Severity:    config.Severity,
		Message:     message,
	}
	if err := s.historyRepo.Create(history); err != nil {
		s.logger.Error("failed to save alert history", zap.Error(err))
	}
}

func (s *Service) sendNotifications(config *model.AlertConfig, alert *notify.Alert) {
	if config.NotifyWechat && s.wechat != nil {
		go func() {
			result := s.wechat.Send(alert)
			if !result.Success {
				s.logger.Warn("wechat notify failed", zap.String("error", result.Error))
			}
		}()
	}

	if config.NotifyWebhook && config.WebhookURL != nil {
		go func() {
			webhook := notify.NewWebhookNotifier(*config.WebhookURL, nil, s.logger)
			result := webhook.Send(alert)
			if !result.Success {
				s.logger.Warn("webhook notify failed", zap.String("error", result.Error))
			}
		}()
	}
}
