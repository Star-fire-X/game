package service

// Verify validates TOTP code and enables 2FA
func (s *TwoFactorService) Verify(userID int, code string) error {
	user, err := s.userRepo.FindByID(userID)
	if err != nil {
		return err
	}
	if user == nil {
		return ErrUserNotFound
	}
	if user.TOTPEnabled {
		return ErrTOTPAlreadyEnabled
	}
	if user.TOTPSecret == nil {
		return ErrTOTPNotEnabled
	}

	if !s.totpService.ValidateCode(*user.TOTPSecret, code) {
		return ErrInvalidTOTPCode
	}

	return s.userRepo.EnableTOTP(userID)
}

// ValidateCode checks if TOTP code is valid for login
func (s *TwoFactorService) ValidateCode(userID int, code string) error {
	user, err := s.userRepo.FindByID(userID)
	if err != nil {
		return err
	}
	if user == nil {
		return ErrUserNotFound
	}
	if !user.TOTPEnabled || user.TOTPSecret == nil {
		return ErrTOTPNotEnabled
	}

	if !s.totpService.ValidateCode(*user.TOTPSecret, code) {
		return ErrInvalidTOTPCode
	}
	return nil
}
