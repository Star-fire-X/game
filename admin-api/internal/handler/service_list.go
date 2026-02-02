package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// List returns all service statuses
// GET /api/v1/services
func (h *ServiceHandler) List(c *gin.Context) {
	statuses := h.monitorSvc.GetAllStatus()
	response.Success(c, statuses)
}
