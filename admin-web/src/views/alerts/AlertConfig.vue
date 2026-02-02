<template>
  <div class="alert-config">
    <div class="page-header">
      <h2>告警规则配置</h2>
    </div>
    <el-table :data="configs" v-loading="loading" stripe>
      <el-table-column prop="rule_name" label="规则名称" width="180" />
      <el-table-column prop="rule_type" label="类型" width="120" />
      <el-table-column prop="service_name" label="服务" width="100">
        <template #default="{ row }">
          {{ row.service_name || '全部' }}
        </template>
      </el-table-column>
      <el-table-column prop="severity" label="级别" width="80">
        <template #default="{ row }">
          <el-tag :type="severityType(row.severity)" size="small">
            {{ severityText(row.severity) }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column prop="is_enabled" label="状态" width="80">
        <template #default="{ row }">
          <el-switch v-model="row.is_enabled" @change="toggleEnabled(row)" />
        </template>
      </el-table-column>
      <el-table-column label="通知渠道" width="120">
        <template #default="{ row }">
          <span v-if="row.notify_wechat">微信</span>
          <span v-if="row.notify_webhook"> Webhook</span>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="100">
        <template #default="{ row }">
          <el-button link type="primary" @click="editConfig(row)">
            编辑
          </el-button>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import type { AlertConfig } from '@/api/alert'
import { getAlertConfigs, updateAlertConfig } from '@/api/alert'

const configs = ref<AlertConfig[]>([])
const loading = ref(false)

async function fetchConfigs() {
  loading.value = true
  try {
    const res = await getAlertConfigs()
    configs.value = res.data
  } catch (e: any) {
    ElMessage.error('加载失败')
  } finally {
    loading.value = false
  }
}

function severityType(s: string) {
  if (s === 'critical') return 'danger'
  if (s === 'warning') return 'warning'
  return 'info'
}

function severityText(s: string) {
  if (s === 'critical') return '严重'
  if (s === 'warning') return '警告'
  return '信息'
}

async function toggleEnabled(row: AlertConfig) {
  try {
    await updateAlertConfig(row.id, { is_enabled: row.is_enabled })
    ElMessage.success('更新成功')
  } catch {
    ElMessage.error('更新失败')
  }
}

function editConfig(row: AlertConfig) {
  ElMessage.info('编辑功能开发中')
}

onMounted(fetchConfigs)
</script>

<style scoped>
.alert-config {
  padding: 20px;
}
.page-header {
  margin-bottom: 20px;
}
.page-header h2 {
  margin: 0;
}
</style>
