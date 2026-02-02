package middleware

import (
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

func JWTAuth(jwtManager *jwt.JWTManager, authSvc *service.AuthService) gin.HandlerFunc {
	return func(c *gin.Context) {
		authHeader := c.GetHeader("Authorization")
		if authHeader == "" {
			response.Unauthorized(c, "missing authorization header")
			c.Abort()
			return
		}

		parts := strings.SplitN(authHeader, " ", 2)
		if len(parts) != 2 || parts[0] != "Bearer" {
			response.Unauthorized(c, "invalid authorization format")
			c.Abort()
			return
		}

		claims, err := jwtManager.Parse(parts[1])
		if err != nil {
			response.Unauthorized(c, "invalid token")
			c.Abort()
			return
		}

		blacklisted, _ := authSvc.IsTokenBlacklisted(c.Request.Context(), claims.ID)
		if blacklisted {
			response.Unauthorized(c, "token revoked")
			c.Abort()
			return
		}

		c.Set("claims", claims)
		c.Set("user_id", claims.UserID)
		c.Set("username", claims.Username)
		c.Set("role", claims.Role)
		c.Next()
	}
}
