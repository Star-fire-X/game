import request from './index'

export interface AlertConfig {
  id: number
  rule_name: string
  rule_type: string
  service_name: string | null
  threshold: number | null
  severity: string
  is_enabled: boolean
  dedup_window_seconds: number
  notify_wechat: boolean
  notify_webhook: boolean
  webhook_url: string | null
}

export function getAlertConfigs() {
  return request.get<AlertConfig[]>('/alerts/configs')
}

export function updateAlertConfig(id: number, data: Partial<AlertConfig>) {
  return request.put(`/alerts/configs/${id}`, data)
}
