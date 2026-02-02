package repository

import (
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
)

// CreateCharacterBan 创建角色封禁记录
func (r *BanRepository) CreateCharacterBan(ban *model.CharacterBan) error {
	return r.db.Create(ban).Error
}

// GetActiveCharacterBan 获取角色的有效封禁
func (r *BanRepository) GetActiveCharacterBan(charID int64, banType string) (*model.CharacterBan, error) {
	var ban model.CharacterBan
	err := r.db.Where("character_id = ? AND ban_type = ? AND lifted_at IS NULL", charID, banType).
		Where("expires_at IS NULL OR expires_at > ?", time.Now()).
		Order("created_at DESC").
		First(&ban).Error
	if err != nil {
		return nil, err
	}
	return &ban, nil
}

// LiftCharacterBan 解除角色封禁
func (r *BanRepository) LiftCharacterBan(banID int64, operatorID int, reason string) error {
	now := time.Now()
	return r.db.Model(&model.CharacterBan{}).
		Where("id = ?", banID).
		Updates(map[string]interface{}{
			"lifted_at":   now,
			"lifted_by":   operatorID,
			"lift_reason": reason,
		}).Error
}
