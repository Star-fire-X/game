package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

type AuthHandler struct {
	authSvc    *service.AuthService
	jwtManager *jwt.JWTManager
}

func NewAuthHandler(authSvc *service.AuthService, jwtManager *jwt.JWTManager) *AuthHandler {
	return &AuthHandler{
		authSvc:    authSvc,
		jwtManager: jwtManager,
	}
}

type LoginRequest struct {
	Username string `json:"username" binding:"required"`
	Password string `json:"password" binding:"required"`
}
