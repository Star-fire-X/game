<template>
  <div class="service-controls">
    <el-button-group>
      <el-button
        type="success"
        :icon="VideoPlay"
        :disabled="service.status === 'running'"
        :loading="starting"
        @click="handleStart"
      >
        启动
      </el-button>
      <el-button
        type="warning"
        :icon="RefreshRight"
        :loading="restarting"
        @click="handleRestart"
      >
        重启
      </el-button>
      <el-button
        type="danger"
        :icon="VideoPause"
        :disabled="service.status === 'stopped'"
        :loading="stopping"
        @click="confirmStop"
      >
        停止
      </el-button>
    </el-button-group>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { ElMessageBox, ElMessage } from 'element-plus'
import { VideoPlay, VideoPause, RefreshRight } from '@element-plus/icons-vue'
import type { ServiceStatus } from '@/api/service'
import { startService, stopService, restartService } from '@/api/service_control'

const props = defineProps<{
  service: ServiceStatus
}>()

const emit = defineEmits<{
  (e: 'refresh'): void
}>()

const starting = ref(false)
const stopping = ref(false)
const restarting = ref(false)

async function handleStart() {
  starting.value = true
  try {
    await startService(props.service.name)
    ElMessage.success(`${props.service.name} 启动成功`)
    emit('refresh')
  } catch (e: any) {
    ElMessage.error(e.message || '启动失败')
  } finally {
    starting.value = false
  }
}

async function handleRestart() {
  restarting.value = true
  try {
    await restartService(props.service.name)
    ElMessage.success(`${props.service.name} 重启成功`)
    emit('refresh')
  } catch (e: any) {
    ElMessage.error(e.message || '重启失败')
  } finally {
    restarting.value = false
  }
}

async function confirmStop() {
  const msg = `确定要停止 ${props.service.name} 吗？当前连接数: ${props.service.connections}`
  try {
    await ElMessageBox.confirm(msg, '停止服务', {
      confirmButtonText: '确定停止',
      cancelButtonText: '取消',
      type: 'warning'
    })
    await handleStop()
  } catch {
    // cancelled
  }
}

async function handleStop() {
  stopping.value = true
  try {
    await stopService(props.service.name)
    ElMessage.success(`${props.service.name} 已停止`)
    emit('refresh')
  } catch (e: any) {
    ElMessage.error(e.message || '停止失败')
  } finally {
    stopping.value = false
  }
}
</script>

<style scoped>
.service-controls {
  margin-top: 12px;
}
</style>
