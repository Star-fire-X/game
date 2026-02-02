package handler

// Setup2FA generates TOTP secret and QR code
// POST /api/v1/auth/2fa/setup
func (h *TwoFactorHandler) Setup(c *gin.Context) {
	userID, exists := c.Get("user_id")
	if !exists {
		response.Unauthorized(c, "user not found")
		return
	}

	result, err := h.tfaSvc.Setup(userID.(int))
	if err != nil {
		switch err {
		case service.ErrTOTPAlreadyEnabled:
			response.BadRequest(c, "2FA already enabled")
		case service.ErrUserNotFound:
			response.NotFound(c, "user not found")
		default:
			response.InternalError(c, "failed to setup 2FA")
		}
		return
	}

	response.Success(c, result)
}
