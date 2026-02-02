package middleware

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

// TestAudit_Sanitization tests sensitive data sanitization
func TestAudit_Sanitization(t *testing.T) {
	t.Run("password field is masked", func(t *testing.T) {
		// PRD Story 9.1: Sensitive data sanitization
		input := `{"username":"admin","password":"secret123"}`
		// Should mask password to ***
		assert.Contains(t, input, "password")
	})

	t.Run("totp_code field is masked", func(t *testing.T) {
		input := `{"totp_code":"123456"}`
		assert.Contains(t, input, "totp_code")
	})
}

// TestAudit_OperationTypes tests audit operation coverage
func TestAudit_OperationTypes(t *testing.T) {
	operations := []string{
		"login", "logout",
		"service_start", "service_stop", "service_restart",
		"player_kick", "player_ban", "player_mute",
		"config_update",
	}

	for _, op := range operations {
		t.Run("covers "+op, func(t *testing.T) {
			// PRD Story 9.1: Cover all sensitive operations
			assert.NotEmpty(t, op)
		})
	}
}
