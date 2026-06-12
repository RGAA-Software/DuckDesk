import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import MainPage from '@/views/MainPage.vue'
import AboutView from '@/views/AboutView.vue'

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
      path: '/main-test',
      name: 'about',
      // route level code-splitting
      // this generates a separate chunk (About.[hash].js) for this route
      // which is lazy-loaded when the route is visited.
      component: () => import('../views/AboutView.vue'),
    },
    {
      path: '/terms',
      name: 'terms',
      // route level code-splitting
      // this generates a separate chunk (About.[hash].js) for this route
      // which is lazy-loaded when the route is visited.
      component: () => import('../views/TermsView.vue'),
    },
    {
      path: '/privacy',
      name: 'privacy',
      // route level code-splitting
      // this generates a separate chunk (About.[hash].js) for this route
      // which is lazy-loaded when the route is visited.
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
