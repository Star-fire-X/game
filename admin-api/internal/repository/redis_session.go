package repository

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"
)

func (r *RedisRepo) AddToBlacklist(ctx context.Context, jti string, ttl time.Duration) error {
	key := fmt.Sprintf(KeyJWTBlacklist, jti)
	return r.client.Set(ctx, key, "1", ttl).Err()
}

func (r *RedisRepo) IsBlacklisted(ctx context.Context, jti string) (bool, error) {
	key := fmt.Sprintf(KeyJWTBlacklist, jti)
	result, err := r.client.Exists(ctx, key).Result()
	if err != nil {
		return false, err
	}
	return result > 0, nil
}

type SessionData struct {
	TokenJTI  string    `json:"token_jti"`
	LoginTime time.Time `json:"login_time"`
	IP        string    `json:"ip"`
}

func (r *RedisRepo) SetSession(ctx context.Context, userID int, data *SessionData) error {
	key := fmt.Sprintf(KeySession, userID)
	jsonData, err := json.Marshal(data)
	if err != nil {
		return err
	}
	return r.client.Set(ctx, key, jsonData, 30*time.Minute).Err()
}

func (r *RedisRepo) GetSession(ctx context.Context, userID int) (*SessionData, error) {
	key := fmt.Sprintf(KeySession, userID)
	result, err := r.client.Get(ctx, key).Result()
	if err == redis.Nil {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	var data SessionData
	if err := json.Unmarshal([]byte(result), &data); err != nil {
		return nil, err
	}
	return &data, nil
}

func (r *RedisRepo) RefreshSession(ctx context.Context, userID int) error {
	key := fmt.Sprintf(KeySession, userID)
	return r.client.Expire(ctx, key, 30*time.Minute).Err()
}

func (r *RedisRepo) DeleteSession(ctx context.Context, userID int) error {
	key := fmt.Sprintf(KeySession, userID)
	return r.client.Del(ctx, key).Err()
}
