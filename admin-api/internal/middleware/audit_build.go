package middleware

import (
	"encoding/json"
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/model"
)

func buildAuditLog(c *gin.Context, body []byte) *model.AuditLog {
	path := c.Request.URL.Path
	action := extractAction(c.Request.Method, path)
	if action == "" {
		return nil
	}

	var operatorID *int
	var operatorName string
	if id, exists := c.Get("user_id"); exists {
		uid := id.(int)
		operatorID = &uid
	}
	if name, exists := c.Get("username"); exists {
		operatorName = name.(string)
	}
	if operatorName == "" {
		operatorName = "anonymous"
	}

	ip := c.ClientIP()
	ua := c.Request.UserAgent()
	details := sanitizeBody(body)

	return &model.AuditLog{
		OperatorID:   operatorID,
		OperatorName: operatorName,
		Action:       action,
		Details:      details,
		IPAddress:    &ip,
		UserAgent:    &ua,
	}
}
