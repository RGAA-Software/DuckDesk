import { createRouter, createWebHistory } from 'vue-router'
import LoginPanel from '@/views/LoginPanel.vue'

const router = createRouter({
  history: createWebHistory(), //import.meta.env.BASE_URL
  routes: [
    {
      path: '/',
      name: 'home',
      component: LoginPanel,
    },
    {
      path: '/main',
      name: 'main',
      redirect: '/main/device-auth',
      component: () => import('@/views/MainPanel.vue'),
      children: [
        {
          path: 'device-auth',
          component: () => import('@/views/DeviceAuthList.vue'),
        },
        {
          path: 'auth-list',
          component: () => import('@/views/AuthList.vue'),
        },
        {
          path: 'admin-list',
          meta: { requiresAdmin: true },
          component: () => import('@/views/AdminList.vue'),
        },
      ]
    },
  ],
})

router.beforeEach((to) => {
  const token = sessionStorage.getItem('login_token')
  if (to.path.startsWith('/main') && !token) {
    return '/'
  }
  if (to.meta.requiresAdmin && sessionStorage.getItem('login_role') !== 'admin') {
    return '/main/auth-list'
  }
})

export default router
