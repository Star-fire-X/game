package handler

import (
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/internal/service/game"
)

// PlayerHandler 玩家管理处理器
type PlayerHandler struct {
	gameClient *game.Client
	banRepo    *repository.BanRepository
}

// NewPlayerHandler 创建玩家管理处理器
func NewPlayerHandler(gameClient *game.Client, banRepo *repository.BanRepository) *PlayerHandler {
	return &PlayerHandler{
		gameClient: gameClient,
		banRepo:    banRepo,
	}
}
