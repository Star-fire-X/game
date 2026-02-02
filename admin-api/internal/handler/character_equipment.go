package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// GetEquipment 获取角色装备
func (h *CharacterHandler) GetEquipment(c *gin.Context) {
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

	items, err := h.equipRepo.GetByCharacterID(id)
	if err != nil {
		response.InternalError(c, "failed to get equipment")
		return
	}

	response.Success(c, gin.H{
		"character_id": id,
		"items":        items,
		"total":        len(items),
	})
}
