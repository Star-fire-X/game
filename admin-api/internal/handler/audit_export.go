package handler

import (
	"encoding/csv"
	"fmt"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/repository"
)

// ExportCSV exports audit logs as CSV
// GET /api/v1/audit/logs/export
func (h *AuditHandler) ExportCSV(c *gin.Context) {
	q := &repository.AuditQuery{
		Page:     1,
		PageSize: 10000,
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
		c.String(500, "export failed")
		return
	}

	filename := fmt.Sprintf("audit_logs_%s.csv", time.Now().Format("20060102"))
	c.Header("Content-Type", "text/csv")
	c.Header("Content-Disposition", fmt.Sprintf("attachment; filename=%s", filename))

	w := csv.NewWriter(c.Writer)
	w.Write([]string{"时间", "操作人", "操作", "目标", "IP"})

	for _, log := range result.Items {
		w.Write([]string{
			log.CreatedAt.Format("2006-01-02 15:04:05"),
			log.OperatorName,
			log.Action,
			stringOrEmpty(log.TargetName),
			stringOrEmpty(log.IPAddress),
		})
	}
	w.Flush()
}

func stringOrEmpty(s *string) string {
	if s == nil {
		return ""
	}
	return *s
}
