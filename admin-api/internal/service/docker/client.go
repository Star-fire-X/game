package docker

import (
	"context"
)

// Close releases docker client resources
func (d *DockerService) Close() error {
	return d.client.Close()
}

// Ping checks docker daemon connectivity
func (d *DockerService) Ping(ctx context.Context) error {
	_, err := d.client.Ping(ctx)
	return err
}
