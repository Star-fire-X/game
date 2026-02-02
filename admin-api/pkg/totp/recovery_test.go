package totp

import (
	"testing"
)

func TestGenerateRecoveryCodes(t *testing.T) {
	svc := NewTOTPService()
	codes, hashed, err := svc.GenerateRecoveryCodes()
	if err != nil {
		t.Fatalf("GenerateRecoveryCodes failed: %v", err)
	}
	if len(codes) != RecoveryCodeNum {
		t.Errorf("Expected %d codes, got %d", RecoveryCodeNum, len(codes))
	}
	if len(hashed) != RecoveryCodeNum {
		t.Errorf("Expected %d hashed codes, got %d", RecoveryCodeNum, len(hashed))
	}
}
