package repository

import (
	"github.com/mir2-cpp/admin-api/internal/model"
)

type AuditListResult struct {
	Total int64             `json:"total"`
	Items []model.AuditLog  `json:"items"`
}

// List queries audit logs with filters
func (r *AuditRepo) List(q *AuditQuery) (*AuditListResult, error) {
	query := r.db.Model(&model.AuditLog{})

	if q.OperatorID != nil {
		query = query.Where("operator_id = ?", *q.OperatorID)
	}
	if q.OperatorName != "" {
		query = query.Where("operator_name LIKE ?", "%"+q.OperatorName+"%")
	}
	if q.Action != "" {
		query = query.Where("action = ?", q.Action)
	}
	if q.StartTime != nil {
		query = query.Where("created_at >= ?", *q.StartTime)
	}
	if q.EndTime != nil {
		query = query.Where("created_at <= ?", *q.EndTime)
	}

	var total int64
	query.Count(&total)

	var items []model.AuditLog
	offset := (q.Page - 1) * q.PageSize
	err := query.Order("created_at DESC").
		Offset(offset).Limit(q.PageSize).
		Find(&items).Error

	return &AuditListResult{Total: total, Items: items}, err
}
