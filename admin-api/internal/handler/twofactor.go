package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

type TwoFactorHandler struct {
	tfaSvc *service.TwoFactorService
}

func NewTwoFactorHandler() *TwoFactorHandler {
	return &TwoFactorHandler{
		tfaSvc: service.NewTwoFactorService(),
	}
}
