package repository

import "github.com/mir2-cpp/admin-api/internal/model"

// AlertHistoryResult represents paginated result
type AlertHistoryResult struct {
	Items      []model.AlertHistory `json:"items"`
	Total      int64                `json:"total"`
	Page       int                  `json:"page"`
	PageSize   int                  `json:"page_size"`
	TotalPages int                  `json:"total_pages"`
}

// List returns alert history with filters
func (r *AlertHistoryRepo) List(q *AlertHistoryQuery) (*AlertHistoryResult, error) {
	query := r.db.Model(&model.AlertHistory{})

	if q.ServiceName != "" {
		query = query.Where("service_name = ?", q.ServiceName)
	}
	if q.Severity != "" {
		query = query.Where("severity = ?", q.Severity)
	}
	if q.StartTime != nil {
		query = query.Where("created_at >= ?", q.StartTime)
	}
	if q.EndTime != nil {
		query = query.Where("created_at <= ?", q.EndTime)
	}

	var total int64
	query.Count(&total)

	var items []model.AlertHistory
	offset := (q.Page - 1) * q.PageSize
	err := query.Order("created_at DESC").
		Offset(offset).Limit(q.PageSize).
		Find(&items).Error

	totalPages := int(total) / q.PageSize
	if int(total)%q.PageSize > 0 {
		totalPages++
	}

	return &AlertHistoryResult{
		Items:      items,
		Total:      total,
		Page:       q.Page,
		PageSize:   q.PageSize,
		TotalPages: totalPages,
	}, err
}
