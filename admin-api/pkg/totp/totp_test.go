package totp

import (
	"testing"
)

func TestGenerateSecret(t *testing.T) {
	svc := NewTOTPService()
	result, err := svc.GenerateSecret("testuser")
	if err != nil {
		t.Fatalf("GenerateSecret failed: %v", err)
	}
	if result.Secret == "" {
		t.Error("Secret should not be empty")
	}
	if result.URL == "" {
		t.Error("URL should not be empty")
	}
}
