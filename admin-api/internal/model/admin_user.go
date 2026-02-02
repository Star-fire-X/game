package model

import (
	"time"

	"github.com/lib/pq"
)

type AdminUser struct {
	ID            int            `gorm:"primaryKey" json:"id"`
	Username      string         `gorm:"uniqueIndex;size:64;not null" json:"username"`
	PasswordHash  string         `gorm:"size:256;not null" json:"-"`
	TOTPSecret    *string        `gorm:"size:64" json:"-"`
	TOTPEnabled   bool           `gorm:"default:false" json:"totp_enabled"`
	RecoveryCodes pq.StringArray `gorm:"type:text[]" json:"-"`
	Role          string         `gorm:"size:32;not null" json:"role"`
	IsActive      bool           `gorm:"default:true" json:"is_active"`
	FailedAttempts int           `gorm:"default:0" json:"-"`
	LockedUntil   *time.Time     `json:"-"`
	CreatedAt     time.Time      `gorm:"autoCreateTime" json:"created_at"`
	UpdatedAt     time.Time      `gorm:"autoUpdateTime" json:"updated_at"`
	LastLoginAt   *time.Time     `json:"last_login_at"`
	LastLoginIP   *string        `gorm:"type:inet" json:"last_login_ip"`
}

func (AdminUser) TableName() string {
	return "admin.users"
}

const (
	RoleOwner  = "Owner"
	RoleTechGM = "TechGM"
	RoleCS     = "CS"
)
