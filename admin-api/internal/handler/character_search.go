package handler

import (
	"net/http"
	"strconv"

	"github.com/gin-gonic/gin"
	"github.com/mir2-cpp/admin-api/pkg/response"
)

// Search 搜索角色
func (h *CharacterHandler) Search(c *gin.Context) {
	page, _ := strconv.Atoi(c.DefaultQuery("page", "1"))
	pageSize, _ := strconv.Atoi(c.DefaultQuery("page_size", "20"))
	keyword := c.Query("keyword")

	chars, total, err := h.charRepo.SearchCharacters(keyword, page, pageSize)
	if err != nil {
		response.Error(c, http.StatusInternalServerError, "search failed")
		return
	}

	response.Success(c, gin.H{
		"total":      total,
		"page":       page,
		"page_size":  pageSize,
		"characters": chars,
	})
}
