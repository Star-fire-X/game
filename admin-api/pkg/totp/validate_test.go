package totp

import (
	"testing"

	"github.com/pquerna/otp/totp"
)

func TestValidateCode(t *testing.T) {
	svc := NewTOTPService()
	result, _ := svc.GenerateSecret("testuser")

	// Generate valid code
	code, _ := totp.GenerateCode(result.Secret, time.Now())

	if !svc.ValidateCode(result.Secret, code) {
		t.Error("Valid code should pass validation")
	}

	if svc.ValidateCode(result.Secret, "000000") {
		t.Error("Invalid code should fail validation")
	}
}
