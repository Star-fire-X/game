package repository

import "github.com/mir2-cpp/admin-api/internal/model"

// Update updates an alert config
func (r *AlertConfigRepo) Update(config *model.AlertConfig) error {
	return r.db.Save(config).Error
}

// GetEnabled returns all enabled alert configs
func (r *AlertConfigRepo) GetEnabled() ([]model.AlertConfig, error) {
	var configs []model.AlertConfig
	err := r.db.Where("is_enabled = ?", true).Find(&configs).Error
	return configs, err
}
