package monitor

import (
	"context"
	"time"
)

// Start begins the monitoring goroutine
func (m *Monitor) Start(ctx context.Context) {
	ctx, m.cancel = context.WithCancel(ctx)
	ticker := time.NewTicker(m.interval)

	go func() {
		defer ticker.Stop()
		m.checkAllServices(ctx)

		for {
			select {
			case <-ctx.Done():
				m.logger.Info("Monitor stopped")
				return
			case <-ticker.C:
				m.checkAllServices(ctx)
			}
		}
	}()

	m.logger.Info("Monitor started",
		zap.Duration("interval", m.interval),
		zap.Int("services", len(m.services)))
}

// Stop halts the monitoring goroutine
func (m *Monitor) Stop() {
	if m.cancel != nil {
		m.cancel()
	}
}
