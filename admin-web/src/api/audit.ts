import request from './index'

export interface AuditLog {
  id: number
  operator_id: number
  operator_name: string
  action: string
  target_type: string | null
  target_id: string | null
  target_name: string | null
  details: any
  ip_address: string
  created_at: string
}

export interface AuditQuery {
  operator?: string
  action?: string
  start_time?: string
  end_time?: string
  page?: number
  page_size?: number
}

export function getAuditLogs(query: AuditQuery) {
  return request.get('/audit/logs', { params: query })
}

export function exportAuditLogs(query: AuditQuery) {
  return request.get('/audit/logs/export', {
    params: query,
    responseType: 'blob'
  })
}
