<template>
  <div class="dashboard">
    <el-container>
      <el-header>
        <div class="header-content">
          <h1>Legend2 管理后台</h1>
          <el-dropdown @command="handleCommand">
            <span class="user-info">
              {{ authStore.user?.username }}
              <el-icon><ArrowDown /></el-icon>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="logout">退出登录</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>
      <el-main>
        <el-card>
          <h2>欢迎使用管理后台</h2>
          <p>当前用户: {{ authStore.user?.username }}</p>
          <p>角色: {{ authStore.user?.role }}</p>
        </el-card>
      </el-main>
    </el-container>
  </div>
</template>

<script setup lang="ts">
import { useRouter } from 'vue-router'
import { ArrowDown } from '@element-plus/icons-vue'
import { useAuthStore } from '@/stores/auth'

const router = useRouter()
const authStore = useAuthStore()

async function handleCommand(cmd: string) {
  if (cmd === 'logout') {
    await authStore.doLogout()
    router.push('/login')
  }
}
</script>

<style scoped>
.dashboard {
  min-height: 100vh;
}
.header-content {
  display: flex;
  justify-content: space-between;
  align-items: center;
  height: 100%;
}
.header-content h1 {
  margin: 0;
  font-size: 20px;
}
.user-info {
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 4px;
}
</style>
