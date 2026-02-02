package handler

import (
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// ListHistory returns alert history with filters
// GET /api/v1/alerts/history
func (h *AlertHandler) ListHistory(c *gin.Context) {
	q := &repository.AlertHistoryQuery{
		Page:     1,
		PageSize: 20,
	}

	if p := c.Query("page"); p != "" {
		if v, _ := strconv.Atoi(p); v > 0 {
			q.Page = v
		}
	}
	if ps := c.Query("page_size"); ps != "" {
		if v, _ := strconv.Atoi(ps); v > 0 && v <= 100 {
			q.PageSize = v
		}
	}

	q.ServiceName = c.Query("service")
	q.Severity = c.Query("severity")

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

	result, err := h.historyRepo.List(q)
	if err != nil {
		response.InternalError(c, "failed to query history")
		return
	}

	response.Success(c, result)
}
