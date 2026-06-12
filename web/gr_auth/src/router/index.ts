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
      redirect: '/main/auth-list',
      component: () => import('@/views/MainPanel.vue'),
      children: [
        {
          path: 'auth-list',
          component: () => import('@/views/AuthList.vue'),
        },
        {
          path: 'admin-list',
          component: () => import('@/views/AdminList.vue'),
        },
      ]
    },
  ],
})

export default router
