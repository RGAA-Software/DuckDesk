import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/',
      name: '',
      component: () => import('@/views/MainPage.vue'),
      redirect: '/main',
      children: [
        {
          path: 'main',
          component: () => import('@/views/HomeView.vue'),
        },
        {
          path: 'products/godesk',
          component: () => import('@/views/products/GodeskView.vue'),
        },
        {
          path: 'products/goxr',
          component: () => import('@/views/products/GoxrView.vue'),
        },
        {
          path: 'products/cybermonitor',
          component: () => import('@/views/products/CyberMonitorView.vue'),
        },
        {
          path: 'price',
          component: () => import('@/views/PriceView.vue'),
        },
        {
          path: 'docs',
          component: () => import('@/views/DocsView.vue'),
        }
      ]
    },
    {
      path: '/admin',
      name: 'admin-login',
      component: () => import('../views/admin/AdminLogin.vue'),
    },
    {
      path: '/admin/panel',
      name: 'admin-panel',
      component: () => import('../views/admin/AdminPanel.vue'),
    },
    {
      path: '/terms',
      name: 'terms',
      component: () => import('../views/TermsView.vue'),
    },
    {
      path: '/privacy',
      name: 'privacy',
      component: () => import('../views/PrivacyTerms.vue'),
    },
  ],

  scrollBehavior(to, from, savedPosition) {
    // 返回上一页（浏览器后退）
    if (savedPosition) {
      return savedPosition
    }
    // 新页面：回到顶部
    return { top: 0 }
  }

})

export default router
