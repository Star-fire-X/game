package middleware

// RequirePermission middleware checks specific permission
func RequirePermission(perm Permission) gin.HandlerFunc {
	return func(c *gin.Context) {
		role, exists := c.Get("role")
		if !exists {
			response.Unauthorized(c, "no role found")
			c.Abort()
			return
		}

		if !HasPermission(role.(string), perm) {
			response.Forbidden(c, "permission denied")
			c.Abort()
			return
		}
		c.Next()
	}
}
