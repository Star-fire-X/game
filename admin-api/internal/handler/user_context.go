package handler

import "github.com/gin-gonic/gin"

// CurrentUser 当前登录用户信息
type CurrentUser struct {
	ID       int
	Username string
	Role     string
}

// GetCurrentUser 从上下文获取当前用户
func GetCurrentUser(c *gin.Context) *CurrentUser {
	if user, exists := c.Get("user"); exists {
		if claims, ok := user.(map[string]interface{}); ok {
			return &CurrentUser{
				ID:       int(claims["user_id"].(float64)),
				Username: claims["username"].(string),
				Role:     claims["role"].(string),
			}
		}
	}
	return &CurrentUser{}
}
