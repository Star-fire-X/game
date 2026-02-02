<template>
  <div class="service-panel">
    <div class="panel-header">
      <h2>服务状态</h2>
      <el-button :icon="Refresh" @click="refresh" :loading="loading">
        刷新
      </el-button>
    </div>
    <div class="service-grid">
      <ServiceCard
        v-for="svc in services"
        :key="svc.name"
        :service="svc"
      />
    </div>
    <p v-if="!loading && services.length === 0" class="empty">
      暂无服务数据
    </p>
  </div>
</template>

<script setup lang="ts">
import { onMounted, onUnmounted, ref } from 'vue'
import { storeToRefs } from 'pinia'
import { Refresh } from '@element-plus/icons-vue'
import { useServiceStore } from '@/stores/service'
import ServiceCard from '@/components/ServiceCard.vue'

const serviceStore = useServiceStore()
const { services, loading } = storeToRefs(serviceStore)

let timer: number | null = null
const REFRESH_INTERVAL = 5000

function refresh() {
  serviceStore.fetchServices()
}

onMounted(() => {
  refresh()
  timer = window.setInterval(refresh, REFRESH_INTERVAL)
})

onUnmounted(() => {
  if (timer) clearInterval(timer)
})
</script>

<style scoped>
.service-panel {
  padding: 20px;
}
.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
}
.panel-header h2 {
  margin: 0;
}
.service-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 16px;
}
.empty {
  text-align: center;
  color: #909399;
}
</style>
