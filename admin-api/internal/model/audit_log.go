package model

import (
	"encoding/json"
	"time"
)

type AuditLog struct {
	ID           int64           `gorm:"primaryKey" json:"id"`
	OperatorID   *int            `json:"operator_id"`
	OperatorName string          `gorm:"size:64;not null" json:"operator_name"`
	Action       string          `gorm:"size:64;not null" json:"action"`
	TargetType   *string         `gorm:"size:32" json:"target_type"`
	TargetID     *string         `gorm:"size:64" json:"target_id"`
	TargetName   *string         `gorm:"size:128" json:"target_name"`
	Details      json.RawMessage `gorm:"type:jsonb" json:"details"`
	IPAddress    *string         `gorm:"type:inet" json:"ip_address"`
	UserAgent    *string         `json:"user_agent"`
	CreatedAt    time.Time       `gorm:"autoCreateTime" json:"created_at"`
}

func (AuditLog) TableName() string {
	return "admin.audit_logs"
}

const (
	ActionLogin       = "login"
	ActionLogout      = "logout"
	ActionLoginFailed = "login_failed"
	ActionKickPlayer  = "kick_player"
	ActionBanPlayer   = "ban_player"
	ActionUnbanPlayer = "unban_player"
	ActionMutePlayer  = "mute_player"
)
