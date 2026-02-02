package model

import "time"

type JWTBlacklist struct {
	JTI       string    `gorm:"primaryKey;size:64" json:"jti"`
	UserID    int       `gorm:"not null" json:"user_id"`
	Reason    *string   `gorm:"size:128" json:"reason"`
	ExpiresAt time.Time `gorm:"not null" json:"expires_at"`
	CreatedAt time.Time `gorm:"autoCreateTime" json:"created_at"`
}

func (JWTBlacklist) TableName() string {
	return "admin.jwt_blacklist"
}
