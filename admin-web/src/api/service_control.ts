import request from './index'

export function startService(name: string) {
  return request.post(`/services/${name}/start`)
}

export function stopService(name: string) {
  return request.post(`/services/${name}/stop`)
}

export function restartService(name: string) {
  return request.post(`/services/${name}/restart`)
}
