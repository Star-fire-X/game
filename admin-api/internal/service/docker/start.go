package docker

import (
	"context"
	"time"

	"github.com/docker/docker/api/types/container"
)

const DefaultStopTimeout = 30

// StartContainer starts a container by name
func (d *DockerService) StartContainer(ctx context.Context, name string) error {
	return d.client.ContainerStart(ctx, name, container.StartOptions{})
}
