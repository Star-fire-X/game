package handler

import (
	"net/http"
	"strconv"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// MuteRequest 禁言请求
type MuteRequest struct {
	Duration int    `json:"duration" binding:"required"`
	Reason   string `json:"reason" binding:"required,min=5"`
}

// Mute 禁言玩家
func (h *PlayerHandler) Mute(c *gin.Context) {
	playerID, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, "invalid player id")
		return
	}

	var req MuteRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, "invalid request")
		return
	}

	user := GetCurrentUser(c)

	// 调用游戏服务禁言
	gameReq := &game.MuteRequest{
		Duration:   req.Duration,
		Reason:     req.Reason,
		Operator:   user.Username,
		OperatorID: user.ID,
	}

	result, err := h.gameClient.MutePlayer(playerID, gameReq)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, err.Error())
		return
	}

	// 记录到数据库
	ban := &model.CharacterBan{
		CharacterID:     playerID,
		CharacterName:   result.PlayerName,
		BanType:         "mute",
		DurationSeconds: req.Duration,
		Reason:          req.Reason,
		OperatorID:      user.ID,
		OperatorName:    user.Username,
	}
	if req.Duration > 0 {
		exp := time.Now().Add(time.Duration(req.Duration) * time.Second)
		ban.ExpiresAt = &exp
	}
	h.banRepo.CreateCharacterBan(ban)

	response.Success(c, result)
}
