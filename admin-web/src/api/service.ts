import request from './index'

export interface ServiceStatus {
  name: string
  status: 'running' | 'stopped' | 'error'
  uptime: number
  port: number
  connections: number
  cpu_percent?: number
  memory_mb?: number
  last_check: string
}

export function getServices() {
  return request.get<ServiceStatus[]>('/services')
}

export function getService(name: string) {
  return request.get<ServiceStatus>(`/services/${name}`)
}
