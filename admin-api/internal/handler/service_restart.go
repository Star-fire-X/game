package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// Restart restarts a service container
// POST /api/v1/services/:name/restart
func (h *ServiceHandler) Restart(c *gin.Context) {
	name := c.Param("name")
	if err := h.dockerSvc.RestartContainer(c.Request.Context(), name); err != nil {
		response.InternalError(c, "failed to restart service")
		return
	}
	response.Success(c, gin.H{"message": "service restarted"})
}
