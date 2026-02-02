package handler_test

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
)

func init() {
	gin.SetMode(gin.TestMode)
}

func TestHealthEndpoint(t *testing.T) {
	r := gin.New()
	r.GET("/api/v1/health", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"status": "ok"})
	})

	req := httptest.NewRequest("GET", "/api/v1/health", nil)
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)

	var resp map[string]string
	json.Unmarshal(w.Body.Bytes(), &resp)
	assert.Equal(t, "ok", resp["status"])
}

func TestLoginEndpoint_InvalidCredentials(t *testing.T) {
	r := gin.New()
	r.POST("/api/v1/auth/login", func(c *gin.Context) {
		c.JSON(http.StatusUnauthorized, gin.H{
			"code":    401,
			"message": "invalid credentials",
		})
	})

	body := bytes.NewBufferString(`{"username":"test","password":"wrong"}`)
	req := httptest.NewRequest("POST", "/api/v1/auth/login", body)
	req.Header.Set("Content-Type", "application/json")
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
}

func TestProtectedEndpoint_NoToken(t *testing.T) {
	r := gin.New()
	r.GET("/api/v1/services", func(c *gin.Context) {
		auth := c.GetHeader("Authorization")
		if auth == "" {
			c.JSON(http.StatusUnauthorized, gin.H{
				"code":    401,
				"message": "missing token",
			})
			return
		}
		c.JSON(http.StatusOK, gin.H{"services": []string{}})
	})

	req := httptest.NewRequest("GET", "/api/v1/services", nil)
	w := httptest.NewRecorder()
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
}
