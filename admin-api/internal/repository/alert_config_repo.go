package repository

import (
	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

// AlertConfigRepo handles alert config database operations
type AlertConfigRepo struct {
	db *gorm.DB
}

// NewAlertConfigRepo creates a new alert config repository
func NewAlertConfigRepo(db *gorm.DB) *AlertConfigRepo {
	return &AlertConfigRepo{db: db}
}

// List returns all alert configs
func (r *AlertConfigRepo) List() ([]model.AlertConfig, error) {
	var configs []model.AlertConfig
	err := r.db.Order("id ASC").Find(&configs).Error
	return configs, err
}

// GetByID returns an alert config by ID
func (r *AlertConfigRepo) GetByID(id int) (*model.AlertConfig, error) {
	var config model.AlertConfig
	err := r.db.First(&config, id).Error
	if err != nil {
		return nil, err
	}
	return &config, nil
}
