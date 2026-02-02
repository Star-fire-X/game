package repository

import (
	"context"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"
)

const (
	KeyLoginFailures = "login:failures:%s"
	KeyJWTBlacklist  = "jwt:blacklist:%s"
	KeySession       = "session:%d"
)

type RedisRepo struct {
	client *redis.Client
}

func NewRedisRepo() *RedisRepo {
	return &RedisRepo{client: rdb}
}

func (r *RedisRepo) IncrLoginFailures(ctx context.Context, username string) (int64, error) {
	key := fmt.Sprintf(KeyLoginFailures, username)
	count, err := r.client.Incr(ctx, key).Result()
	if err != nil {
		return 0, err
	}
	r.client.Expire(ctx, key, 15*time.Minute)
	return count, nil
}

func (r *RedisRepo) GetLoginFailures(ctx context.Context, username string) (int64, error) {
	key := fmt.Sprintf(KeyLoginFailures, username)
	return r.client.Get(ctx, key).Int64()
}

func (r *RedisRepo) ClearLoginFailures(ctx context.Context, username string) error {
	key := fmt.Sprintf(KeyLoginFailures, username)
	return r.client.Del(ctx, key).Err()
}
