package repository

import (
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

// LiftAccountBan 解除账号封禁
func (r *BanRepository) LiftAccountBan(banID int64, operatorID int, reason string) error {
	now := time.Now()
	return r.db.Model(&model.AccountBan{}).
		Where("id = ?", banID).
		Updates(map[string]interface{}{
			"lifted_at":   now,
			"lifted_by":   operatorID,
			"lift_reason": reason,
		}).Error
}

// ListAccountBans 查询账号封禁历史
func (r *BanRepository) ListAccountBans(accountID int64, page, pageSize int) ([]model.AccountBan, int64, error) {
	var bans []model.AccountBan
	var total int64

	query := r.db.Model(&model.AccountBan{}).Where("account_id = ?", accountID)
	query.Count(&total)

	err := query.Order("created_at DESC").
		Offset((page - 1) * pageSize).
		Limit(pageSize).
		Find(&bans).Error

	return bans, total, err
}
