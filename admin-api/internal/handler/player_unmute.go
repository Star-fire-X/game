package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// UnmuteRequest 解除禁言请求
type UnmuteRequest struct {
	Reason string `json:"reason" binding:"required,min=5"`
}

// Unmute 解除禁言
func (h *PlayerHandler) Unmute(c *gin.Context) {
	playerID, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, "invalid player id")
		return
	}

	var req UnmuteRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, "reason required")
		return
	}

	user := GetCurrentUser(c)
	gameReq := &game.UnmuteRequest{
		Reason:     req.Reason,
		Operator:   user.Username,
		OperatorID: user.ID,
	}

	result, err := h.gameClient.UnmutePlayer(playerID, gameReq)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, err.Error())
		return
	}

	response.Success(c, result)
}
