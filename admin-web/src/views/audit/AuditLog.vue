<template>
  <div class="audit-log">
    <div class="page-header">
      <h2>审计日志</h2>
      <el-button type="primary" @click="handleExport">导出 CSV</el-button>
    </div>
    <el-form :inline="true" class="filter-form">
      <el-form-item label="操作人">
        <el-input v-model="query.operator" placeholder="用户名" clearable />
      </el-form-item>
      <el-form-item label="操作类型">
        <el-select v-model="query.action" placeholder="全部" clearable>
          <el-option label="登录" value="login" />
          <el-option label="登出" value="logout" />
          <el-option label="服务控制" value="service_control" />
          <el-option label="玩家管理" value="player_manage" />
        </el-select>
      </el-form-item>
      <el-form-item label="时间范围">
        <el-date-picker
          v-model="dateRange"
          type="daterange"
          range-separator="至"
          start-placeholder="开始日期"
          end-placeholder="结束日期"
        />
      </el-form-item>
      <el-form-item>
        <el-button type="primary" @click="search">查询</el-button>
      </el-form-item>
    </el-form>
    <el-table :data="logs" v-loading="loading" stripe>
      <el-table-column prop="created_at" label="时间" width="180">
        <template #default="{ row }">
          {{ formatTime(row.created_at) }}
        </template>
      </el-table-column>
      <el-table-column prop="operator_name" label="操作人" width="120" />
      <el-table-column prop="action" label="操作" width="120" />
      <el-table-column prop="target_name" label="目标" width="150" />
      <el-table-column prop="ip_address" label="IP" width="140" />
      <el-table-column prop="details" label="详情">
        <template #default="{ row }">
          {{ JSON.stringify(row.details) }}
        </template>
      </el-table-column>
    </el-table>
    <el-pagination
      v-model:current-page="query.page"
      v-model:page-size="query.page_size"
      :total="total"
      layout="total, prev, pager, next"
      @current-change="fetchLogs"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import type { AuditLog } from '@/api/audit'
import { getAuditLogs, exportAuditLogs } from '@/api/audit'

const logs = ref<AuditLog[]>([])
const loading = ref(false)
const total = ref(0)
const dateRange = ref<[Date, Date] | null>(null)

const query = reactive({
  operator: '',
  action: '',
  start_time: '',
  end_time: '',
  page: 1,
  page_size: 20
})

async function fetchLogs() {
  loading.value = true
  try {
    const res = await getAuditLogs(query)
    logs.value = res.data.items
    total.value = res.data.total
  } catch {
    ElMessage.error('加载失败')
  } finally {
    loading.value = false
  }
}

function search() {
  if (dateRange.value) {
    query.start_time = dateRange.value[0].toISOString()
    query.end_time = dateRange.value[1].toISOString()
  } else {
    query.start_time = ''
    query.end_time = ''
  }
  query.page = 1
  fetchLogs()
}

function formatTime(t: string) {
  return new Date(t).toLocaleString('zh-CN')
}

async function handleExport() {
  try {
    const res = await exportAuditLogs(query)
    const url = URL.createObjectURL(res.data)
    const a = document.createElement('a')
    a.href = url
    a.download = 'audit_logs.csv'
    a.click()
    URL.revokeObjectURL(url)
  } catch {
    ElMessage.error('导出失败')
  }
}

onMounted(fetchLogs)
</script>

<style scoped>
.audit-log {
  padding: 20px;
}
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}
.page-header h2 {
  margin: 0;
}
.filter-form {
  margin-bottom: 16px;
}
.el-pagination {
  margin-top: 16px;
  justify-content: flex-end;
}
</style>
