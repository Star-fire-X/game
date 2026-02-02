package handler

import (
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
	"go.uber.org/zap"
)

// ListConfigs returns all alert configurations
// GET /api/v1/alerts/configs
func (h *AlertHandler) ListConfigs(c *gin.Context) {
	configs, err := h.configRepo.List()
	if err != nil {
		h.logger.Error("failed to list alert configs", zap.Error(err))
		response.InternalError(c, "failed to list configs")
		return
	}
	response.Success(c, configs)
}

// GetConfig returns a single alert config
// GET /api/v1/alerts/configs/:id
func (h *AlertHandler) GetConfig(c *gin.Context) {
	id, err := strconv.Atoi(c.Param("id"))
	if err != nil {
		response.BadRequest(c, "invalid id")
		return
	}

	config, err := h.configRepo.GetByID(id)
	if err != nil {
		response.NotFound(c, "config not found")
		return
	}
	response.Success(c, config)
}
