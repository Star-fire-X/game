package repository

import (
	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

type AuditRepo struct {
	db *gorm.DB
}

func NewAuditRepo() *AuditRepo {
	return &AuditRepo{db: db}
}

func (r *AuditRepo) Create(log *model.AuditLog) error {
	return r.db.Create(log).Error
}
