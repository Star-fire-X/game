import { defineStore } from 'pinia'
import { ref } from 'vue'
import type { ServiceStatus } from '@/api/service'
import { getServices } from '@/api/service'

export const useServiceStore = defineStore('service', () => {
  const services = ref<ServiceStatus[]>([])
  const loading = ref(false)
  const error = ref<string | null>(null)

  async function fetchServices() {
    loading.value = true
    error.value = null
    try {
      const res = await getServices()
      services.value = res.data
    } catch (e: any) {
      error.value = e.message || 'Failed to fetch services'
    } finally {
      loading.value = false
    }
  }

  return { services, loading, error, fetchServices }
})
