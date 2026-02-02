package handler

import (
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

func (h *AuthHandler) Logout(c *gin.Context) {
	claims, exists := c.Get("claims")
	if !exists {
		response.Unauthorized(c, "no claims")
		return
	}

	jwtClaims := claims.(*jwt.Claims)
	ttl := time.Until(jwtClaims.ExpiresAt.Time)

	if err := h.authSvc.Logout(c.Request.Context(), jwtClaims.ID, jwtClaims.UserID, ttl); err != nil {
		response.InternalError(c, "logout failed")
		return
	}

	response.Success(c, nil)
}

func (h *AuthHandler) Me(c *gin.Context) {
	userID, _ := c.Get("user_id")
	username, _ := c.Get("username")
	role, _ := c.Get("role")

	response.Success(c, gin.H{
		"id":       userID,
		"username": username,
		"role":     role,
	})
}
