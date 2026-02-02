package jwt_test

import (
	"testing"
	"time"

	"github.com/mir2-cpp/admin-api/pkg/jwt"
)

func TestJWTManager_Generate(t *testing.T) {
	manager := jwt.NewJWTManager("test-secret", 24)

	token, jti, err := manager.Generate(1, "admin", "super_admin")
	if err != nil {
		t.Fatalf("Generate failed: %v", err)
	}
	if token == "" {
		t.Error("Token should not be empty")
	}
	if jti == "" {
		t.Error("JTI should not be empty")
	}
}
