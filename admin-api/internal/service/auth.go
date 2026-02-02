package service

import (
	"context"
	"errors"
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"github.com/mir2-cpp/admin-api/pkg/jwt"
	"golang.org/x/crypto/bcrypt"
)

var (
	ErrUserNotFound    = errors.New("user not found")
	ErrInvalidPassword = errors.New("invalid password")
	ErrAccountLocked   = errors.New("account locked")
	ErrAccountDisabled = errors.New("account disabled")
)

const (
	MaxFailedAttempts = 5
	LockDuration      = 15 * time.Minute
)

type AuthService struct {
	userRepo   *repository.UserRepo
	redisRepo  *repository.RedisRepo
	jwtManager *jwt.JWTManager
}

func NewAuthService(jwtManager *jwt.JWTManager) *AuthService {
	return &AuthService{
		userRepo:   repository.NewUserRepo(),
		redisRepo:  repository.NewRedisRepo(),
		jwtManager: jwtManager,
	}
}
