package middleware

// HasPermission checks if role has specific permission
func HasPermission(role string, perm Permission) bool {
	perms, ok := rolePermissions[role]
	if !ok {
		return false
	}
	for _, p := range perms {
		if p == perm {
			return true
		}
	}
	return false
}

// RequireRole middleware checks minimum role level
func RequireRole(minRole string) gin.HandlerFunc {
	return func(c *gin.Context) {
		role, exists := c.Get("role")
		if !exists {
			response.Unauthorized(c, "no role found")
			c.Abort()
			return
		}

		userLevel := roleHierarchy[role.(string)]
		requiredLevel := roleHierarchy[minRole]

		if userLevel < requiredLevel {
			response.Forbidden(c, "insufficient permissions")
			c.Abort()
			return
		}
		c.Next()
	}
}
