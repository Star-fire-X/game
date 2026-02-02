package repository

import (
	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

// EquipmentRepository 装备数据访问层
type EquipmentRepository struct {
	db *gorm.DB
}

// NewEquipmentRepository 创建装备 Repository
func NewEquipmentRepository(db *gorm.DB) *EquipmentRepository {
	return &EquipmentRepository{db: db}
}

// GetByCharacterID 获取角色装备
func (r *EquipmentRepository) GetByCharacterID(charID int64) ([]model.EquipmentItem, error) {
	var items []model.EquipmentItem
	err := r.db.Where("character_id = ?", charID).
		Order("slot ASC").
		Find(&items).Error
	return items, err
}
