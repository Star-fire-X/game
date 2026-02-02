package service

// ValidateRecoveryCode checks recovery code and removes it after use
func (s *TwoFactorService) ValidateRecoveryCode(userID int, code string) error {
	user, err := s.userRepo.FindByID(userID)
	if err != nil {
		return err
	}
	if user == nil {
		return ErrUserNotFound
	}

	idx, valid := s.totpService.ValidateRecoveryCode(code, user.RecoveryCodes)
	if !valid {
		return ErrInvalidRecoveryCode
	}

	// Remove used recovery code
	newCodes := make([]string, 0, len(user.RecoveryCodes)-1)
	for i, c := range user.RecoveryCodes {
		if i != idx {
			newCodes = append(newCodes, c)
		}
	}

	return s.userRepo.UpdateRecoveryCodes(userID, newCodes)
}

// Disable removes 2FA from user account
func (s *TwoFactorService) Disable(userID int) error {
	return s.userRepo.DisableTOTP(userID)
}
