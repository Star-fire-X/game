import request from './index'

export interface AlertHistory {
  id: number
  rule_name: string
  service_name: string | null
  severity: string
  message: string
  created_at: string
  resolved_at: string | null
}

export interface AlertHistoryQuery {
  service?: string
  severity?: string
  start_time?: string
  end_time?: string
  page?: number
  page_size?: number
}

export function getAlertHistory(query: AlertHistoryQuery) {
  return request.get('/alerts/history', { params: query })
}
