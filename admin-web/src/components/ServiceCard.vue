<template>
  <el-card :class="['service-card', statusClass]" shadow="hover">
    <template #header>
      <div class="card-header">
        <span class="service-name">{{ service.name }}</span>
        <el-tag :type="tagType" size="small">{{ statusText }}</el-tag>
      </div>
    </template>
    <div class="card-body">
      <div class="info-row">
        <span class="label">端口:</span>
        <span class="value">{{ service.port }}</span>
      </div>
      <div class="info-row">
        <span class="label">连接数:</span>
        <span class="value">{{ service.connections }}</span>
      </div>
      <div class="info-row">
        <span class="label">运行时长:</span>
        <span class="value">{{ formatUptime(service.uptime) }}</span>
      </div>
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { ServiceStatus } from '@/api/service'

const props = defineProps<{
  service: ServiceStatus
}>()

const statusClass = computed(() => `status-${props.service.status}`)

const tagType = computed(() => {
  switch (props.service.status) {
    case 'running': return 'success'
    case 'stopped': return 'info'
    case 'error': return 'danger'
    default: return 'info'
  }
})

const statusText = computed(() => {
  switch (props.service.status) {
    case 'running': return '运行中'
    case 'stopped': return '已停止'
    case 'error': return '异常'
    default: return '未知'
  }
})

function formatUptime(seconds: number): string {
  if (!seconds || seconds <= 0) return '-'
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  if (h > 0) return `${h}h ${m}m`
  return `${m}m`
}
</script>

<style scoped>
.service-card {
  min-width: 220px;
}
.service-card.status-error {
  border-color: #f56c6c;
}
.service-card.status-stopped {
  opacity: 0.7;
}
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.service-name {
  font-weight: 600;
}
.info-row {
  display: flex;
  justify-content: space-between;
  margin-bottom: 8px;
}
.info-row:last-child {
  margin-bottom: 0;
}
.label {
  color: #909399;
}
</style>
