package main

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/handler"
	"github.com/mir2-cpp/admin-api/internal/middleware"
)

func setupAlertRoutes(protected *gin.RouterGroup, alertHandler *handler.AlertHandler) {
	alerts := protected.Group("/alerts")
	alerts.Use(middleware.RequireRole(middleware.RoleTechGM))
	{
		alerts.GET("/configs", alertHandler.ListConfigs)
		alerts.GET("/configs/:id", alertHandler.GetConfig)
		alerts.PUT("/configs/:id", middleware.RequireRole(middleware.RoleOwner), alertHandler.UpdateConfig)
		alerts.GET("/history", alertHandler.ListHistory)
	}
}
