package jwt

import (
	"errors"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/google/uuid"
)

var (
	ErrInvalidToken = errors.New("invalid token")
	ErrExpiredToken = errors.New("token expired")
)

type Claims struct {
	jwt.RegisteredClaims
	UserID   int    `json:"user_id"`
	Username string `json:"username"`
	Role     string `json:"role"`
}

type JWTManager struct {
	secret     []byte
	expireHour int
}

func NewJWTManager(secret string, expireHour int) *JWTManager {
	return &JWTManager{
		secret:     []byte(secret),
		expireHour: expireHour,
	}
}
