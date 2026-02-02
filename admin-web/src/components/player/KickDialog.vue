<template>
  <el-dialog v-model="visible" title="踢出玩家" width="400px" @close="handleClose">
    <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
      <el-form-item label="角色名">
        <span>{{ player?.name }}</span>
      </el-form-item>
      <el-form-item label="原因" prop="reason">
        <el-input v-model="form.reason" type="textarea" :rows="3" placeholder="请输入踢出原因（至少5个字符）" />
      </el-form-item>
    </el-form>
    <template #footer>
      <el-button @click="visible = false">取消</el-button>
      <el-button type="primary" :loading="loading" @click="handleSubmit">确认踢出</el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { ElMessage, type FormInstance } from 'element-plus'
import { kickPlayer } from '@/api/player_actions'
import type { OnlinePlayer } from '@/api/player'

const props = defineProps<{ player: OnlinePlayer | null }>()
const emit = defineEmits(['update:modelValue', 'success'])
const visible = defineModel<boolean>({ default: false })

const formRef = ref<FormInstance>()
const loading = ref(false)
const form = ref({ reason: '' })

const rules = {
  reason: [{ required: true, min: 5, message: '原因至少5个字符', trigger: 'blur' }]
}

watch(visible, (val) => { if (!val) form.value.reason = '' })

function handleClose() {
  formRef.value?.resetFields()
}

async function handleSubmit() {
  if (!props.player) return
  await formRef.value?.validate()
  loading.value = true
  try {
    await kickPlayer(props.player.id, { reason: form.value.reason })
    ElMessage.success('踢出成功')
    visible.value = false
    emit('success')
  } catch (e: any) {
    ElMessage.error(e.message || '操作失败')
  } finally {
    loading.value = false
  }
}
</script>
