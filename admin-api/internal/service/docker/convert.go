package docker

import (
	"strings"

	"github.com/docker/docker/api/types"
)

func (d *DockerService) toContainerInfoList(containers []types.Container) []ContainerInfo {
	result := make([]ContainerInfo, len(containers))
	for i, c := range containers {
		name := ""
		if len(c.Names) > 0 {
			name = strings.TrimPrefix(c.Names[0], "/")
		}
		result[i] = ContainerInfo{
			ID:      c.ID[:12],
			Name:    name,
			Image:   c.Image,
			Status:  c.Status,
			State:   c.State,
			Created: c.Created,
		}
	}
	return result
}
