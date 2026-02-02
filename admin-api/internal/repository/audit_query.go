package repository

import (
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
)

type AuditQuery struct {
	OperatorID   *int
	OperatorName string
	Action       string
	StartTime    *time.Time
	EndTime      *time.Time
	Page         int
	PageSize     int
}
