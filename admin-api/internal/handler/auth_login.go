package handler

import (
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

func (h *AuthHandler) Login(c *gin.Context) {
	var req LoginRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.BadRequest(c, "invalid request")
		return
	}

	ip := c.ClientIP()
	result, err := h.authSvc.Login(c.Request.Context(), req.Username, req.Password, ip)
	if err != nil {
		switch err {
		case service.ErrUserNotFound, service.ErrInvalidPassword:
			response.Unauthorized(c, "invalid credentials")
		case service.ErrAccountLocked:
			response.Forbidden(c, "account locked")
		case service.ErrAccountDisabled:
			response.Forbidden(c, "account disabled")
		default:
			response.InternalError(c, "login failed")
		}
		return
	}

	response.Success(c, gin.H{
		"token": result.Token,
		"user": gin.H{
			"id":       result.User.ID,
			"username": result.User.Username,
			"role":     result.User.Role,
		},
	})
}
