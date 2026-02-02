package docker

import (
	"context"
	"fmt"

	"github.com/docker/docker/api/types/container"
	"github.com/docker/docker/api/types/filters"
)

// GetComposeServices returns containers by compose project
func (d *DockerService) GetComposeServices(ctx context.Context, project string) ([]ContainerInfo, error) {
	f := filters.NewArgs()
	f.Add("label", fmt.Sprintf("com.docker.compose.project=%s", project))

	containers, err := d.client.ContainerList(ctx, container.ListOptions{
		All:     true,
		Filters: f,
	})
	if err != nil {
		return nil, err
	}
	return d.toContainerInfoList(containers), nil
}
