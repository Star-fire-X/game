package middleware

import (
	"bytes"
	"encoding/json"
	"io"
	"regexp"
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/config"
	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"go.uber.org/zap"
)

var sensitiveFields = regexp.MustCompile(`(?i)(password|secret|token|key)`)

type AuditWriter struct {
	auditChan chan *model.AuditLog
	repo      *repository.AuditRepo
}

func NewAuditWriter() *AuditWriter {
	w := &AuditWriter{
		auditChan: make(chan *model.AuditLog, 100),
		repo:      repository.NewAuditRepo(),
	}
	go w.worker()
	return w
}

func (w *AuditWriter) worker() {
	for log := range w.auditChan {
		w.repo.Create(log)
	}
}

func (w *AuditWriter) Write(log *model.AuditLog) {
	select {
	case w.auditChan <- log:
	default:
		// Channel full, log warning to prevent silent data loss
		w.logOverflowWarning(log)
	}
}

func (w *AuditWriter) logOverflowWarning(log *model.AuditLog) {
	// Use zap logger if available, fallback to standard log
	config.Logger().Warn("audit log channel overflow, log dropped",
		zap.String("action", log.Action),
		zap.String("operator", log.OperatorName),
	)
}
