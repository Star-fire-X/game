package alert

import (
	"context"
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/notify"
	"go.uber.org/zap"
)

// Service handles alert processing
type Service struct {
	configRepo  *repository.AlertConfigRepo
	historyRepo *repository.AlertHistoryRepo
	dedup       *Deduplicator
	wechat      *notify.WeChatNotifier
	logger      *zap.Logger
}

// NewService creates a new alert service
func NewService(
	configRepo *repository.AlertConfigRepo,
	historyRepo *repository.AlertHistoryRepo,
	dedup *Deduplicator,
	wechatURL string,
	logger *zap.Logger,
) *Service {
	return &Service{
		configRepo:  configRepo,
		historyRepo: historyRepo,
		dedup:       dedup,
		wechat:      notify.NewWeChatNotifier(wechatURL, logger),
		logger:      logger,
	}
}
