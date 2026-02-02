package middleware

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
)

// TestRBAC_PermissionMatrix tests the complete RBAC permission matrix
func TestRBAC_PermissionMatrix(t *testing.T) {
	// PRD Story 1.3: RBAC permission control
	tests := []struct {
		name     string
		role     string
		perm     Permission
		expected bool
	}{
		// Owner has all permissions
		{"Owner can view services", RoleOwner, PermServiceView, true},
		{"Owner can restart services", RoleOwner, PermServiceRestart, true},
		{"Owner can stop services", RoleOwner, PermServiceStop, true},
		{"Owner can view alerts", RoleOwner, PermAlertView, true},
		{"Owner can config alerts", RoleOwner, PermAlertConfig, true},
		{"Owner can view players", RoleOwner, PermPlayerView, true},
		{"Owner can action players", RoleOwner, PermPlayerAction, true},
		{"Owner can view audit", RoleOwner, PermAuditView, true},
		{"Owner can manage users", RoleOwner, PermUserManage, true},

		// TechGM permissions
		{"TechGM can view services", RoleTechGM, PermServiceView, true},
		{"TechGM can restart services", RoleTechGM, PermServiceRestart, true},
		{"TechGM cannot stop services", RoleTechGM, PermServiceStop, false},
		{"TechGM can view alerts", RoleTechGM, PermAlertView, true},
		{"TechGM cannot config alerts", RoleTechGM, PermAlertConfig, false},
		{"TechGM can view players", RoleTechGM, PermPlayerView, true},
		{"TechGM can action players", RoleTechGM, PermPlayerAction, true},
		{"TechGM can view audit", RoleTechGM, PermAuditView, true},
		{"TechGM cannot manage users", RoleTechGM, PermUserManage, false},

		// CS permissions (most restricted)
		{"CS can view services", RoleCS, PermServiceView, true},
		{"CS cannot restart services", RoleCS, PermServiceRestart, false},
		{"CS cannot stop services", RoleCS, PermServiceStop, false},
		{"CS cannot view alerts", RoleCS, PermAlertView, false},
		{"CS cannot config alerts", RoleCS, PermAlertConfig, false},
		{"CS can view players", RoleCS, PermPlayerView, true},
		{"CS can action players", RoleCS, PermPlayerAction, true},
		{"CS cannot view audit", RoleCS, PermAuditView, false},
		{"CS cannot manage users", RoleCS, PermUserManage, false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := HasPermission(tt.role, tt.perm)
			assert.Equal(t, tt.expected, got,
				"Role %s permission %s mismatch", tt.role, tt.perm)
		})
	}
}
