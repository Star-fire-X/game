package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// Start starts a service container
// POST /api/v1/services/:name/start
func (h *ServiceHandler) Start(c *gin.Context) {
	name := c.Param("name")
	if err := h.dockerSvc.StartContainer(c.Request.Context(), name); err != nil {
		response.InternalError(c, "failed to start service")
		return
	}
	response.Success(c, gin.H{"message": "service started"})
}
