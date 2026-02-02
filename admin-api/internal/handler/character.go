package handler

import (
	"github.com/mir2-cpp/admin-api/internal/repository"
)

// CharacterHandler 角色查询处理器
type CharacterHandler struct {
	charRepo  *repository.CharacterRepository
	invRepo   *repository.InventoryRepository
	equipRepo *repository.EquipmentRepository
}

// NewCharacterHandler 创建角色查询处理器
func NewCharacterHandler(
	charRepo *repository.CharacterRepository,
	invRepo *repository.InventoryRepository,
	equipRepo *repository.EquipmentRepository,
) *CharacterHandler {
	return &CharacterHandler{
		charRepo:  charRepo,
		invRepo:   invRepo,
		equipRepo: equipRepo,
	}
}
