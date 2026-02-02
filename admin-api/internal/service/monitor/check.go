package monitor

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"time"
)

func (m *Monitor) checkAllServices(ctx context.Context) {
	for _, svc := range m.services {
		status := m.checkService(ctx, svc)
		m.updateCache(svc.Name, status)
		m.updateRedis(ctx, svc.Name, status)
	}
}

func (m *Monitor) checkService(ctx context.Context, svc ServiceConfig) *ServiceStatus {
	status := &ServiceStatus{
		Name:      svc.Name,
		Status:    "unknown",
		LastCheck: time.Now(),
	}

	// Check port connectivity
	conn, err := net.DialTimeout("tcp", svc.Address, HealthTimeout)
	if err != nil {
		status.Status = "down"
		status.Error = fmt.Sprintf("connection failed: %v", err)
		return status
	}
	conn.Close()

	// Check health endpoint
	if svc.HealthPath != "" {
		status = m.checkHealth(svc, status)
	} else {
		status.Status = "up"
	}

	return status
}
