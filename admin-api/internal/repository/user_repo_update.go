package repository

import (
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

func (r *UserRepo) UpdateLoginInfo(id int, ip string) error {
	now := time.Now()
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).Updates(map[string]interface{}{
		"last_login_at": now,
		"last_login_ip": ip,
		"failed_attempts": 0,
		"locked_until": nil,
	}).Error
}

func (r *UserRepo) IncrFailedAttempts(id int) error {
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).
		UpdateColumn("failed_attempts", gorm.Expr("failed_attempts + 1")).Error
}

func (r *UserRepo) LockUser(id int, until time.Time) error {
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).
		Update("locked_until", until).Error
}

func (r *UserRepo) Create(user *model.AdminUser) error {
	return r.db.Create(user).Error
}
