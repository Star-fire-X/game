package middleware

import (
	"testing"
)

func TestHasPermission(t *testing.T) {
	tests := []struct {
		role string
		perm Permission
		want bool
	}{
		{RoleOwner, PermServiceStop, true},
		{RoleTechGM, PermServiceStop, false},
		{RoleCS, PermPlayerView, true},
		{RoleCS, PermAuditView, false},
	}

	for _, tt := range tests {
		got := HasPermission(tt.role, tt.perm)
		if got != tt.want {
			t.Errorf("HasPermission(%s, %s) = %v, want %v",
				tt.role, tt.perm, got, tt.want)
		}
	}
}
