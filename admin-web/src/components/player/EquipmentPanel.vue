<template>
  <div class="equipment-panel">
    <el-card>
      <template #header>
        <div class="card-header">
          <span>装备 ({{ items.length }} 件)</span>
          <el-button size="small" @click="refresh" :loading="loading">
            刷新
          </el-button>
        </div>
      </template>

      <div v-if="loading" class="loading-container">
        <el-skeleton :rows="3" animated />
      </div>

      <div v-else class="equipment-slots">
        <div
          v-for="slot in equipmentSlots"
          :key="slot.id"
          class="equip-slot"
          :class="{ 'has-item': getItemBySlot(slot.id) }"
          @click="showItemDetail(getItemBySlot(slot.id))"
        >
          <div class="slot-label">{{ slot.name }}</div>
          <div class="slot-content">
            <template v-if="getItemBySlot(slot.id)">
              <el-icon><Goods /></el-icon>
              <span class="item-id">{{ getItemBySlot(slot.id)?.item_template_id }}</span>
              <span v-if="getItemBySlot(slot.id)?.enhancement_level" class="enhance">
                +{{ getItemBySlot(slot.id)?.enhancement_level }}
              </span>
            </template>
            <template v-else>
              <span class="empty-slot">空</span>
            </template>
          </div>
        </div>
      </div>
    </el-card>

    <el-dialog v-model="dialogVisible" title="装备详情" width="400px">
      <el-descriptions v-if="selectedItem" :column="1" border>
        <el-descriptions-item label="槽位">{{ getSlotName(selectedItem.slot) }}</el-descriptions-item>
        <el-descriptions-item label="模板ID">{{ selectedItem.item_template_id }}</el-descriptions-item>
        <el-descriptions-item label="实例ID">{{ selectedItem.instance_id }}</el-descriptions-item>
        <el-descriptions-item label="耐久度">{{ selectedItem.durability }}</el-descriptions-item>
        <el-descriptions-item label="强化等级">+{{ selectedItem.enhancement_level }}</el-descriptions-item>
      </el-descriptions>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Goods } from '@element-plus/icons-vue'
import { getCharacterEquipment, type EquipmentItem } from '@/api/character'
import { ElMessage } from 'element-plus'

const props = defineProps<{
  characterId: number
}>()

const equipmentSlots = [
  { id: 0, name: '武器' },
  { id: 1, name: '衣服' },
  { id: 2, name: '头盔' },
  { id: 3, name: '项链' },
  { id: 4, name: '左手镯' },
  { id: 5, name: '右手镯' },
  { id: 6, name: '左戒指' },
  { id: 7, name: '右戒指' },
  { id: 8, name: '鞋子' },
  { id: 9, name: '腰带' }
]

const loading = ref(false)
const items = ref<EquipmentItem[]>([])
const dialogVisible = ref(false)
const selectedItem = ref<EquipmentItem | null>(null)

const fetchEquipment = async () => {
  loading.value = true
  try {
    const res = await getCharacterEquipment(props.characterId)
    items.value = res.data.items || []
  } catch (e) {
    ElMessage.error('获取装备失败')
  } finally {
    loading.value = false
  }
}

const getItemBySlot = (slotId: number) => {
  return items.value.find(item => item.slot === slotId)
}

const getSlotName = (slotId: number) => {
  return equipmentSlots.find(s => s.id === slotId)?.name || `槽位${slotId}`
}

const refresh = () => {
  fetchEquipment()
}

const showItemDetail = (item: EquipmentItem | undefined) => {
  if (!item) return
  selectedItem.value = item
  dialogVisible.value = true
}

onMounted(() => {
  fetchEquipment()
})

defineExpose({ refresh })
</script>

<style scoped>
.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.loading-container {
  min-height: 150px;
}

.equipment-slots {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}

.equip-slot {
  border: 1px dashed #dcdfe6;
  border-radius: 4px;
  padding: 12px;
  cursor: pointer;
  transition: all 0.2s;
}

.equip-slot.has-item {
  border-style: solid;
  border-color: #409eff;
  background-color: #ecf5ff;
}

.equip-slot:hover {
  border-color: #409eff;
}

.slot-label {
  font-size: 12px;
  color: #909399;
  margin-bottom: 4px;
}

.slot-content {
  display: flex;
  align-items: center;
  gap: 8px;
}

.item-id {
  font-size: 13px;
  color: #303133;
}

.enhance {
  color: #e6a23c;
  font-weight: bold;
  font-size: 12px;
}

.empty-slot {
  color: #c0c4cc;
  font-size: 12px;
}
</style>
