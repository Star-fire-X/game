<template>
  <div class="player-detail">
    <el-page-header @back="goBack" :title="'返回'" :content="characterName" />

    <div v-if="loading" class="loading-container">
      <el-skeleton :rows="6" animated />
    </div>

    <template v-else-if="character">
      <el-row :gutter="20" class="info-section">
        <el-col :span="24">
          <el-card>
            <template #header>
              <span>角色信息</span>
            </template>
            <el-descriptions :column="3" border>
              <el-descriptions-item label="角色ID">{{ character.id }}</el-descriptions-item>
              <el-descriptions-item label="角色名">{{ character.name }}</el-descriptions-item>
              <el-descriptions-item label="等级">{{ character.level }}</el-descriptions-item>
              <el-descriptions-item label="职业">{{ getClassName(character.class) }}</el-descriptions-item>
              <el-descriptions-item label="性别">{{ character.gender === 0 ? '男' : '女' }}</el-descriptions-item>
              <el-descriptions-item label="金币">{{ formatGold(character.gold) }}</el-descriptions-item>
              <el-descriptions-item label="HP">{{ character.hp }} / {{ character.max_hp }}</el-descriptions-item>
              <el-descriptions-item label="MP">{{ character.mp }} / {{ character.max_mp }}</el-descriptions-item>
              <el-descriptions-item label="经验">{{ character.experience }}</el-descriptions-item>
              <el-descriptions-item label="地图">{{ character.map_id }}</el-descriptions-item>
              <el-descriptions-item label="坐标">{{ character.x }}, {{ character.y }}</el-descriptions-item>
              <el-descriptions-item label="最后登录">{{ character.last_login_at || '-' }}</el-descriptions-item>
            </el-descriptions>
          </el-card>
        </el-col>
      </el-row>

      <el-row :gutter="20" class="data-section">
        <el-col :span="12">
          <EquipmentPanel :character-id="characterId" ref="equipRef" />
        </el-col>
        <el-col :span="12">
          <InventoryGrid :character-id="characterId" ref="invRef" />
        </el-col>
      </el-row>
    </template>

    <el-empty v-else description="角色不存在" />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { getCharacterById, type Character } from '@/api/character'
import EquipmentPanel from '@/components/player/EquipmentPanel.vue'
import InventoryGrid from '@/components/player/InventoryGrid.vue'
import { ElMessage } from 'element-plus'

const route = useRoute()
const router = useRouter()

const characterId = computed(() => Number(route.params.id))
const loading = ref(false)
const character = ref<Character | null>(null)
const equipRef = ref()
const invRef = ref()

const characterName = computed(() => character.value?.name || '角色详情')

const classNames: Record<number, string> = {
  0: '战士',
  1: '法师',
  2: '道士'
}

const getClassName = (classId: number) => classNames[classId] || '未知'

const formatGold = (gold: number) => {
  return gold.toLocaleString()
}

const goBack = () => {
  router.back()
}

const fetchCharacter = async () => {
  loading.value = true
  try {
    const res = await getCharacterById(characterId.value)
    character.value = res.data
  } catch (e) {
    ElMessage.error('获取角色信息失败')
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  fetchCharacter()
})
</script>

<style scoped>
.player-detail {
  padding: 20px;
}

.loading-container {
  margin-top: 20px;
}

.info-section {
  margin-top: 20px;
}

.data-section {
  margin-top: 20px;
}
</style>
