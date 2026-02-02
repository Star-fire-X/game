package docker

import (
	"context"

	"github.com/docker/docker/api/types/container"
)

// RestartContainer restarts a container by name
func (d *DockerService) RestartContainer(ctx context.Context, name string) error {
	timeout := DefaultStopTimeout
	return d.client.ContainerRestart(ctx, name, container.StopOptions{
		Timeout: &timeout,
	})
}
