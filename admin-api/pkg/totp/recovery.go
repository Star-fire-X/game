package totp

import (
	"crypto/rand"
	"encoding/base32"
	"strings"

	"golang.org/x/crypto/bcrypt"
)

// GenerateRecoveryCodes creates a set of recovery codes
func (s *TOTPService) GenerateRecoveryCodes() ([]string, []string, error) {
	codes := make([]string, RecoveryCodeNum)
	hashedCodes := make([]string, RecoveryCodeNum)

	for i := 0; i < RecoveryCodeNum; i++ {
		code, err := generateRandomCode(RecoveryCodeLen)
		if err != nil {
			return nil, nil, err
		}
		codes[i] = code

		hashed, err := bcrypt.GenerateFromPassword([]byte(code), bcrypt.DefaultCost)
		if err != nil {
			return nil, nil, err
		}
		hashedCodes[i] = string(hashed)
	}

	return codes, hashedCodes, nil
}

// ValidateRecoveryCode checks if a recovery code is valid
func (s *TOTPService) ValidateRecoveryCode(code string, hashedCodes []string) (int, bool) {
	for i, hashed := range hashedCodes {
		if bcrypt.CompareHashAndPassword([]byte(hashed), []byte(code)) == nil {
			return i, true
		}
	}
	return -1, false
}

func generateRandomCode(length int) (string, error) {
	bytes := make([]byte, length)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}
	code := base32.StdEncoding.EncodeToString(bytes)
	code = strings.ToUpper(code[:length])
	return formatCode(code), nil
}

func formatCode(code string) string {
	if len(code) <= 4 {
		return code
	}
	return code[:4] + "-" + code[4:]
}
