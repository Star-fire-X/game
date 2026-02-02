package repository

import (
	"github.com/lib/pq"
	"github.com/mir2-cpp/admin-api/internal/model"
)

// UpdateTOTPSetup stores TOTP secret and recovery codes
func (r *UserRepo) UpdateTOTPSetup(id int, secret string, codes []string) error {
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).Updates(map[string]interface{}{
		"totp_secret":    secret,
		"recovery_codes": pq.StringArray(codes),
	}).Error
}

// EnableTOTP activates 2FA for user
func (r *UserRepo) EnableTOTP(id int) error {
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).
		Update("totp_enabled", true).Error
}

// DisableTOTP removes 2FA from user
func (r *UserRepo) DisableTOTP(id int) error {
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).Updates(map[string]interface{}{
		"totp_enabled":   false,
		"totp_secret":    nil,
		"recovery_codes": nil,
	}).Error
}

// UpdateRecoveryCodes updates remaining recovery codes
func (r *UserRepo) UpdateRecoveryCodes(id int, codes []string) error {
	return r.db.Model(&model.AdminUser{}).Where("id = ?", id).
		Update("recovery_codes", pq.StringArray(codes)).Error
}
