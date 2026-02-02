package handler

import (
	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// StopRequest 停止服务请求
type StopRequest struct {
	Confirmed bool `json:"confirmed"` // 是否已确认（前端二次确认后设为true）
}

// StopResponse 停止服务响应
type StopResponse struct {
	Message     string `json:"message"`
	OnlineCount int64  `json:"online_count"` // 当前在线人数
	NeedConfirm bool   `json:"need_confirm"` // 是否需要前端确认
}

// Stop stops a service container
// POST /api/v1/services/:name/stop
func (h *ServiceHandler) Stop(c *gin.Context) {
	name := c.Param("name")

	var req StopRequest
	_ = c.ShouldBindJSON(&req) // 允许空body

	// 获取在线人数
	onlineCount := h.getOnlinePlayerCount()

	// 如果有在线玩家且未确认，返回需要确认
	if onlineCount > 0 && !req.Confirmed {
		response.Success(c, StopResponse{
			Message:     "service has online players, confirmation required",
			OnlineCount: onlineCount,
			NeedConfirm: true,
		})
		return
	}

	// 执行停止操作
	if err := h.dockerSvc.StopContainer(c.Request.Context(), name); err != nil {
		response.InternalError(c, "failed to stop service")
		return
	}

	response.Success(c, StopResponse{
		Message:     "service stopped",
		OnlineCount: onlineCount,
		NeedConfirm: false,
	})
}

// getOnlinePlayerCount 获取当前在线玩家数量
func (h *ServiceHandler) getOnlinePlayerCount() int64 {
	if h.gameClient == nil {
		return 0
	}

	params := game.OnlinePlayersParams{
		Page:     1,
		PageSize: 1, // 只需要获取总数
	}

	result, err := h.gameClient.GetOnlinePlayers(params)
	if err != nil {
		return 0
	}

	return result.Total
}
