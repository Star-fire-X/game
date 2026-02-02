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

// BanRequest 封号请求
type BanRequest struct {
	Duration int    `json:"duration" binding:"required"` // seconds, 0=permanent
	Reason   string `json:"reason" binding:"required,min=10"`
}

// Ban 封禁账号
func (h *PlayerHandler) Ban(c *gin.Context) {
	playerID, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, "invalid player id")
		return
	}

	var req BanRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, "invalid request")
		return
	}

	user := GetCurrentUser(c)

	// 创建封禁记录
	ban := &model.AccountBan{
		AccountID:       playerID,
		BanType:         "ban",
		DurationSeconds: req.Duration,
		Reason:          req.Reason,
		OperatorID:      user.ID,
		OperatorName:    user.Username,
	}
	if req.Duration > 0 {
		exp := time.Now().Add(time.Duration(req.Duration) * time.Second)
		ban.ExpiresAt = &exp
	}

	if err := h.banRepo.CreateAccountBan(ban); err != nil {
		response.Error(c, http.StatusInternalServerError, "create ban failed")
		return
	}

	// 踢出在线玩家
	kickReq := &game.KickRequest{
		Reason:     "账号已被封禁: " + req.Reason,
		Operator:   user.Username,
		OperatorID: user.ID,
	}
	h.gameClient.KickPlayer(playerID, kickReq)

	response.Success(c, gin.H{
		"ban_id":     ban.ID,
		"expires_at": ban.ExpiresAt,
	})
}
