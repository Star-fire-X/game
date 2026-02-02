package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// ListOnline 获取在线玩家列表
func (h *PlayerHandler) ListOnline(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("page_size", "20"))
	if pageSize > 100 {
		pageSize = 100
	}

	params := game.OnlinePlayersParams{
		Page:     page,
		PageSize: pageSize,
		Keyword:  c.Query("keyword"),
	}
	if v := c.Query("min_level"); v != "" {
		params.MinLevel, _ = strconv.Atoi(v)
	}
	if v := c.Query("max_level"); v != "" {
		params.MaxLevel, _ = strconv.Atoi(v)
	}
	if v := c.Query("map_id"); v != "" {
		params.MapID, _ = strconv.Atoi(v)
	}

	result, err := h.gameClient.GetOnlinePlayers(params)
	if err != nil {
		response.Error(c, http.StatusServiceUnavailable, "game service unavailable")
		return
	}

	response.Success(c, result)
}
