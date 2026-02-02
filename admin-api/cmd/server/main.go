package main

import (
	"context"
	"embed"
	"fmt"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/config"
	"github.com/mir2-cpp/admin-api/internal/handler"
	"github.com/mir2-cpp/admin-api/internal/middleware"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/internal/service"
	"github.com/mir2-cpp/admin-api/internal/service/docker"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/internal/service/monitor"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"go.uber.org/zap"
)

//go:embed web/dist/*
var staticFS embed.FS

func main() {
	cfg, err := config.Load("config.yaml")
	if err != nil {
		panic(fmt.Sprintf("Failed to load config: %v", err))
	}

	if err := config.InitLogger(&cfg.Log); err != nil {
		panic(fmt.Sprintf("Failed to init logger: %v", err))
	}
	logger := config.Logger()
	defer logger.Sync()

	if err := repository.InitDB(&cfg.Database); err != nil {
		logger.Fatal("Failed to init database", zap.Error(err))
	}

	if err := repository.InitRedis(&cfg.Redis); err != nil {
		logger.Fatal("Failed to init redis", zap.Error(err))
	}

	if cfg.Server.Mode == "release" {
		gin.SetMode(gin.ReleaseMode)
	}

	jwtManager := jwt.NewJWTManager(cfg.JWT.Secret, cfg.JWT.ExpireHour)
	authSvc := service.NewAuthService(jwtManager)
	authHandler := handler.NewAuthHandler(authSvc, jwtManager)
	tfaHandler := handler.NewTwoFactorHandler()
	auditHandler := handler.NewAuditHandler()
	auditWriter := middleware.NewAuditWriter()

	// Docker service
	dockerSvc, err := docker.NewDockerService()
	if err != nil {
		logger.Warn("Docker service unavailable", zap.Error(err))
	}

	// Monitor service
	redisCli := repository.GetRedis()
	monitorSvc := monitor.NewMonitor(redisCli, logger)
	monitorSvc.Start(context.Background())

	// Game service client
	gameClient := game.NewClient(
		cfg.GameService.Host,
		cfg.GameService.Port,
		cfg.GameService.Timeout,
	)

	svcHandler := handler.NewServiceHandler(dockerSvc, monitorSvc, gameClient)

	banRepo := repository.NewBanRepository(repository.GetDB())
	playerHandler := handler.NewPlayerHandler(gameClient, banRepo)

	// Character repository
	charRepo := repository.NewCharacterRepository(repository.GetDB())
	invRepo := repository.NewInventoryRepository(repository.GetDB())
	equipRepo := repository.NewEquipmentRepository(repository.GetDB())
	charHandler := handler.NewCharacterHandler(charRepo, invRepo, equipRepo)

	r := gin.New()
	r.Use(gin.Recovery())
	r.Use(middleware.AuditLog(auditWriter))

	setupRoutes(r, authHandler, tfaHandler, svcHandler, auditHandler, playerHandler, charHandler, jwtManager, authSvc)
	serveStatic(r, logger)

	addr := fmt.Sprintf(":%d", cfg.Server.Port)
	logger.Info("Starting server", zap.String("addr", addr))
	if err := r.Run(addr); err != nil {
		logger.Fatal("Failed to start server", zap.Error(err))
	}
}
