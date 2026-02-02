package handler

import (
	"github.com/mir2-cpp/admin-api/internal/repository"
	"go.uber.org/zap"
)

// AlertHandler handles alert-related HTTP requests
type AlertHandler struct {
	configRepo  *repository.AlertConfigRepo
	historyRepo *repository.AlertHistoryRepo
	logger      *zap.Logger
}

// NewAlertHandler creates a new alert handler
func NewAlertHandler(
	configRepo *repository.AlertConfigRepo,
	historyRepo *repository.AlertHistoryRepo,
	logger *zap.Logger,
) *AlertHandler {
	return &AlertHandler{
		configRepo:  configRepo,
		historyRepo: historyRepo,
		logger:      logger,
	}
}
