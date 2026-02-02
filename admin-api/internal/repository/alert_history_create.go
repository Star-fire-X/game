package repository

import "github.com/mir2-cpp/admin-api/internal/model"

// Create saves a new alert history record
func (r *AlertHistoryRepo) Create(history *model.AlertHistory) error {
	return r.db.Create(history).Error
}
