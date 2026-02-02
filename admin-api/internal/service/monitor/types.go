package monitor

import (
	"context"
	"sync"
	"time"

	"go.uber.org/zap"
)

const (
	DefaultInterval = 10 * time.Second
	HealthTimeout   = 3 * time.Second
)

type ServiceStatus struct {
	Name        string    `json:"name"`
	Status      string    `json:"status"`
	ContainerID string    `json:"container_id,omitempty"`
	Uptime      int64     `json:"uptime"`
	Connections int       `json:"connections"`
	LastCheck   time.Time `json:"last_check"`
	Error       string    `json:"error,omitempty"`
}

type ServiceConfig struct {
	Name          string
	ContainerName string
	Address       string
	HealthPath    string
}
