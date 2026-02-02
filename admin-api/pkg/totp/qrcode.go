package totp

import (
	"encoding/base64"

	qrcode "github.com/skip2/go-qrcode"
)

// GenerateQRCode creates a base64 encoded QR code image
func (s *TOTPService) GenerateQRCode(url string) (string, error) {
	png, err := qrcode.Encode(url, qrcode.Medium, 256)
	if err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(png), nil
}
