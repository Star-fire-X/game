package service

import (
	"errors"

	"github.com/lib/pq"
	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/totp"
)

var (
	ErrTOTPAlreadyEnabled  = errors.New("2FA already enabled")
	ErrTOTPNotEnabled      = errors.New("2FA not enabled")
	ErrInvalidTOTPCode     = errors.New("invalid TOTP code")
	ErrInvalidRecoveryCode = errors.New("invalid recovery code")
)

type TwoFactorService struct {
	userRepo    *repository.UserRepo
	totpService *totp.TOTPService
}

func NewTwoFactorService() *TwoFactorService {
	return &TwoFactorService{
		userRepo:    repository.NewUserRepo(),
		totpService: totp.NewTOTPService(),
	}
}

type SetupResponse struct {
	Secret        string   `json:"secret"`
	QRCode        string   `json:"qr_code"`
	RecoveryCodes []string `json:"recovery_codes"`
}

// Setup generates TOTP secret and recovery codes for a user
func (s *TwoFactorService) Setup(userID int) (*SetupResponse, error) {
	user, err := s.userRepo.FindByID(userID)
	if err != nil {
		return nil, err
	}
	if user == nil {
		return nil, ErrUserNotFound
	}
	if user.TOTPEnabled {
		return nil, ErrTOTPAlreadyEnabled
	}

	result, err := s.totpService.GenerateSecret(user.Username)
	if err != nil {
		return nil, err
	}

	qrCode, err := s.totpService.GenerateQRCode(result.URL)
	if err != nil {
		return nil, err
	}

	codes, hashedCodes, err := s.totpService.GenerateRecoveryCodes()
	if err != nil {
		return nil, err
	}

	// Store secret and hashed recovery codes temporarily
	err = s.userRepo.UpdateTOTPSetup(userID, result.Secret, hashedCodes)
	if err != nil {
		return nil, err
	}

	return &SetupResponse{
		Secret:        result.Secret,
		QRCode:        qrCode,
		RecoveryCodes: codes,
	}, nil
}
