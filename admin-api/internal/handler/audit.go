package handler

import (
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

type AuditHandler struct {
	auditRepo *repository.AuditRepo
}

func NewAuditHandler() *AuditHandler {
	return &AuditHandler{
		auditRepo: repository.NewAuditRepo(),
	}
}
