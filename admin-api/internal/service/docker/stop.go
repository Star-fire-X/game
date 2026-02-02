package docker

import (
	"context"

	"github.com/docker/docker/api/types/container"
)

// StopContainer stops a container by name
func (d *DockerService) StopContainer(ctx context.Context, name string) error {
	timeout := DefaultStopTimeout
	return d.client.ContainerStop(ctx, name, container.StopOptions{
		Timeout: &timeout,
	})
}
