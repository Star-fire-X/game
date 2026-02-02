package repository

import (
	"github.com/mir2-cpp/admin-api/internal/model"
	"gorm.io/gorm"
)

// InventoryRepository 背包数据访问层
type InventoryRepository struct {
	db *gorm.DB
}

// NewInventoryRepository 创建背包 Repository
func NewInventoryRepository(db *gorm.DB) *InventoryRepository {
	return &InventoryRepository{db: db}
}

// GetByCharacterID 获取角色背包物品
func (r *InventoryRepository) GetByCharacterID(charID int64) ([]model.InventoryItem, error) {
	var items []model.InventoryItem
	err := r.db.Where("character_id = ?", charID).
		Order("slot ASC").
		Find(&items).Error
	return items, err
}
