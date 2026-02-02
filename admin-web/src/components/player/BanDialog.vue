<template>
  <el-dialog v-model="visible" title="封禁账号" width="400px" @close="handleClose">
    <el-alert type="warning" :closable="false" class="mb-4">
      封禁后玩家将被踢出游戏且无法登录
    </el-alert>
    <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
      <el-form-item label="角色名">
        <span>{{ player?.name }}</span>
      </el-form-item>
      <el-form-item label="时长" prop="duration">
        <el-select v-model="form.duration" placeholder="选择封禁时长">
          <el-option label="1小时" :value="3600" />
          <el-option label="1天" :value="86400" />
          <el-option label="7天" :value="604800" />
          <el-option label="30天" :value="2592000" />
          <el-option label="永久" :value="0" />
        </el-select>
      </el-form-item>
      <el-form-item label="原因" prop="reason">
        <el-input v-model="form.reason" type="textarea" :rows="3"
          placeholder="请输入封禁原因（至少10个字符）" />
      </el-form-item>
    </el-form>
    <template #footer>
      <el-button @click="visible = false">取消</el-button>
      <el-button type="danger" :loading="loading" @click="handleSubmit">确认封禁</el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import { ElMessage, type FormInstance } from 'element-plus'
import { banPlayer } from '@/api/player_actions'
import type { OnlinePlayer } from '@/api/player'

const props = defineProps<{ player: OnlinePlayer | null }>()
const emit = defineEmits(['update:modelValue', 'success'])
const visible = defineModel<boolean>({ default: false })

const formRef = ref<FormInstance>()
const loading = ref(false)
const form = ref({ duration: 86400, reason: '' })

const rules = {
  duration: [{ required: true, message: '请选择封禁时长', trigger: 'change' }],
  reason: [{ required: true, min: 10, message: '原因至少10个字符', trigger: 'blur' }]
}

watch(visible, (val) => {
  if (!val) form.value = { duration: 86400, reason: '' }
})

function handleClose() { formRef.value?.resetFields() }

async function handleSubmit() {
  if (!props.player) return
  await formRef.value?.validate()
  loading.value = true
  try {
    await banPlayer(props.player.id, form.value)
    ElMessage.success('封禁成功')
    visible.value = false
    emit('success')
  } catch (e: any) {
    ElMessage.error(e.message || '操作失败')
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.mb-4 { margin-bottom: 16px; }
</style>
