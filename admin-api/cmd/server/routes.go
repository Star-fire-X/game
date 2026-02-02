package main

import (
	"io/fs"
	"net/http"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/handler"
	"github.com/mir2-cpp/admin-api/internal/middleware"
	"github.com/mir2-cpp/admin-api/internal/service"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"go.uber.org/zap"
)

func setupRoutes(r *gin.Engine, authHandler *handler.AuthHandler, tfaHandler *handler.TwoFactorHandler, svcHandler *handler.ServiceHandler, auditHandler *handler.AuditHandler, playerHandler *handler.PlayerHandler, charHandler *handler.CharacterHandler, jwtManager *jwt.JWTManager, authSvc *service.AuthService) {
	api := r.Group("/api/v1")
	{
		api.GET("/health", func(c *gin.Context) {
			c.JSON(http.StatusOK, gin.H{"status": "ok"})
		})

		auth := api.Group("/auth")
		{
			auth.POST("/login", authHandler.Login)
			auth.POST("/verify-2fa", tfaHandler.Verify2FALogin)
			auth.POST("/recovery", tfaHandler.VerifyRecoveryCode)
		}

		protected := api.Group("")
		protected.Use(middleware.JWTAuth(jwtManager, authSvc))
		{
			protected.POST("/auth/logout", authHandler.Logout)
			protected.GET("/auth/me", authHandler.Me)
			protected.POST("/auth/2fa/setup", tfaHandler.Setup)
			protected.POST("/auth/2fa/verify", tfaHandler.Verify)

			// Service APIs
			services := protected.Group("/services")
			{
				services.GET("", svcHandler.List)
				services.GET("/:name", svcHandler.Get)
				services.POST("/:name/start", middleware.RequireRole(middleware.RoleOwner), svcHandler.Start)
				services.POST("/:name/stop", middleware.RequireRole(middleware.RoleOwner), svcHandler.Stop)
				services.POST("/:name/restart", middleware.RequireRole(middleware.RoleTechGM), svcHandler.Restart)
			}

			// Audit APIs
			audit := protected.Group("/audit")
			audit.Use(middleware.RequireRole(middleware.RoleTechGM))
			{
				audit.GET("/logs", auditHandler.List)
				audit.GET("/logs/export", auditHandler.ExportCSV)
			}

			// Player APIs
			players := protected.Group("/players")
			{
				players.GET("/online", playerHandler.ListOnline)
				players.POST("/:id/kick", playerHandler.Kick)
				players.POST("/:id/ban", playerHandler.Ban)
				players.POST("/:id/unban/:ban_id", playerHandler.Unban)
				players.POST("/:id/mute", playerHandler.Mute)
				players.POST("/:id/unmute", playerHandler.Unmute)
			}

			// Character APIs
			characters := protected.Group("/characters")
			{
				characters.GET("/search", charHandler.Search)
				characters.GET("/:id", charHandler.GetByID)
				characters.GET("/:id/inventory", charHandler.GetInventory)
				characters.GET("/:id/equipment", charHandler.GetEquipment)
			}
		}
	}
}

func serveStatic(r *gin.Engine, logger *zap.Logger) {
	distFS, err := fs.Sub(staticFS, "web/dist")
	if err != nil {
		logger.Fatal("Failed to get sub filesystem", zap.Error(err))
	}
	r.NoRoute(func(c *gin.Context) {
		c.FileFromFS(c.Request.URL.Path, http.FS(distFS))
	})
}
