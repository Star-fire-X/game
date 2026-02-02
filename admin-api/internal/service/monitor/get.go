package monitor

// GetStatus returns cached status for a service
func (m *Monitor) GetStatus(name string) *ServiceStatus {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.cache[name]
}

// GetAllStatus returns all cached statuses
func (m *Monitor) GetAllStatus() map[string]*ServiceStatus {
	m.mu.RLock()
	defer m.mu.RUnlock()
	result := make(map[string]*ServiceStatus)
	for k, v := range m.cache {
		result[k] = v
	}
	return result
}
