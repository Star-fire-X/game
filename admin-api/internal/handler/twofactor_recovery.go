package handler

type RecoveryCodeRequest struct {
	UserID int    `json:"user_id" binding:"required"`
	Code   string `json:"code" binding:"required"`
}

// VerifyRecoveryCode validates recovery code during login
// POST /api/v1/auth/recovery
func (h *TwoFactorHandler) VerifyRecoveryCode(c *gin.Context) {
	var req RecoveryCodeRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.BadRequest(c, "invalid request")
		return
	}

	err := h.tfaSvc.ValidateRecoveryCode(req.UserID, req.Code)
	if err != nil {
		switch err {
		case service.ErrInvalidRecoveryCode:
			response.BadRequest(c, "invalid recovery code")
		default:
			response.InternalError(c, "verification failed")
		}
		return
	}

	response.Success(c, gin.H{"verified": true})
}
