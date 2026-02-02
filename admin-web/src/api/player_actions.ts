import request from './index'

export interface KickRequest {
  reason: string
}

export interface BanRequest {
  duration: number
  reason: string
}

export interface MuteRequest {
  duration: number
  reason: string
}

export interface UnmuteRequest {
  reason: string
}

export function kickPlayer(playerId: number, data: KickRequest) {
  return request.post(`/players/${playerId}/kick`, data)
}

export function banPlayer(playerId: number, data: BanRequest) {
  return request.post(`/players/${playerId}/ban`, data)
}

export function unbanPlayer(playerId: number, banId: number, reason: string) {
  return request.post(`/players/${playerId}/unban/${banId}`, { reason })
}

export function mutePlayer(playerId: number, data: MuteRequest) {
  return request.post(`/players/${playerId}/mute`, data)
}

export function unmutePlayer(playerId: number, data: UnmuteRequest) {
  return request.post(`/players/${playerId}/unmute`, data)
}
