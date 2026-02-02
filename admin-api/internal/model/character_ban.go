package model

import "time"

// CharacterBan 角色封禁记录（禁言等）
type CharacterBan struct {
	ID              int64      `gorm:"primaryKey" json:"id"`
	CharacterID     int64      `gorm:"not null" json:"character_id"`
	CharacterName   string     `gorm:"size:64;not null" json:"character_name"`
	BanType         string     `gorm:"size:32;not null" json:"ban_type"`
	DurationSeconds int        `gorm:"not null" json:"duration_seconds"`
	Reason          string     `gorm:"type:text;not null" json:"reason"`
	OperatorID      int        `gorm:"not null" json:"operator_id"`
	OperatorName    string     `gorm:"size:64;not null" json:"operator_name"`
	CreatedAt       time.Time  `gorm:"autoCreateTime" json:"created_at"`
	ExpiresAt       *time.Time `json:"expires_at"`
	LiftedAt        *time.Time `json:"lifted_at"`
	LiftedBy        *int       `json:"lifted_by"`
	LiftReason      *string    `json:"lift_reason"`
}

func (CharacterBan) TableName() string {
	return "game.character_bans"
}

// IsActive 检查封禁是否有效
func (b *CharacterBan) IsActive() bool {
	if b.LiftedAt != nil {
		return false
	}
	if b.ExpiresAt == nil {
		return true
	}
	return time.Now().Before(*b.ExpiresAt)
}
