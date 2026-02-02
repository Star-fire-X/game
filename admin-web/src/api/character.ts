import request from './index'

export interface Character {
  id: number
  account_id: number
  name: string
  level: number
  class: number
  gender: number
  hp: number
  max_hp: number
  mp: number
  max_mp: number
  experience: number
  gold: number
  map_id: number
  x: number
  y: number
  created_at: string
  last_login_at: string
}

export interface CharacterSearchResponse {
  total: number
  page: number
  page_size: number
  characters: Character[]
}

export interface InventoryItem {
  id: number
  character_id: number
  slot: number
  item_template_id: number
  instance_id: number
  quantity: number
  durability: number
  enhancement_level: number
}

export interface EquipmentItem {
  id: number
  character_id: number
  slot: number
  item_template_id: number
  instance_id: number
  durability: number
  enhancement_level: number
}

export interface InventoryResponse {
  character_id: number
  items: InventoryItem[]
  total: number
}

export interface EquipmentResponse {
  character_id: number
  items: EquipmentItem[]
  total: number
}

export function searchCharacters(params: { keyword?: string; page?: number; page_size?: number }) {
  return request.get<CharacterSearchResponse>('/characters/search', { params })
}

export function getCharacterById(id: number) {
  return request.get<Character>(`/characters/${id}`)
}

export function getCharacterInventory(id: number) {
  return request.get<InventoryResponse>(`/characters/${id}/inventory`)
}

export function getCharacterEquipment(id: number) {
  return request.get<EquipmentResponse>(`/characters/${id}/equipment`)
}
