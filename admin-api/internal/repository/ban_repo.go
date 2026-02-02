package repository

import (
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

type BanRepository struct {
	db *gorm.DB
}

func NewBanRepository(db *gorm.DB) *BanRepository {
	return &BanRepository{db: db}
}

// CreateAccountBan 创建账号封禁记录
func (r *BanRepository) CreateAccountBan(ban *model.AccountBan) error {
	return r.db.Create(ban).Error
}

// GetActiveAccountBan 获取账号的有效封禁
func (r *BanRepository) GetActiveAccountBan(accountID int64, banType string) (*model.AccountBan, error) {
	var ban model.AccountBan
	err := r.db.Where("account_id = ? AND ban_type = ? AND lifted_at IS NULL", accountID, banType).
		Where("expires_at IS NULL OR expires_at > ?", time.Now()).
		Order("created_at DESC").
		First(&ban).Error
	if err != nil {
		return nil, err
	}
	return &ban, nil
}
