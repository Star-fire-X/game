package middleware

import (
	"bytes"
	"encoding/json"
	"io"
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/model"
)

func AuditLog(writer *AuditWriter) gin.HandlerFunc {
	return func(c *gin.Context) {
		if c.Request.Method == "GET" {
			c.Next()
			return
		}

		var bodyBytes []byte
		if c.Request.Body != nil {
			bodyBytes, _ = io.ReadAll(c.Request.Body)
			c.Request.Body = io.NopCloser(bytes.NewBuffer(bodyBytes))
		}

		c.Next()

		go func() {
			log := buildAuditLog(c, bodyBytes)
			if log != nil {
				writer.Write(log)
			}
		}()
	}
}
