package repository

import (
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

// AlertHistoryRepo handles alert history database operations
type AlertHistoryRepo struct {
	db *gorm.DB
}

// NewAlertHistoryRepo creates a new alert history repository
func NewAlertHistoryRepo(db *gorm.DB) *AlertHistoryRepo {
	return &AlertHistoryRepo{db: db}
}

// AlertHistoryQuery represents query parameters
type AlertHistoryQuery struct {
	ServiceName string
	Severity    string
	StartTime   *time.Time
	EndTime     *time.Time
	Page        int
	PageSize    int
}
