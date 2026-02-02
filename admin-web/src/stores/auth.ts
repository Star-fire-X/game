import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { login, logout } from '@/api/auth'

export const useAuthStore = defineStore('auth', () => {
  const token = ref(localStorage.getItem('token') || '')
  const user = ref<{ id: number; username: string; role: string } | null>(null)

  const isLoggedIn = computed(() => !!token.value)

  async function doLogin(username: string, password: string) {
    const res = await login(username, password)
    token.value = res.token
    user.value = res.user
    localStorage.setItem('token', res.token)
  }

  async function doLogout() {
    await logout()
    token.value = ''
    user.value = null
    localStorage.removeItem('token')
  }

  return { token, user, isLoggedIn, doLogin, doLogout }
})
