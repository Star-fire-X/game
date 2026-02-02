package model

// EquipmentItem 装备物品模型 (game schema)
type EquipmentItem struct {
	ID               int64 `gorm:"primaryKey" json:"id"`
	CharacterID      int64 `gorm:"column:character_id" json:"character_id"`
	Slot             int   `gorm:"column:slot" json:"slot"`
	ItemTemplateID   int   `gorm:"column:item_template_id" json:"item_template_id"`
	InstanceID       int64 `gorm:"column:instance_id" json:"instance_id"`
	Durability       int   `gorm:"column:durability" json:"durability"`
	EnhancementLevel int   `gorm:"column:enhancement_level" json:"enhancement_level"`
}

func (EquipmentItem) TableName() string {
	return "character_equipment"
}

// EquipmentSlot 装备槽位常量
const (
	SlotWeapon    = 0 // 武器
	SlotArmor     = 1 // 衣服
	SlotHelmet    = 2 // 头盔
	SlotNecklace  = 3 // 项链
	SlotBraceletL = 4 // 左手镯
	SlotBraceletR = 5 // 右手镯
	SlotRingL     = 6 // 左戒指
	SlotRingR     = 7 // 右戒指
	SlotBoots     = 8 // 鞋子
	SlotBelt      = 9 // 腰带
)
