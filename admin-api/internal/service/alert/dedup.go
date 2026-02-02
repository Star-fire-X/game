package alert

import (
	"context"
	"crypto/md5"
	"fmt"
	"time"

	"github.com/redis/go-redis/v9"
	"go.uber.org/zap"
)

// Deduplicator handles alert deduplication
type Deduplicator struct {
	redisCli *redis.Client
	logger   *zap.Logger
}

// NewDeduplicator creates a new deduplicator
func NewDeduplicator(redisCli *redis.Client, logger *zap.Logger) *Deduplicator {
	return &Deduplicator{
		redisCli: redisCli,
		logger:   logger,
	}
}
