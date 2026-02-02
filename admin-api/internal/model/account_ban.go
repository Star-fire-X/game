package model

import "time"

// AccountBan 账号封禁记录
type AccountBan struct {
	ID              int64      `gorm:"primaryKey" json:"id"`
	AccountID       int64      `gorm:"not null" json:"account_id"`
	BanType         string     `gorm:"size:32;not null" json:"ban_type"` // ban, mute
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

func (AccountBan) TableName() string {
	return "game.account_bans"
}

// IsActive 检查封禁是否有效
func (b *AccountBan) IsActive() bool {
	if b.LiftedAt != nil {
		return false
	}
	if b.ExpiresAt == nil {
		return true // permanent
	}
	return time.Now().Before(*b.ExpiresAt)
}
