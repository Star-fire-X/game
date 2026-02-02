import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/login',
      name: 'Login',
      component: () => import('@/views/login/LoginView.vue')
    },
    {
      path: '/',
      name: 'Dashboard',
      component: () => import('@/views/DashboardView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/services',
      name: 'Services',
      component: () => import('@/views/services/ServiceList.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/alerts/config',
      name: 'AlertConfig',
      component: () => import('@/views/alerts/AlertConfig.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/audit',
      name: 'AuditLog',
      component: () => import('@/views/audit/AuditLog.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/players/online',
      name: 'OnlinePlayers',
      component: () => import('@/views/players/OnlineList.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/players/:id',
      name: 'PlayerDetail',
      component: () => import('@/views/players/PlayerDetail.vue'),
      meta: { requiresAuth: true }
    }
  ]
})

router.beforeEach((to, from, next) => {
  const authStore = useAuthStore()
  if (to.meta.requiresAuth && !authStore.isLoggedIn) {
    next('/login')
  } else {
    next()
  }
})

export default router
