package handler

type VerifyRequest struct {
	Code string `json:"code" binding:"required,len=6"`
}

// Verify validates TOTP code and enables 2FA
// POST /api/v1/auth/2fa/verify
func (h *TwoFactorHandler) Verify(c *gin.Context) {
	userID, exists := c.Get("user_id")
	if !exists {
		response.Unauthorized(c, "user not found")
		return
	}

	var req VerifyRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.BadRequest(c, "invalid request")
		return
	}

	err := h.tfaSvc.Verify(userID.(int), req.Code)
	if err != nil {
		switch err {
		case service.ErrInvalidTOTPCode:
			response.BadRequest(c, "invalid TOTP code")
		case service.ErrTOTPAlreadyEnabled:
			response.BadRequest(c, "2FA already enabled")
		default:
			response.InternalError(c, "verification failed")
		}
		return
	}

	response.Success(c, gin.H{"message": "2FA enabled"})
}
