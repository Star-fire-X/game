package model

import (
	"encoding/json"
	"time"
)

type AlertHistory struct {
	ID              int64           `gorm:"primaryKey" json:"id"`
	RuleID          *int            `json:"rule_id"`
	RuleName        string          `gorm:"size:64;not null" json:"rule_name"`
	ServiceName     *string         `gorm:"size:32" json:"service_name"`
	Severity        string          `gorm:"size:16;not null" json:"severity"`
	Message         string          `gorm:"not null" json:"message"`
	Details         json.RawMessage `gorm:"type:jsonb" json:"details"`
	NotifiedWechat  bool            `gorm:"default:false" json:"notified_wechat"`
	NotifiedWebhook bool            `gorm:"default:false" json:"notified_webhook"`
	CreatedAt       time.Time       `gorm:"autoCreateTime" json:"created_at"`
	ResolvedAt      *time.Time      `json:"resolved_at"`
}

func (AlertHistory) TableName() string {
	return "admin.alert_history"
}
