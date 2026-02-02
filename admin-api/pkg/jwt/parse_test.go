package jwt_test

import (
	"testing"

	"github.com/mir2-cpp/admin-api/pkg/jwt"
)

func TestJWTManager_Parse(t *testing.T) {
	manager := jwt.NewJWTManager("test-secret", 24)

	token, _, err := manager.Generate(1, "admin", "super_admin")
	if err != nil {
		t.Fatalf("Generate failed: %v", err)
	}

	claims, err := manager.Parse(token)
	if err != nil {
		t.Fatalf("Parse failed: %v", err)
	}

	if claims.UserID != 1 {
		t.Errorf("UserID = %d, want 1", claims.UserID)
	}
	if claims.Username != "admin" {
		t.Errorf("Username = %s, want admin", claims.Username)
	}
	if claims.Role != "super_admin" {
		t.Errorf("Role = %s, want super_admin", claims.Role)
	}
}
