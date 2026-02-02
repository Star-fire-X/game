package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// Get returns status for a specific service
// GET /api/v1/services/:name
func (h *ServiceHandler) Get(c *gin.Context) {
	name := c.Param("name")
	status := h.monitorSvc.GetStatus(name)
	if status == nil {
		response.NotFound(c, "service not found")
		return
	}
	response.Success(c, status)
}
