package repository

import (
	"context"
	"fmt"
	"time"

	"github.com/mir2-cpp/admin-api/internal/config"
	"github.com/redis/go-redis/v9"
)

var rdb *redis.Client

func InitRedis(cfg *config.RedisConfig) error {
	rdb = redis.NewClient(&redis.Options{
		Addr:     fmt.Sprintf("%s:%d", cfg.Host, cfg.Port),
		Password: cfg.Password,
		DB:       cfg.DB,
		PoolSize: 10,
	})

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	return rdb.Ping(ctx).Err()
}

func GetRedis() *redis.Client {
	return rdb
}
