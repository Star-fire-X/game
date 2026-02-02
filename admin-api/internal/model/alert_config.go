package model

import (
	"encoding/json"
	"time"
)

type AlertConfig struct {
	ID                 int             `gorm:"primaryKey" json:"id"`
	RuleName           string          `gorm:"size:64;not null" json:"rule_name"`
	RuleType           string          `gorm:"size:32;not null" json:"rule_type"`
	ServiceName        *string         `gorm:"size:32" json:"service_name"`
	Threshold          *float64        `json:"threshold"`
	Severity           string          `gorm:"size:16;not null" json:"severity"`
	IsEnabled          bool            `gorm:"default:true" json:"is_enabled"`
	DedupWindowSeconds int             `gorm:"default:300" json:"dedup_window_seconds"`
	NotifyWechat       bool            `gorm:"default:true" json:"notify_wechat"`
	NotifyWebhook      bool            `gorm:"default:false" json:"notify_webhook"`
	WebhookURL         *string         `gorm:"size:512" json:"webhook_url"`
	WebhookHeaders     json.RawMessage `gorm:"type:jsonb" json:"webhook_headers"`
	CreatedAt          time.Time       `gorm:"autoCreateTime" json:"created_at"`
	UpdatedAt          time.Time       `gorm:"autoUpdateTime" json:"updated_at"`
}

func (AlertConfig) TableName() string {
	return "admin.alert_configs"
}
