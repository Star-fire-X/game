package handler

import (
	"github.com/mir2-cpp/admin-api/internal/service/docker"
	"github.com/mir2-cpp/admin-api/internal/service/game"
	"github.com/mir2-cpp/admin-api/internal/service/monitor"
)

type ServiceHandler struct {
	dockerSvc  *docker.DockerService
	monitorSvc *monitor.Monitor
	gameClient *game.Client
}

func NewServiceHandler(dockerSvc *docker.DockerService, monitorSvc *monitor.Monitor, gameClient *game.Client) *ServiceHandler {
	return &ServiceHandler{
		dockerSvc:  dockerSvc,
		monitorSvc: monitorSvc,
		gameClient: gameClient,
	}
}
