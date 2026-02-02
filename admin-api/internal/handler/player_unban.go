package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// UnbanRequest 解封请求
type UnbanRequest struct {
	Reason string `json:"reason" binding:"required,min=5"`
}

// Unban 解封账号
func (h *PlayerHandler) Unban(c *gin.Context) {
	banID, err := strconv.ParseInt(c.Param("ban_id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, "invalid ban id")
		return
	}

	var req UnbanRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.Error(c, http.StatusBadRequest, "reason required")
		return
	}

	user := GetCurrentUser(c)
	if err := h.banRepo.LiftAccountBan(banID, user.ID, req.Reason); err != nil {
		response.Error(c, http.StatusInternalServerError, "unban failed")
		return
	}

	response.Success(c, gin.H{"message": "unban success"})
}
