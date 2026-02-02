package handler

import (
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// List returns audit logs with filters
// GET /api/v1/audit/logs
func (h *AuditHandler) List(c *gin.Context) {
	q := &repository.AuditQuery{
		Page:     1,
		PageSize: 20,
	}

	if p := c.Query("page"); p != "" {
		if v, err := strconv.Atoi(p); err == nil && v > 0 {
			q.Page = v
		}
	}
	if ps := c.Query("page_size"); ps != "" {
		if v, err := strconv.Atoi(ps); err == nil && v > 0 && v <= 100 {
			q.PageSize = v
		}
	}

	q.OperatorName = c.Query("operator")
	q.Action = c.Query("action")

	if start := c.Query("start_time"); start != "" {
		if t, err := time.Parse(time.RFC3339, start); err == nil {
			q.StartTime = &t
		}
	}
	if end := c.Query("end_time"); end != "" {
		if t, err := time.Parse(time.RFC3339, end); err == nil {
			q.EndTime = &t
		}
	}

	result, err := h.auditRepo.List(q)
	if err != nil {
		response.InternalError(c, "failed to query logs")
		return
	}

	response.Success(c, result)
}
