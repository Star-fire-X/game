package monitor

import (
	"testing"
)

func TestServiceStatus(t *testing.T) {
	status := &ServiceStatus{
		Name:   "test",
		Status: "up",
	}
	if status.Name != "test" {
		t.Error("Name mismatch")
	}
	if status.Status != "up" {
		t.Error("Status mismatch")
	}
}
