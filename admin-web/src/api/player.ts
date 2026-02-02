import request from './index'

export interface OnlinePlayer {
  id: number
  account_id: number
  name: string
  level: number
  class: number
  class_name: string
  gender: number
  map_id: number
  map_name: string
  x: number
  y: number
  hp: number
  max_hp: number
  mp: number
  max_mp: number
  login_time: string
  online_duration: number
  ip_address: string
  is_muted: boolean
  mute_until: string | null
}

export interface OnlinePlayersResponse {
  total: number
  page: number
  page_size: number
  players: OnlinePlayer[]
}

export interface OnlinePlayersParams {
  page?: number
  page_size?: number
  keyword?: string
  min_level?: number
  max_level?: number
  map_id?: number
}

export function getOnlinePlayers(params: OnlinePlayersParams) {
  return request.get<OnlinePlayersResponse>('/players/online', { params })
}
