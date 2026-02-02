package monitor

// SetServices configures services to monitor
func (m *Monitor) SetServices(services []ServiceConfig) {
	m.services = services
}

// SetInterval changes polling interval
func (m *Monitor) SetInterval(d time.Duration) {
	m.interval = d
}
