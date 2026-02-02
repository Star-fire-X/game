package monitor

import (
	"context"
	"encoding/json"
	"fmt"
	"time"
)

func (m *Monitor) updateCache(name string, status *ServiceStatus) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.cache[name] = status
}

func (m *Monitor) updateRedis(ctx context.Context, name string, status *ServiceStatus) {
	key := fmt.Sprintf("service:status:%s", name)
	data, err := json.Marshal(status)
	if err != nil {
		m.logger.Error("Failed to marshal status", zap.String("service", name), zap.Error(err))
		return
	}
	m.redisCli.Set(ctx, key, data, 15*time.Second)
}
