package service

import (
	"context"
	"time"

	"github.com/mir2-cpp/admin-api/internal/model"
)

func (s *AuthService) handleFailedLogin(ctx context.Context, user *model.AdminUser) {
	count, _ := s.redisRepo.IncrLoginFailures(ctx, user.Username)
	s.userRepo.IncrFailedAttempts(user.ID)

	if count >= MaxFailedAttempts {
		lockUntil := time.Now().Add(LockDuration)
		s.userRepo.LockUser(user.ID, lockUntil)
	}
}

func (s *AuthService) Logout(ctx context.Context, jti string, userID int, ttl time.Duration) error {
	if err := s.redisRepo.AddToBlacklist(ctx, jti, ttl); err != nil {
		return err
	}
	return s.redisRepo.DeleteSession(ctx, userID)
}

func (s *AuthService) IsTokenBlacklisted(ctx context.Context, jti string) (bool, error) {
	return s.redisRepo.IsBlacklisted(ctx, jti)
}
