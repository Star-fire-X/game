package service

import (
	"context"
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
	"github.com/mir2-cpp/admin-api/internal/repository"
	"golang.org/x/crypto/bcrypt"
)

type LoginResult struct {
	Token string           `json:"token"`
	User  *model.AdminUser `json:"user"`
}

func (s *AuthService) Login(ctx context.Context, username, password, ip string) (*LoginResult, error) {
	user, err := s.userRepo.FindByUsername(username)
	if err != nil {
		return nil, err
	}
	if user == nil {
		return nil, ErrUserNotFound
	}

	if !user.IsActive {
		return nil, ErrAccountDisabled
	}

	if user.LockedUntil != nil && user.LockedUntil.After(time.Now()) {
		return nil, ErrAccountLocked
	}

	if err := bcrypt.CompareHashAndPassword([]byte(user.PasswordHash), []byte(password)); err != nil {
		s.handleFailedLogin(ctx, user)
		return nil, ErrInvalidPassword
	}

	s.redisRepo.ClearLoginFailures(ctx, username)
	s.userRepo.UpdateLoginInfo(user.ID, ip)

	token, jti, err := s.jwtManager.Generate(user.ID, user.Username, user.Role)
	if err != nil {
		return nil, err
	}

	s.redisRepo.SetSession(ctx, user.ID, &repository.SessionData{
		TokenJTI:  jti,
		LoginTime: time.Now(),
		IP:        ip,
	})

	return &LoginResult{Token: token, User: user}, nil
}
