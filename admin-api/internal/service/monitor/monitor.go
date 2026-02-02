package monitor

import (
	"context"
	"sync"
	"time"

	"github.com/redis/go-redis/v9"
	"go.uber.org/zap"
)

type Monitor struct {
	interval time.Duration
	services []ServiceConfig
	redisCli *redis.Client
	logger   *zap.Logger
	cache    map[string]*ServiceStatus
	mu       sync.RWMutex
	cancel   context.CancelFunc
}

func NewMonitor(redisCli *redis.Client, logger *zap.Logger) *Monitor {
	return &Monitor{
		interval: DefaultInterval,
		redisCli: redisCli,
		logger:   logger,
		cache:    make(map[string]*ServiceStatus),
	}
}
