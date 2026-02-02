package alert

import (
	"context"
	"crypto/md5"
	"fmt"
	"time"
)

// IsDuplicate checks if an alert is a duplicate within the window
func (d *Deduplicator) IsDuplicate(ctx context.Context, ruleID int, serviceName, message string, windowSeconds int) bool {
	key := d.buildKey(ruleID, serviceName, message)

	exists, err := d.redisCli.Exists(ctx, key).Result()
	if err != nil {
		d.logger.Warn("dedup check failed",
			zap.Int("rule_id", ruleID),
			zap.Error(err))
		return false
	}

	return exists > 0
}

// MarkSent marks an alert as sent for deduplication
func (d *Deduplicator) MarkSent(ctx context.Context, ruleID int, serviceName, message string, windowSeconds int) error {
	key := d.buildKey(ruleID, serviceName, message)
	ttl := time.Duration(windowSeconds) * time.Second

	return d.redisCli.Set(ctx, key, "1", ttl).Err()
}

func (d *Deduplicator) buildKey(ruleID int, serviceName, message string) string {
	hash := md5.Sum([]byte(fmt.Sprintf("%d:%s:%s", ruleID, serviceName, message)))
	return fmt.Sprintf("alert:dedup:%x", hash)
}
