package repository

import (
	"gorm.io/gorm"
)

// CharacterRepository 角色数据访问层
type CharacterRepository struct {
	db *gorm.DB
}

// NewCharacterRepository 创建角色 Repository
func NewCharacterRepository(db *gorm.DB) *CharacterRepository {
	return &CharacterRepository{db: db}
}

// Character 角色模型 (game schema)
type Character struct {
	ID          int64  `gorm:"primaryKey"`
	AccountID   int64  `gorm:"column:account_id"`
	Name        string `gorm:"column:name"`
	Level       int    `gorm:"column:level"`
	Class       int    `gorm:"column:char_class"`
	Gender      int    `gorm:"column:gender"`
	HP          int    `gorm:"column:hp"`
	MaxHP       int    `gorm:"column:max_hp"`
	MP          int    `gorm:"column:mp"`
	MaxMP       int    `gorm:"column:max_mp"`
	Experience  int64  `gorm:"column:experience"`
	Gold        int64  `gorm:"column:gold"`
	MapID       int    `gorm:"column:current_map_id"`
	X           int    `gorm:"column:x"`
	Y           int    `gorm:"column:y"`
	CreatedAt   string `gorm:"column:created_at"`
	LastLoginAt string `gorm:"column:last_login_at"`
}

func (Character) TableName() string {
	return "game.characters"
}
