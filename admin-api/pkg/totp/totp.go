package totp

import (
	"crypto/rand"
	"encoding/base32"
	"fmt"
	"strings"

	"github.com/pquerna/otp"
	"github.com/pquerna/otp/totp"
	"golang.org/x/crypto/bcrypt"
)

const (
	Issuer          = "MIR2-Admin"
	RecoveryCodeLen = 8
	RecoveryCodeNum = 8
)

type TOTPService struct{}

func NewTOTPService() *TOTPService {
	return &TOTPService{}
}

type SetupResult struct {
	Secret   string `json:"secret"`
	QRCode   string `json:"qr_code"`
	URL      string `json:"url"`
}

// GenerateSecret creates a new TOTP secret for a user
func (s *TOTPService) GenerateSecret(username string) (*SetupResult, error) {
	key, err := totp.Generate(totp.GenerateOpts{
		Issuer:      Issuer,
		AccountName: username,
		SecretSize:  32,
		Algorithm:   otp.AlgorithmSHA1,
		Digits:      otp.DigitsSix,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to generate TOTP key: %w", err)
	}

	return &SetupResult{
		Secret: key.Secret(),
		URL:    key.URL(),
	}, nil
}

// ValidateCode verifies a TOTP code against a secret
func (s *TOTPService) ValidateCode(secret, code string) bool {
	return totp.Validate(code, secret)
}
