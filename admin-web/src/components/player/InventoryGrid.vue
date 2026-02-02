<template>
  <div class="inventory-grid">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>背包 ({{ items.length }} 件物品)</span>
          <el-button size="small" @click="refresh" :loading="loading">
            刷新
          </el-button>
        </div>
      </template>

      <div v-if="loading" class="loading-container">
        <el-skeleton :rows="4" animated />
      </div>

      <div v-else-if="items.length === 0" class="empty-container">
        <el-empty description="背包为空" />
      </div>

      <div v-else class="grid-container">
        <div
          v-for="item in items"
          :key="item.id"
          class="inventory-slot"
          @click="showItemDetail(item)"
        >
          <div class="slot-number">{{ item.slot }}</div>
          <div class="item-icon">
            <el-icon><Box /></el-icon>
          </div>
          <div class="item-info">
            <span class="item-id">ID: {{ item.item_template_id }}</span>
            <span v-if="item.quantity > 1" class="item-qty">x{{ item.quantity }}</span>
          </div>
          <div v-if="item.enhancement_level > 0" class="enhancement">
            +{{ item.enhancement_level }}
          </div>
        </div>
      </div>
    </el-card>

    <el-dialog v-model="dialogVisible" title="物品详情" width="400px">
      <el-descriptions v-if="selectedItem" :column="1" border>
        <el-descriptions-item label="槽位">{{ selectedItem.slot }}</el-descriptions-item>
        <el-descriptions-item label="模板ID">{{ selectedItem.item_template_id }}</el-descriptions-item>
        <el-descriptions-item label="实例ID">{{ selectedItem.instance_id }}</el-descriptions-item>
        <el-descriptions-item label="数量">{{ selectedItem.quantity }}</el-descriptions-item>
        <el-descriptions-item label="耐久度">{{ selectedItem.durability }}</el-descriptions-item>
        <el-descriptions-item label="强化等级">+{{ selectedItem.enhancement_level }}</el-descriptions-item>
      </el-descriptions>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Box } from '@element-plus/icons-vue'
import { getCharacterInventory, type InventoryItem } from '@/api/character'
import { ElMessage } from 'element-plus'

const props = defineProps<{
  characterId: number
}>()

const loading = ref(false)
const items = ref<InventoryItem[]>([])
const dialogVisible = ref(false)
const selectedItem = ref<InventoryItem | null>(null)

const fetchInventory = async () => {
  loading.value = true
  try {
    const res = await getCharacterInventory(props.characterId)
    items.value = res.data.items || []
  } catch (e) {
    ElMessage.error('获取背包失败')
  } finally {
    loading.value = false
  }
}

const refresh = () => {
  fetchInventory()
}

const showItemDetail = (item: InventoryItem) => {
  selectedItem.value = item
  dialogVisible.value = true
}

onMounted(() => {
  fetchInventory()
})

defineExpose({ refresh })
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.loading-container,
.empty-container {
  min-height: 200px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.grid-container {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(80px, 1fr));
  gap: 8px;
}

.inventory-slot {
  position: relative;
  border: 1px solid #dcdfe6;
  border-radius: 4px;
  padding: 8px;
  cursor: pointer;
  transition: all 0.2s;
  text-align: center;
}

.inventory-slot:hover {
  border-color: #409eff;
  background-color: #ecf5ff;
}

.slot-number {
  position: absolute;
  top: 2px;
  left: 4px;
  font-size: 10px;
  color: #909399;
}

.item-icon {
  font-size: 24px;
  color: #606266;
  margin-bottom: 4px;
}

.item-info {
  font-size: 11px;
  color: #606266;
}

.item-qty {
  display: block;
  color: #67c23a;
  font-weight: bold;
}

.enhancement {
  position: absolute;
  top: 2px;
  right: 4px;
  font-size: 10px;
  color: #e6a23c;
  font-weight: bold;
}
</style>
