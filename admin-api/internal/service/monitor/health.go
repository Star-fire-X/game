package monitor

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

type HealthResponse struct {
	Status      string `json:"status"`
	Uptime      int64  `json:"uptime"`
	Connections int    `json:"connections"`
}

func (m *Monitor) checkHealth(svc ServiceConfig, status *ServiceStatus) *ServiceStatus {
	client := &http.Client{Timeout: HealthTimeout}
	url := fmt.Sprintf("http://%s%s", svc.Address, svc.HealthPath)

	resp, err := client.Get(url)
	if err != nil {
		status.Status = "degraded"
		status.Error = fmt.Sprintf("health check failed: %v", err)
		return status
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		status.Status = "degraded"
		status.Error = fmt.Sprintf("health returned %d", resp.StatusCode)
		return status
	}

	var health HealthResponse
	if err := json.NewDecoder(resp.Body).Decode(&health); err == nil {
		status.Uptime = health.Uptime
		status.Connections = health.Connections
	}

	status.Status = "up"
	return status
}
