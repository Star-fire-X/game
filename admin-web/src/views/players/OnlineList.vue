<template>
  <div class="online-players">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>在线玩家列表</span>
          <el-tag type="success">在线: {{ total }}</el-tag>
        </div>
      </template>

      <!-- 搜索栏 -->
      <el-form :inline="true" class="search-form">
        <el-form-item label="角色名">
          <el-input v-model="searchParams.keyword" placeholder="搜索角色名" clearable />
        </el-form-item>
        <el-form-item label="等级">
          <el-input-number v-model="searchParams.min_level" :min="0" placeholder="最小" />
          <span class="mx-2">-</span>
          <el-input-number v-model="searchParams.max_level" :min="0" placeholder="最大" />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" @click="handleSearch">搜索</el-button>
          <el-button @click="handleReset">重置</el-button>
        </el-form-item>
      </el-form>

      <!-- 玩家列表 -->
      <el-table :data="players" v-loading="loading" stripe>
        <el-table-column prop="name" label="角色名" width="120" />
        <el-table-column prop="level" label="等级" width="80" />
        <el-table-column prop="class_name" label="职业" width="80" />
        <el-table-column prop="map_name" label="地图" width="120" />
        <el-table-column label="坐标" width="100">
          <template #default="{ row }">
            {{ row.x }}, {{ row.y }}
          </template>
        </el-table-column>
        <el-table-column label="HP/MP" width="150">
          <template #default="{ row }">
            <div>HP: {{ row.hp }}/{{ row.max_hp }}</div>
            <div>MP: {{ row.mp }}/{{ row.max_mp }}</div>
          </template>
        </el-table-column>
        <el-table-column prop="ip_address" label="IP" width="130" />
        <el-table-column label="在线时长" width="100">
          <template #default="{ row }">
            {{ formatDuration(row.online_duration) }}
          </template>
        </el-table-column>
        <el-table-column label="状态" width="80">
          <template #default="{ row }">
            <el-tag v-if="row.is_muted" type="warning">禁言</el-tag>
            <el-tag v-else type="success">正常</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button size="small" @click="handleKick(row)">踢出</el-button>
            <el-button size="small" type="warning" @click="handleMute(row)">禁言</el-button>
            <el-button size="small" type="danger" @click="handleBan(row)">封号</el-button>
          </template>
        </el-table-column>
      </el-table>

      <!-- 分页 -->
      <el-pagination
        class="pagination"
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :total="total"
        :page-sizes="[20, 50, 100]"
        layout="total, sizes, prev, pager, next"
        @size-change="fetchPlayers"
        @current-change="fetchPlayers"
      />
    </el-card>

    <!-- 踢人对话框 -->
    <KickDialog v-model="kickDialogVisible" :player="selectedPlayer" @success="fetchPlayers" />
    <!-- 禁言对话框 -->
    <MuteDialog v-model="muteDialogVisible" :player="selectedPlayer" @success="fetchPlayers" />
    <!-- 封号对话框 -->
    <BanDialog v-model="banDialogVisible" :player="selectedPlayer" @success="fetchPlayers" />
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { getOnlinePlayers, type OnlinePlayer } from '@/api/player'
import KickDialog from '@/components/player/KickDialog.vue'
import MuteDialog from '@/components/player/MuteDialog.vue'
import BanDialog from '@/components/player/BanDialog.vue'

const loading = ref(false)
const players = ref<OnlinePlayer[]>([])
const total = ref(0)
const currentPage = ref(1)
const pageSize = ref(20)

const searchParams = ref({
  keyword: '',
  min_level: undefined as number | undefined,
  max_level: undefined as number | undefined
})

const selectedPlayer = ref<OnlinePlayer | null>(null)
const kickDialogVisible = ref(false)
const muteDialogVisible = ref(false)
const banDialogVisible = ref(false)

let refreshTimer: number | null = null

async function fetchPlayers() {
  loading.value = true
  try {
    const res = await getOnlinePlayers({
      page: currentPage.value,
      page_size: pageSize.value,
      ...searchParams.value
    })
    players.value = res.data.players || []
    total.value = res.data.total
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  currentPage.value = 1
  fetchPlayers()
}

function handleReset() {
  searchParams.value = { keyword: '', min_level: undefined, max_level: undefined }
  handleSearch()
}

function handleKick(player: OnlinePlayer) {
  selectedPlayer.value = player
  kickDialogVisible.value = true
}

function handleMute(player: OnlinePlayer) {
  selectedPlayer.value = player
  muteDialogVisible.value = true
}

function handleBan(player: OnlinePlayer) {
  selectedPlayer.value = player
  banDialogVisible.value = true
}

function formatDuration(seconds: number): string {
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  return `${h}h ${m}m`
}

onMounted(() => {
  fetchPlayers()
  refreshTimer = window.setInterval(fetchPlayers, 10000)
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
})
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.search-form {
  margin-bottom: 16px;
}
.pagination {
  margin-top: 16px;
  justify-content: flex-end;
}
.mx-2 {
  margin: 0 8px;
}
</style>
