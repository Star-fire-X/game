package handler

type LoginVerify2FARequest struct {
	UserID int    `json:"user_id" binding:"required"`
	Code   string `json:"code" binding:"required,len=6"`
}

// Verify2FALogin validates TOTP during login
// POST /api/v1/auth/verify-2fa
func (h *TwoFactorHandler) Verify2FALogin(c *gin.Context) {
	var req LoginVerify2FARequest
	if err := c.ShouldBindJSON(&req); err != nil {
		response.BadRequest(c, "invalid request")
		return
	}

	err := h.tfaSvc.ValidateCode(req.UserID, req.Code)
	if err != nil {
		switch err {
		case service.ErrInvalidTOTPCode:
			response.BadRequest(c, "invalid TOTP code")
		case service.ErrTOTPNotEnabled:
			response.BadRequest(c, "2FA not enabled")
		default:
			response.InternalError(c, "verification failed")
		}
		return
	}

	response.Success(c, gin.H{"verified": true})
}
