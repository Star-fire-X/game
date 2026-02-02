package handler

import (
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
	"go.uber.org/zap"
)

// UpdateConfigRequest represents update request body
type UpdateConfigRequest struct {
	IsEnabled          *bool    `json:"is_enabled"`
	Severity           *string  `json:"severity"`
	Threshold          *float64 `json:"threshold"`
	DedupWindowSeconds *int     `json:"dedup_window_seconds"`
	NotifyWechat       *bool    `json:"notify_wechat"`
	NotifyWebhook      *bool    `json:"notify_webhook"`
	WebhookURL         *string  `json:"webhook_url"`
}

// UpdateConfig updates an alert configuration
// PUT /api/v1/alerts/configs/:id
func (h *AlertHandler) UpdateConfig(c *gin.Context) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		response.BadRequest(c, "invalid id")
		return
	}

	var req UpdateConfigRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.BadRequest(c, "invalid request body")
		return
	}

	config, err := h.configRepo.GetByID(id)
	if err != nil {
		response.NotFound(c, "config not found")
		return
	}

	// Apply updates
	if req.IsEnabled != nil {
		config.IsEnabled = *req.IsEnabled
	}
	if req.Severity != nil {
		config.Severity = *req.Severity
	}
	if req.Threshold != nil {
		config.Threshold = req.Threshold
	}
	if req.DedupWindowSeconds != nil {
		config.DedupWindowSeconds = *req.DedupWindowSeconds
	}
	if req.NotifyWechat != nil {
		config.NotifyWechat = *req.NotifyWechat
	}
	if req.NotifyWebhook != nil {
		config.NotifyWebhook = *req.NotifyWebhook
	}
	if req.WebhookURL != nil {
		config.WebhookURL = req.WebhookURL
	}

	if err := h.configRepo.Update(config); err != nil {
		h.logger.Error("failed to update config", zap.Error(err))
		response.InternalError(c, "failed to update")
		return
	}

	response.Success(c, config)
}
