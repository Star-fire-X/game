package middleware

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// Role constants matching PRD requirements
const (
	RoleOwner  = "Owner"
	RoleTechGM = "TechGM"
	RoleCS     = "CS"
)

// Role hierarchy levels
var roleHierarchy = map[string]int{
	RoleOwner:  3,
	RoleTechGM: 2,
	RoleCS:     1,
}
