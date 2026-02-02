package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// GetInventory 获取角色背包
func (h *CharacterHandler) GetInventory(c *gin.Context) {
	id, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		response.BadRequest(c, "invalid character id")
		return
	}

	// 验证角色存在
	_, err = h.charRepo.GetCharacterByID(id)
	if err != nil {
		response.NotFound(c, "character not found")
		return
	}

	items, err := h.invRepo.GetByCharacterID(id)
	if err != nil {
		response.InternalError(c, "failed to get inventory")
		return
	}

	response.Success(c, gin.H{
		"character_id": id,
		"items":        items,
		"total":        len(items),
	})
}
