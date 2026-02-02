package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// GetByID 获取角色详情
func (h *CharacterHandler) GetByID(c *gin.Context) {
	id, err := strconv.ParseInt(c.Param("id"), 10, 64)
	if err != nil {
		response.Error(c, http.StatusBadRequest, "invalid id")
		return
	}

	char, err := h.charRepo.GetCharacterByID(id)
	if err != nil {
		response.Error(c, http.StatusNotFound, "character not found")
		return
	}

	response.Success(c, char)
}
