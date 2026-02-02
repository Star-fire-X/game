<template>
  <el-dialog v-model="visible" title="禁言玩家" width="400px" @close="handleClose">
    <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
      <el-form-item label="角色名">
        <span>{{ player?.name }}</span>
      </el-form-item>
      <el-form-item label="时长" prop="duration">
        <el-select v-model="form.duration" placeholder="选择禁言时长">
          <el-option label="10分钟" :value="600" />
          <el-option label="1小时" :value="3600" />
          <el-option label="1天" :value="86400" />
          <el-option label="7天" :value="604800" />
          <el-option label="永久" :value="0" />
        </el-select>
      </el-form-item>
      <el-form-item label="原因" prop="reason">
        <el-input v-model="form.reason" type="textarea" :rows="3" placeholder="请输入禁言原因（至少5个字符）" />
      </el-form-item>
    </el-form>
    <template #footer>
      <el-button @click="visible = false">取消</el-button>
      <el-button type="warning" :loading="loading" @click="handleSubmit">确认禁言</el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { ElMessage, type FormInstance } from 'element-plus'
import { mutePlayer } from '@/api/player_actions'
import type { OnlinePlayer } from '@/api/player'

const props = defineProps<{ player: OnlinePlayer | null }>()
const emit = defineEmits(['update:modelValue', 'success'])
const visible = defineModel<boolean>({ default: false })

const formRef = ref<FormInstance>()
const loading = ref(false)
const form = ref({ duration: 3600, reason: '' })

const rules = {
  duration: [{ required: true, message: '请选择禁言时长', trigger: 'change' }],
  reason: [{ required: true, min: 5, message: '原因至少5个字符', trigger: 'blur' }]
}

watch(visible, (val) => { if (!val) { form.value = { duration: 3600, reason: '' } } })

function handleClose() { formRef.value?.resetFields() }

async function handleSubmit() {
  if (!props.player) return
  await formRef.value?.validate()
  loading.value = true
  try {
    await mutePlayer(props.player.id, form.value)
    ElMessage.success('禁言成功')
    visible.value = false
    emit('success')
  } catch (e: any) {
    ElMessage.error(e.message || '操作失败')
  } finally {
    loading.value = false
  }
}
</script>
