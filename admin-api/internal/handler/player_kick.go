package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// KickRequest 踢人请求
type KickRequest struct {
	Reason string `json:"reason" binding:"required,min=5"`
}

// Kick 踢出玩家
func (h *PlayerHandler) Kick(c *gin.Context) {
	playerID, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, "invalid player id")
		return
	}

	var req KickRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, "reason is required (min 5 chars)")
		return
	}

	user := GetCurrentUser(c)
	gameReq := &game.KickRequest{
		Reason:     req.Reason,
		Operator:   user.Username,
		OperatorID: user.ID,
	}

	result, err := h.gameClient.KickPlayer(playerID, gameReq)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, err.Error())
		return
	}

	response.Success(c, result)
}
