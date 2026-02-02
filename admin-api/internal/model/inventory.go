package model

// InventoryItem 背包物品模型 (game schema)
type InventoryItem struct {
	ID               int64 `gorm:"primaryKey" json:"id"`
	CharacterID      int64 `gorm:"column:character_id" json:"character_id"`
	Slot             int   `gorm:"column:slot" json:"slot"`
	ItemTemplateID   int   `gorm:"column:item_template_id" json:"item_template_id"`
	InstanceID       int64 `gorm:"column:instance_id" json:"instance_id"`
	Quantity         int   `gorm:"column:quantity" json:"quantity"`
	Durability       int   `gorm:"column:durability" json:"durability"`
	EnhancementLevel int   `gorm:"column:enhancement_level" json:"enhancement_level"`
}

func (InventoryItem) TableName() string {
	return "character_inventory"
}
